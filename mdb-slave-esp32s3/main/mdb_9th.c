#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include <rom/ets_sys.h>

#include "mdb_9th.h"

#define BITS_TIME(d) (d + 512)/1042

QueueHandle_t mdb_queue;

rmt_channel_handle_t    tx_chan = NULL;
rmt_symbol_word_t       tx_symbols[256];
rmt_encoder_handle_t    tx_rmt_encoder;
rmt_transmit_config_t   tx_cfg = { .loop_count = 0, .flags.eot_level = 1 };

rmt_symbol_word_t       rx_symbols[256];
QueueHandle_t           rx_queue;

void write_9(uint16_t *payload, size_t len) {

    size_t sy= 0;
    for(int w= 0; w < len; w++){

        uint16_t nth9= payload[w];

        tx_symbols[sy].duration0 = 1042;
        tx_symbols[sy].level0 = 0;
        tx_symbols[sy].duration1 = 1042;
        tx_symbols[sy].level1 = (nth9 >> 0) & 1;
        ++sy;

        tx_symbols[sy].duration0 = 1042;
        tx_symbols[sy].level0 = (nth9 >> 1) & 1;
        tx_symbols[sy].duration1 = 1042;
        tx_symbols[sy].level1 = (nth9 >> 2) & 1;
        ++sy;

        tx_symbols[sy].duration0 = 1042;
        tx_symbols[sy].level0 = (nth9 >> 3) & 1;
        tx_symbols[sy].duration1 = 1042;
        tx_symbols[sy].level1 = (nth9 >> 4) & 1;
        ++sy;

        tx_symbols[sy].duration0 = 1042;
        tx_symbols[sy].level0 = (nth9 >> 5) & 1;
        tx_symbols[sy].duration1 = 1042;
        tx_symbols[sy].level1 = (nth9 >> 6) & 1;
        ++sy;

        tx_symbols[sy].duration0 = 1042;
        tx_symbols[sy].level0 = (nth9 >> 7) & 1;
        tx_symbols[sy].duration1 = 1042;
        tx_symbols[sy].level1 = (nth9 >> 8) & 1;
        ++sy;

        tx_symbols[sy].duration0 = 521;
        tx_symbols[sy].level0 = 1;
        tx_symbols[sy].duration1 = 521;
        tx_symbols[sy].level1 = 1;
        ++sy;
    }

    tx_symbols[sy].duration0 = 0;
    tx_symbols[sy].level0 = 1;
    tx_symbols[sy].duration1 = 0;
    tx_symbols[sy].level1 = 1;
    ++sy;

    rmt_transmit(tx_chan, tx_rmt_encoder, tx_symbols, sy * sizeof(rmt_symbol_word_t), &tx_cfg);
}

uint16_t read_9(uint8_t *checksum) {

    uint16_t coming_read = 0;
    xQueueReceive(mdb_queue, &coming_read, portMAX_DELAY);

	if (checksum)
		*checksum += coming_read;

	return coming_read;
}

static void bits_to_mdb(uint8_t *out_bits, size_t n_bits) {

    int x= 0;
    while(x < n_bits){

        if(out_bits[x++] != 0) continue;

        uint16_t coming = 0x0000;
        for(int y= 0; y < 9; y++)
            coming |= out_bits[x++] << y;

        xQueueSend(mdb_queue, &coming, 0);
    }
}

static int symbols_to_bits(rmt_symbol_word_t *symb, size_t n_syms, uint8_t *out_bits) {

    int idx = 0;

    for(int x= 0; x < n_syms; x++){
        rmt_symbol_word_t *aux_symbol = &symb[x];

        for(int y= 0; y < BITS_TIME(aux_symbol->duration0); y++){
            out_bits[idx++] = aux_symbol->level0;
        }

        for(int y= 0; y < BITS_TIME(aux_symbol->duration1); y++){
            out_bits[idx++] = aux_symbol->level1;
        }
    }

    return idx;
}

static bool IRAM_ATTR rmt_rx_done_cb(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *user_ctx) {

    BaseType_t woken = pdFALSE;
    xQueueSendFromISR((QueueHandle_t) user_ctx, edata, &woken);

    return woken == pdTRUE;
}

static void rx_task(void *arg) {

    rmt_channel_handle_t rx_chan = (rmt_channel_handle_t)arg;

    rmt_receive_config_t recv_cfg = {
        .signal_range_min_ns = 3000UL,      // ignora pulsos < 3 µs (ruído)
        .signal_range_max_ns = 2000000UL,   // silêncio de 2 ms = fim de frame
    };

    uint8_t out_bits[1024];

    rmt_rx_done_event_data_t event;

    rmt_receive(rx_chan, rx_symbols, sizeof(rx_symbols), &recv_cfg);
    while (true) {

        if (xQueueReceive(rx_queue, &event, portMAX_DELAY) != pdTRUE)
            continue;

        int tot = symbols_to_bits(event.received_symbols, event.num_symbols, (uint8_t*) &out_bits);

        bits_to_mdb((uint8_t*) &out_bits, tot);

        rmt_receive(rx_chan, rx_symbols, sizeof(rx_symbols), &recv_cfg);
    }
}

void mdb_9th_init(void) {

	rx_queue = xQueueCreate(4, sizeof(rmt_rx_done_event_data_t));
    mdb_queue = xQueueCreate(64, sizeof(uint16_t));

    rmt_rx_channel_config_t rx_chan_config = {
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .gpio_num          = PIN_MDB_RX,
        .mem_block_symbols = 64,
        .resolution_hz     = 10000000, // 10 MHz (1 tick = 0.1 µs)
    };
    rmt_channel_handle_t rx_chan = NULL;
    rmt_new_rx_channel(&rx_chan_config, &rx_chan);

    rmt_rx_event_callbacks_t cbs = {
        .on_recv_done = rmt_rx_done_cb,
    };
    rmt_rx_register_event_callbacks(rx_chan, &cbs, rx_queue);

    rmt_enable(rx_chan);

    xTaskCreate(rx_task, "mdb_rx", 4096, rx_chan, configMAX_PRIORITIES - 2, NULL);

	///////////////////////////////////////////////////////////////////////////////////////////
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = PIN_MDB_TX,
        .mem_block_symbols = 64,
        .resolution_hz = 10000000, // Clock RMT: 10 MHz → 1 tick = 0,1 µs
        .trans_queue_depth = 4 };

    rmt_new_tx_channel(&tx_chan_config, &tx_chan);

    rmt_enable(tx_chan);

    rmt_copy_encoder_config_t copy_cfg = {};
    rmt_new_copy_encoder(&copy_cfg, &tx_rmt_encoder);
}