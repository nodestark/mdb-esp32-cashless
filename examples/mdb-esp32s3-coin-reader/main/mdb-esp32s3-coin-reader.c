/*
 * VMflow.xyz
 *
 * mdb-esp32s3-coin-reader.c - MDB Coin Changer peripheral (ESP32-S3)
 *
 * Emulates an MDB Level 3 coin changer (address 0x08). The BOOT button
 * on GPIO0 injects a 25c coin (type 2) event for bench testing.
 *
 * MDB lines are bit-banged at 9600 baud, 9 data bits (the 9th bit is the
 * MODE bit). RX is sampled inside a falling-edge ISR; TX is shifted out by
 * busy-wait. Same low-level scheme as the cashless peripheral.
 */

#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <rom/ets_sys.h>
#include <driver/gpio.h>
#include <soc/gpio_struct.h>

#define TAG "mdb_changer"

#define PIN_MDB_RX      GPIO_NUM_4
#define PIN_MDB_TX      GPIO_NUM_5
#define PIN_BOOT_BTN    GPIO_NUM_0   // BOOT button -> simulate 25c coin (type 2)

//---------------- MDB protocol --------------------//
#define BIT_MODE_SET    0b100000000
#define BIT_ADD_SET     0b011111000
#define BIT_CMD_SET     0b000000111

#define ACK             0x00    // Acknowledgment / checksum correct
#define RET             0xAA    // Retransmit previously sent data (VMC only)
#define NAK             0xFF    // Negative acknowledge

#define CHANGER_ADDR    0x08

enum CHANGER_CMD {
    CHANGER_RESET       = 0x00,
    CHANGER_SETUP       = 0x01,
    CHANGER_TUBE_STATUS = 0x02,
    CHANGER_POLL        = 0x03,
    CHANGER_COIN_TYPE   = 0x04,
    CHANGER_DISPENSE    = 0x05,
    CHANGER_EXPANSION   = 0x07,
};

// POLL status/error codes
enum CHANGER_STATUS {
    ST_ESCROW_REQUEST       = 0x01,
    ST_CHANGER_PAYOUT_BUSY  = 0x02,
    ST_NO_CREDIT            = 0x03,
    ST_DEFECTIVE_SENSOR     = 0x04,
    ST_DOUBLE_ARRIVAL       = 0x05,
    ST_ACCEPTOR_UNPLUGGED   = 0x06,
    ST_TUBE_JAM             = 0x07,
    ST_ROM_CHECKSUM         = 0x08,
    ST_COIN_ROUTING_ERR     = 0x09,
    ST_CHANGER_BUSY         = 0x0A,
    ST_CHANGER_RESET        = 0x0B,  // Changer was reset
    ST_COIN_JAM             = 0x0C,
};

typedef enum {
    INACTIVE_STATE,
    DISABLED_STATE,
    ENABLED_STATE,
} changer_state_t;

//---------------- Configuration -------------------//
// Scaling factor 1, 2 decimal places => credit 5 == $0.05.
#define MDB_CURRENCY_CODE   0xFFFF
#define MDB_SCALE_FACTOR    0x01
#define MDB_DECIMAL_PLACES  0x02
#define TUBE_CAPACITY       100

// Coin values (credit * scale_factor)
static const uint8_t coin_credit[16] = {5, 10, 25, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

//---------------- Runtime state -------------------//
static changer_state_t changer_state = INACTIVE_STATE;
static bool     reset_changer_todo = false;
static uint16_t coin_enable    = 0x0000;   // set by COIN TYPE command
static uint16_t dispense_enable = 0x0000;  // set by COIN TYPE command
static uint8_t  tube_count[16]  = {0};

static volatile uint32_t pending_coins = 0;
static QueueHandle_t mdb_rx_queue;

//---------------- POLL event ring buffer ----------//
// Touched only by the MDB task, so no locking is required.
#define POLL_QSIZE 32
static uint8_t poll_q[POLL_QSIZE];
static uint8_t poll_q_head = 0;
static uint8_t poll_q_tail = 0;

static void poll_q_push(uint8_t b) {
    uint8_t next = (poll_q_head + 1) % POLL_QSIZE;
    if (next != poll_q_tail) {
        poll_q[poll_q_head] = b;
        poll_q_head = next;
    } else {
        ESP_LOGW(TAG, "poll queue full, event dropped");
    }
}

static bool poll_q_empty(void) { return poll_q_head == poll_q_tail; }

static uint8_t poll_q_pop(void) {
    uint8_t b = poll_q[poll_q_tail];
    poll_q_tail = (poll_q_tail + 1) % POLL_QSIZE;
    return b;
}

//---------------- Coin simulation -----------------//
static void simulate_coin(uint8_t coin_type) {
    if (coin_type > 15) return;

    if (changer_state != ENABLED_STATE) {
        ESP_LOGW(TAG, "coin ignored: changer not enabled");
        return;
    }
    if (!(coin_enable & (1 << coin_type))) {
        ESP_LOGW(TAG, "coin ignored: type=%d not enabled", coin_type);
        return;
    }

    // Determine routing: route to tube if space exists, otherwise to cash box
    uint8_t routing = 0; // 0 = cash box, 1 = tubes
    if (tube_count[coin_type] < TUBE_CAPACITY) {
        tube_count[coin_type]++;
        routing = 1; // tubes
    } else {
        routing = 0; // cash box
    }

    // Push 2-byte event to poll queue:
    // Byte 1: 0x40 | (routing << 4) | coin_type
    // Byte 2: tube_count[coin_type]
    poll_q_push(0x40 | (routing << 4) | coin_type);
    poll_q_push(tube_count[coin_type]);

    ESP_LOGI(TAG, "coin deposited: type=%d credit=%d routing=%s tube_count=%d",
             coin_type, coin_credit[coin_type],
             routing == 1 ? "TUBES" : "CASHBOX",
             tube_count[coin_type]);
}

//---------------- 9-bit bit-bang I/O --------------//
static void IRAM_ATTR mdb_rx_falling_isr(void *arg) {
    gpio_intr_disable(PIN_MDB_RX);

    uint16_t coming_read = 0x0000;

    ets_delay_us(156);   // skip start bit, sample mid first data bit

    for (int x = 0; x < 9; x++) {
        coming_read |= (gpio_get_level(PIN_MDB_RX) << x);
        ets_delay_us(104);
    }
    xQueueSendFromISR(mdb_rx_queue, &coming_read, NULL);

    gpio_intr_enable(PIN_MDB_RX);
}

static uint16_t read_9(uint8_t *checksum) {
    uint16_t coming_read = 0;
    xQueueReceive(mdb_rx_queue, &coming_read, portMAX_DELAY);

    if (checksum)
        *checksum += coming_read;

    return coming_read;
}

static void write_9(uint16_t nth9) {
    gpio_set_level(PIN_MDB_TX, 0);   // start bit
    ets_delay_us(104);

    for (uint8_t x = 0; x < 9; x++) {
        gpio_set_level(PIN_MDB_TX, (nth9 >> x) & 1);
        ets_delay_us(104);
    }

    gpio_set_level(PIN_MDB_TX, 1);   // stop bit
    ets_delay_us(104);
}

static void write_payload_9(uint8_t *payload, uint8_t length) {
    uint8_t checksum = 0x00;

    // Half-duplex: don't listen while we talk. The RX falling-edge ISR would
    // otherwise trigger on our own transmission (TX/RX are coupled on the shared
    // point-to-point bus) and queue spurious bytes, desyncing the framing so the
    // very next command from the VMC (the POLL right after this ACK) was lost.
    gpio_intr_disable(PIN_MDB_RX);

    for (uint8_t x = 0; x < length; x++) {
        checksum += payload[x];
        write_9(payload[x]);
    }

    write_9(BIT_MODE_SET | checksum);   // checksum carries the MODE bit

    // TX (GPIO5) capacitively couples into the adjacent RX pin (GPIO4). The
    // resulting edge gets LATCHED in the interrupt status while RX is disabled
    // and, on re-enable, fires a spurious ISR that desyncs reception - so the
    // coin dropped exactly one command after every reply. Let it settle, drop
    // any queued garbage, clear the latched edge, then re-arm clean.
    esp_rom_delay_us(200);
    xQueueReset(mdb_rx_queue);
    GPIO.status_w1tc = (1U << PIN_MDB_RX);   // clear latched RX edge (GPIO4 < 32)
    gpio_intr_enable(PIN_MDB_RX);
}

//---------------- BOOT button ---------------------//
static void IRAM_ATTR boot_btn_isr(void *arg) {
    static int64_t last_us = 0;
    int64_t now = esp_timer_get_time();

    if (now - last_us < 200000) return;   // 200 ms debounce
    last_us = now;

    pending_coins++;
}

//---------------- MDB changer task ----------------//
static void mdb_changer_task(void *pvParameters) {
    uint8_t mdb_payload[36];

    for (;;) {
        uint8_t checksum = 0x00;
        uint8_t available_tx = 0;

        uint16_t coming_read = read_9(&checksum);

        if (!(coming_read & BIT_MODE_SET)) continue;

        if ((uint8_t) coming_read == ACK) continue;
        if ((uint8_t) coming_read == RET) continue;
        if ((uint8_t) coming_read == NAK) continue;

        if ((coming_read & BIT_ADD_SET) != CHANGER_ADDR) continue;

        switch (coming_read & BIT_CMD_SET) {

        case CHANGER_RESET: {
            if (read_9(NULL) != checksum) continue;

            reset_changer_todo = true;
            changer_state = INACTIVE_STATE;
            coin_enable = 0x0000;
            dispense_enable = 0x0000;

            ESP_LOGI(TAG, "RESET");
            break;   // ACK (empty payload -> checksum only)
        }

        case CHANGER_SETUP: {
            if (read_9(NULL) != checksum) continue;

            changer_state = DISABLED_STATE;

            mdb_payload[0]  = 0x03;                          // Feature Level (Level 3)
            mdb_payload[1]  = MDB_CURRENCY_CODE >> 8;        // Currency Code High
            mdb_payload[2]  = MDB_CURRENCY_CODE & 0xFF;      // Currency Code Low
            mdb_payload[3]  = MDB_SCALE_FACTOR;              // Coin Scaling Factor (1 byte)
            mdb_payload[4]  = MDB_DECIMAL_PLACES;            // Decimal Places (1 byte)
            mdb_payload[5]  = 0x00;                          // Coin Type Routing High (types 8-15)
            mdb_payload[6]  = 0x0F;                          // Coin Type Routing Low (types 0-7 can go to tubes -> 0x000F)
            for (int i = 0; i < 16; i++) {
                mdb_payload[7 + i] = coin_credit[i];
            }
            available_tx = 23;

            ESP_LOGI(TAG, "SETUP -> DISABLED");
            break;
        }

        case CHANGER_TUBE_STATUS: {
            if (read_9(NULL) != checksum) continue;

            uint16_t tube_full_mask = 0;
            for (int i = 0; i < 16; i++) {
                if (tube_count[i] >= TUBE_CAPACITY) {
                    tube_full_mask |= (1 << i);
                }
            }

            mdb_payload[0] = (tube_full_mask >> 8) & 0xFF; // Z1 (MSB)
            mdb_payload[1] = tube_full_mask & 0xFF;        // Z2 (LSB)
            for (int i = 0; i < 16; i++) {
                mdb_payload[2 + i] = tube_count[i];
            }
            available_tx = 18;

            ESP_LOGI(TAG, "TUBE_STATUS sent (tubes: 5c=%d, 10c=%d, 25c=%d, $1=%d)",
                     tube_count[0], tube_count[1], tube_count[2], tube_count[3]);
            break;
        }

        case CHANGER_POLL: {
            if (read_9(NULL) != checksum) continue;

            // Inject queued BOOT-button coins (type 2 - 25 cents).
            while (pending_coins) {
                pending_coins--;
                simulate_coin(2);
            }

            if (reset_changer_todo) {
                reset_changer_todo = false;
                changer_state = DISABLED_STATE;
                mdb_payload[0] = ST_CHANGER_RESET;   // Changer was reset
                available_tx = 1;
                ESP_LOGI(TAG, "POLL -> Changer was Reset");
            } else {
                while (!poll_q_empty() && available_tx < 15) {
                    uint8_t b = poll_q_pop();
                    mdb_payload[available_tx++] = b;

                    // If this represents a coin deposit event (bits 7:6 are 01, i.e., 0x40 to 0x7F)
                    if ((b & 0xC0) == 0x40) {
                        if (!poll_q_empty() && available_tx < 16) {
                            uint8_t count = poll_q_pop();
                            mdb_payload[available_tx++] = count;
                            ESP_LOGI(TAG, "POLL -> Coin Deposit: type=%d routing=%d tube_count=%d",
                                     b & 0x0F, (b >> 4) & 0x03, count);
                        } else {
                            mdb_payload[available_tx++] = 0;
                        }
                    } else {
                        ESP_LOGI(TAG, "POLL -> status 0x%02X", b);
                    }
                }
            }
            break;
        }

        case CHANGER_COIN_TYPE: {
            uint8_t coin_en_high = (uint8_t) read_9(&checksum);   // coin enable high
            uint8_t coin_en_low  = (uint8_t) read_9(&checksum);   // coin enable low
            uint8_t disp_en_high = (uint8_t) read_9(&checksum);   // dispense enable high
            uint8_t disp_en_low  = (uint8_t) read_9(&checksum);   // dispense enable low
            if (read_9(NULL) != checksum) continue;

            coin_enable     = ((uint16_t) coin_en_high << 8) | coin_en_low;
            dispense_enable = ((uint16_t) disp_en_high << 8) | disp_en_low;

            changer_state = coin_enable ? ENABLED_STATE : DISABLED_STATE;

            ESP_LOGI(TAG, "COIN_TYPE coin_enable=0x%04X dispense_enable=0x%04X -> %s",
                     coin_enable, dispense_enable,
                     changer_state == ENABLED_STATE ? "ENABLED" : "DISABLED");
            break;   // ACK
        }

        case CHANGER_DISPENSE: {
            uint8_t disp_val = (uint8_t) read_9(&checksum);
            if (read_9(NULL) != checksum) continue;

            uint8_t count = (disp_val >> 4) & 0x0F;
            uint8_t coin_type = disp_val & 0x0F;

            ESP_LOGI(TAG, "DISPENSE: count=%d type=%d", count, coin_type);

            if (coin_type < 16 && tube_count[coin_type] >= count) {
                tube_count[coin_type] -= count;
                ESP_LOGI(TAG, "DISPENSE success: type=%d count=%d (remaining=%d)",
                         coin_type, count, tube_count[coin_type]);
            } else {
                ESP_LOGE(TAG, "DISPENSE failed: type=%d count=%d (inventory=%d)",
                         coin_type, count, coin_type < 16 ? tube_count[coin_type] : 0);
            }
            break; // ACK
        }

        case CHANGER_EXPANSION: {
            uint8_t sub = (uint8_t) read_9(&checksum);

            if (sub == 0x00) {   // REQUEST_ID / Identification
                if (read_9(NULL) != checksum) continue;

                memcpy(&mdb_payload[0],  "MDB", 3);   // Manufacturer Code
                memset(&mdb_payload[3],  ' ', 12);    // Serial Number
                memset(&mdb_payload[15], ' ', 12);    // Model / Tuning Revision
                mdb_payload[27] = 0x00;               // Software Version High
                mdb_payload[28] = 0x01;               // Software Version Low
                mdb_payload[29] = 0x00;               // Optional Features byte 1
                mdb_payload[30] = 0x00;               // Optional Features byte 2
                mdb_payload[31] = 0x00;               // Optional Features byte 3
                mdb_payload[32] = 0x00;               // Optional Features byte 4
                available_tx = 33;

                ESP_LOGI(TAG, "EXPANSION REQUEST_ID");
            }
            else if (sub == 0x01) {   // FEATURE_ENABLE
                (void) read_9(&checksum);
                (void) read_9(&checksum);
                (void) read_9(&checksum);
                (void) read_9(&checksum);
                if (read_9(NULL) != checksum) continue;

                ESP_LOGI(TAG, "EXPANSION FEATURE_ENABLE");
                // just ACK
            }
            break;
        }

        }

        write_payload_9(mdb_payload, available_tx);
    }
}

//---------------- Entry point ---------------------//
void app_main(void) {
    // MDB RX: input, falling-edge interrupt.
    gpio_config_t rx_cfg = {
        .pin_bit_mask = 1ULL << PIN_MDB_RX,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&rx_cfg);

    // MDB TX: output, idle high.
    gpio_config_t tx_cfg = {
        .pin_bit_mask = 1ULL << PIN_MDB_TX,
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&tx_cfg);
    gpio_set_level(PIN_MDB_TX, 1);

    // BOOT button: input, pull-up, falling-edge interrupt.
    gpio_config_t btn_cfg = {
        .pin_bit_mask = 1ULL << PIN_BOOT_BTN,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&btn_cfg);

    mdb_rx_queue = xQueueCreate(64, sizeof(uint16_t));

    gpio_install_isr_service(0);
    gpio_isr_handler_add(PIN_MDB_RX, mdb_rx_falling_isr, NULL);
    gpio_isr_handler_add(PIN_BOOT_BTN, boot_btn_isr, NULL);

    ESP_LOGI(TAG, "init RX=%d TX=%d addr=0x%02X (BOOT btn -> 25c coin)",
             PIN_MDB_RX, PIN_MDB_TX, CHANGER_ADDR);

    xTaskCreate(mdb_changer_task, "mdb_changer", 4096, NULL, 10, NULL);
}
