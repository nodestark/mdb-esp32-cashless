#ifndef WEBUI_SERVER_H
#define WEBUI_SERVER_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* ---------- SoftAP defaults ---------- */

#ifndef CONFIG_AP_SSID
#define CONFIG_AP_SSID "VMC-Simulator"
#endif
#ifndef CONFIG_AP_PASSWORD
#define CONFIG_AP_PASSWORD "12345678"
#endif

/* ---------- MDB Log Ringbuffer ---------- */

#define MDB_LOG_SIZE 200

typedef struct {
    uint32_t timestamp_ms;
    char direction;         // 'T' = TX, 'R' = RX, 'I' = Info
    char description[64];
} mdb_log_entry_t;

/* ---------- Command Queue ---------- */

typedef enum {
    CMD_VEND_REQUEST,
    CMD_VEND_CANCEL,
    CMD_CASH_SALE,
    CMD_RESET,
    CMD_READER_ENABLE,
    CMD_READER_DISABLE,
    CMD_SESSION_COMPLETE,
} vmc_command_type_t;

typedef struct {
    vmc_command_type_t type;
    uint8_t  target_addr;   // 0x10 or 0x60, 0 = any active
    uint16_t item_number;
    uint16_t item_price;    // scaled price (0 = random)
} vmc_command_t;

/* ---------- Product catalog ---------- */

#define MAX_PRODUCTS 10

typedef struct {
    uint8_t slot;
    const char *name;
    float price;
} product_t;

extern const product_t products[MAX_PRODUCTS];

/* ---------- Shared state (defined in mdb-master-esp32s3.c) ---------- */

typedef enum {
    INACTIVE_STATE, DISABLED_STATE, ENABLED_STATE, IDLE_STATE, VEND_STATE
} machine_state_t;

struct reader_t {
    uint8_t featureLevel;
    uint16_t countryCode;
    uint16_t coinTypeRouting;
    uint16_t tubeFullStatus;
    uint8_t scaleFactor;
    uint8_t decimalPlaces;
    uint8_t responseTimeSec;
    uint8_t tubeCounts[16];
    uint8_t miscellaneous;
    uint8_t poll_fail_count;
    machine_state_t machineState;

    // Expansion ID fields
    char manufacturer[4];   // 3 chars + null
    char serial[13];        // 12 chars + null
    char model[13];         // 12 chars + null
    char sw_version[3];     // 2 chars + null
};

extern struct reader_t reader0x10;
extern struct reader_t reader0x60;
extern QueueHandle_t command_queue;

/* ---------- MDB Log functions ---------- */

extern mdb_log_entry_t mdb_log[];
extern volatile int mdb_log_head;
extern volatile int mdb_log_count;

void mdb_log_add(char direction, const char *fmt, ...);

/* ---------- Server functions ---------- */

void start_wifi(void);
void start_webui_server(void);
void stop_webui_server(void);
void start_dns_server(void);
void stop_dns_server(void);

#endif
