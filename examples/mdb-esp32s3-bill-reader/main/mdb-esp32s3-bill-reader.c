/*
 * VMflow.xyz
 *
 * mdb-esp32s3-bill-reader.c - MDB Bill Validator peripheral (ESP32-S3)
 *
 * Emulates an MDB Level 1 bill validator (address 0x30). The BOOT button
 * on GPIO0 injects a $1 bill (type 0) event for bench testing.
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

#define TAG "mdb_bill"

#define PIN_MDB_RX      GPIO_NUM_4
#define PIN_MDB_TX      GPIO_NUM_5
#define PIN_BOOT_BTN    GPIO_NUM_0   // BOOT button -> simulate $1 bill (type 0)

//---------------- MDB protocol --------------------//
#define BIT_MODE_SET    0b100000000
#define BIT_ADD_SET     0b011111000
#define BIT_CMD_SET     0b000000111

#define ACK             0x00    // Acknowledgment / checksum correct
#define RET             0xAA    // Retransmit previously sent data (VMC only)
#define NAK             0xFF    // Negative acknowledge

#define VALIDATOR_ADDR  0x30

enum VALIDATOR_CMD {
    VAL_RESET       = 0x00,
    VAL_SETUP       = 0x01,
    VAL_SECURITY    = 0x02,
    VAL_POLL        = 0x03,
    VAL_BILL_TYPE   = 0x04,
    VAL_ESCROW      = 0x05,
    VAL_STACKER     = 0x06,
    VAL_EXPANSION   = 0x07,
};

// POLL status codes (bit7 = 0)
enum VALIDATOR_STATUS {
    ST_DEFECTIVE_MOTOR   = 0x01,
    ST_SENSOR_PROBLEM    = 0x02,
    ST_VALIDATOR_BUSY    = 0x03,
    ST_ROM_CHECKSUM      = 0x04,
    ST_VALIDATOR_JAMMED  = 0x05,
    ST_VALIDATOR_RESET   = 0x06,   // Just Reset
    ST_BILL_REMOVED      = 0x07,
    ST_CASHBOX_OOP       = 0x08,
    ST_VALIDATOR_DISABLE = 0x09,
    ST_INVALID_ESCROW    = 0x0A,
    ST_BILL_REJECTED     = 0x0B,
    ST_CREDITED_REMOVAL  = 0x0C,
};

// Bill routing (bit7 = 1, bits 6-4 = route, bits 3-0 = type)
#define ROUTE_STACKED   (0 << 4)   // 0x80 | type
#define ROUTE_ESCROW    (1 << 4)   // 0x90 | type
#define ROUTE_RETURNED  (2 << 4)   // 0xA0 | type

typedef enum {
    INACTIVE_STATE,
    DISABLED_STATE,
    ENABLED_STATE,
} validator_state_t;

//---------------- Configuration -------------------//
// Currency 0xFFFF, scale factor 100, 2 decimals => credit 1 == $1.00.
#define MDB_CURRENCY_CODE   0xFFFF
#define MDB_SCALE_FACTOR    0x0064
#define MDB_DECIMAL_PLACES  0x02
#define STACKER_CAPACITY    500

// Bill credit in scaling-factor units. credit * scale_factor == cents/100.
static const uint8_t bill_credit[16] = {1, 2, 5, 10, 20, 50, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0};

//---------------- Runtime state -------------------//
static validator_state_t validator_state = INACTIVE_STATE;
static bool     reset_validator_todo = false;
static uint16_t bill_enable    = 0x0000;   // set by BILL TYPE command
static uint16_t bill_escrow_en = 0x0000;   // set by BILL TYPE command
static uint16_t stacker_count  = 0;

// $1 bills queued by the BOOT button ISR, drained on the next POLL.
static volatile uint32_t pending_bills = 0;

static QueueHandle_t mdb_rx_queue;

//---------------- POLL event ring buffer ----------//
// Touched only by the MDB task, so no locking is required.
#define POLL_QSIZE 16
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

//---------------- Bill simulation -----------------//
static void simulate_bill(uint8_t bill_type) {
    if (bill_type > 15) return;

    if (validator_state != ENABLED_STATE) {
        ESP_LOGW(TAG, "bill ignored: validator not enabled");
        return;
    }
    if (!(bill_enable & (1 << bill_type))) {
        ESP_LOGW(TAG, "bill ignored: type=%d not enabled", bill_type);
        return;
    }

    // No escrow: bill is always accepted and stacked immediately.
    if (stacker_count < STACKER_CAPACITY) stacker_count++;
    poll_q_push(0x80 | ROUTE_STACKED | bill_type);
    ESP_LOGI(TAG, "bill stacked: type=%d credit=%d poll=0x%02X",
             bill_type, bill_credit[bill_type], 0x80 | ROUTE_STACKED | bill_type);
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

    // Half-duplex: don't listen while we talk. TX (GPIO5) couples into the adjacent
    // RX pin (GPIO4); the edge gets latched while RX is disabled and, on re-enable,
    // fires a spurious ISR that desyncs reception - so the next command from the VMC
    // right after this reply was dropped. Settle, drop garbage, clear edge, re-arm.
    gpio_intr_disable(PIN_MDB_RX);

    for (uint8_t x = 0; x < length; x++) {
        checksum += payload[x];
        write_9(payload[x]);
    }

    write_9(BIT_MODE_SET | checksum);   // checksum carries the MODE bit

    ets_delay_us(200);
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

    pending_bills++;
}

//---------------- MDB validator task --------------//
static void mdb_validator_task(void *pvParameters) {
    uint8_t mdb_payload[36];

    for (;;) {
        uint8_t checksum = 0x00;
        uint8_t available_tx = 0;

        uint16_t coming_read = read_9(&checksum);

        if (!(coming_read & BIT_MODE_SET)) continue;

        if ((uint8_t) coming_read == ACK) continue;
        if ((uint8_t) coming_read == RET) continue;
        if ((uint8_t) coming_read == NAK) continue;

        if ((coming_read & BIT_ADD_SET) != VALIDATOR_ADDR) continue;

        switch (coming_read & BIT_CMD_SET) {

        case VAL_RESET: {
            if (read_9(NULL) != checksum) continue;

            reset_validator_todo = true;
            validator_state = INACTIVE_STATE;
            bill_enable = 0x0000;
            bill_escrow_en = 0x0000;

            ESP_LOGI(TAG, "RESET");
            break;   // ACK (empty payload -> checksum only)
        }

        case VAL_SETUP: {
            if (read_9(NULL) != checksum) continue;

            validator_state = DISABLED_STATE;

            mdb_payload[0]  = 0x01;                          // Feature Level
            mdb_payload[1]  = MDB_CURRENCY_CODE >> 8;        // Currency Code High
            mdb_payload[2]  = MDB_CURRENCY_CODE & 0xFF;      // Currency Code Low
            mdb_payload[3]  = MDB_SCALE_FACTOR >> 8;         // Scaling Factor High
            mdb_payload[4]  = MDB_SCALE_FACTOR & 0xFF;       // Scaling Factor Low
            mdb_payload[5]  = MDB_DECIMAL_PLACES;            // Decimal Places
            mdb_payload[6]  = (STACKER_CAPACITY >> 8) & 0xFF;// Stacker Capacity High
            mdb_payload[7]  = STACKER_CAPACITY & 0xFF;       // Stacker Capacity Low
            mdb_payload[8]  = 0x00;                          // Security Levels High
            mdb_payload[9]  = 0x00;                          // Security Levels Low
            mdb_payload[10] = 0x00;                          // Escrow (0 = no escrow)
            for (int i = 0; i < 16; i++) mdb_payload[11 + i] = bill_credit[i];
            available_tx = 27;

            ESP_LOGI(TAG, "SETUP -> DISABLED");
            break;
        }

        case VAL_SECURITY: {
            (void) read_9(&checksum);   // security level high
            (void) read_9(&checksum);   // security level low
            if (read_9(NULL) != checksum) continue;

            ESP_LOGI(TAG, "SECURITY");
            break;   // ACK
        }

        case VAL_POLL: {
            if (read_9(NULL) != checksum) continue;

            // Inject queued BOOT-button $1 bills (type 0).
            while (pending_bills) {
                pending_bills--;
                simulate_bill(0);
            }

            if (reset_validator_todo) {
                reset_validator_todo = false;
                validator_state = DISABLED_STATE;
                mdb_payload[0] = ST_VALIDATOR_RESET;   // Just Reset
                available_tx = 1;
                ESP_LOGI(TAG, "POLL -> Just Reset");
            } else {
                while (!poll_q_empty() && available_tx < 16) {
                    uint8_t b = poll_q_pop();
                    mdb_payload[available_tx++] = b;
                    if (b & 0x80)
                        ESP_LOGI(TAG, "POLL -> bill event 0x%02X (route=%d type=%d)",
                                 b, (b >> 4) & 0x07, b & 0x0F);
                    else
                        ESP_LOGI(TAG, "POLL -> status 0x%02X", b);
                }
            }
            break;
        }

        case VAL_BILL_TYPE: {
            uint8_t en_high  = (uint8_t) read_9(&checksum);   // bill enable high
            uint8_t en_low   = (uint8_t) read_9(&checksum);   // bill enable low
            uint8_t esc_high = (uint8_t) read_9(&checksum);   // bill escrow enable high
            uint8_t esc_low  = (uint8_t) read_9(&checksum);   // bill escrow enable low
            if (read_9(NULL) != checksum) continue;

            bill_enable    = ((uint16_t) en_high  << 8) | en_low;
            bill_escrow_en = ((uint16_t) esc_high << 8) | esc_low;

            validator_state = bill_enable ? ENABLED_STATE : DISABLED_STATE;

            ESP_LOGI(TAG, "BILL_TYPE enable=0x%04X escrow=0x%04X -> %s",
                     bill_enable, bill_escrow_en,
                     validator_state == ENABLED_STATE ? "ENABLED" : "DISABLED");
            break;   // ACK
        }

        case VAL_ESCROW: {
            // No escrow capability: bills already stacked at insertion. Just ACK.
            (void) read_9(&checksum);   // escrow byte (bit0: 1=stack, 0=return)
            if (read_9(NULL) != checksum) continue;

            ESP_LOGI(TAG, "ESCROW (no-op, escrow disabled)");
            break;   // ACK
        }

        case VAL_STACKER: {
            if (read_9(NULL) != checksum) continue;

            uint16_t full = (stacker_count >= STACKER_CAPACITY) ? 0x8000 : 0x0000;
            uint16_t val  = full | (stacker_count & 0x7FFF);
            mdb_payload[0] = (val >> 8) & 0xFF;   // bit15 = full flag
            mdb_payload[1] = val & 0xFF;
            available_tx = 2;

            ESP_LOGI(TAG, "STACKER count=%d full=%d", stacker_count, full ? 1 : 0);
            break;
        }

        case VAL_EXPANSION: {
            uint8_t sub = (uint8_t) read_9(&checksum);

            if (sub == 0x00) {   // REQUEST_ID / Identification
                if (read_9(NULL) != checksum) continue;

                memcpy(&mdb_payload[0],  "MDB", 3);   // Manufacturer Code
                memset(&mdb_payload[3],  ' ', 12);    // Serial Number
                memset(&mdb_payload[15], ' ', 12);    // Model / Tuning Revision
                mdb_payload[27] = 0x00;               // Software Version High
                mdb_payload[28] = 0x01;               // Software Version Low
                available_tx = 29;

                ESP_LOGI(TAG, "EXPANSION REQUEST_ID");
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

    ESP_LOGI(TAG, "init RX=%d TX=%d addr=0x%02X (BOOT btn -> $1 bill)",
             PIN_MDB_RX, PIN_MDB_TX, VALIDATOR_ADDR);

    xTaskCreate(mdb_validator_task, "mdb_validator", 4096, NULL, 10, NULL);
}
