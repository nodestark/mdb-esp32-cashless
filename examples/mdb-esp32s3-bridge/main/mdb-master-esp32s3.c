/*
 * VMflow.xyz
 *
 * mdb-master-esp32s3.c - MDB controller <-> target bridge (coin + bill)
 *                        with self-contained cashless emulation on target.
 *
 * Two independent MDB ports:
 *  - controller port (UART2, master): drives physical coin changer (0x08)
 *    and bill validator (0x30). Plug-and-play FSM (INACTIVE→DISABLED→ENABLED).
 *  - target port (bit-banged GPIO, slave): emulates cashless (0x10), coin
 *    changer (0x08) and bill validator (0x30) toward the VMC. Always online.
 *
 * Cashless (0x10) on the target port is fully self-contained — no physical
 * cashless device is bridged. Credit is granted by BLE (phone app) or MQTT
 * RPC; vend approve/deny is decided locally based on available funds.
 *
 * Connectivity: WiFi STA, MQTT (mqtt.vmflow.xyz), BLE NimBLE (config + pay).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>
#include <sdkconfig.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_app_desc.h>
#include <esp_ota_ops.h>
#include <esp_https_ota.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <nvs_flash.h>
#include <rom/ets_sys.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <soc/gpio_struct.h>
#include <esp_wifi.h>
#include <mqtt_client.h>
#include <esp_sntp.h>
#include <led_strip.h>

#include "nimble.h"
#include "rpc-auth.h"

#define TAG "mdb_bridge"

#define TO_SCALE_FACTOR(p, scale_to, dec_to)       (p / scale_to / pow(10, -(dec_to)))
#define FROM_SCALE_FACTOR(p, scale_from, dec_from) (p * scale_from * pow(10, -(dec_from)))

//------------------------------------------------------------------------//
// Pin definitions
//------------------------------------------------------------------------//

#define PIN_TARGET_RX     GPIO_NUM_4
#define PIN_TARGET_TX     GPIO_NUM_5
#define PIN_CONTROLLER_RX GPIO_NUM_1
#define PIN_CONTROLLER_TX GPIO_NUM_2
#define PIN_MDB_LED       GPIO_NUM_21

//------------------------------------------------------------------------//
// MDB constants
//------------------------------------------------------------------------//

#define ACK          0x00
#define RET          0xAA
#define NAK          0xFF

#define BIT_MODE_SET 0b100000000
#define BIT_ADD_SET  0b011111000
#define BIT_CMD_SET  0b000000111

#define ADDR_CASHLESS  0x10
#define ADDR_CHANGER   0x08
#define ADDR_VALIDATOR 0x30

enum CASHLESS_CMD {
    CSHL_RESET     = 0x00,
    CSHL_SETUP     = 0x01,
    CSHL_POLL      = 0x02,
    CSHL_VEND      = 0x03,
    CSHL_READER    = 0x04,
    CSHL_EXPANSION = 0x07,
};

enum CHANGER_CMD {
    CHGR_RESET       = 0x00,
    CHGR_SETUP       = 0x01,
    CHGR_TUBE_STATUS = 0x02,
    CHGR_POLL        = 0x03,
    CHGR_COIN_TYPE   = 0x04,
    CHGR_DISPENSE    = 0x05,
    CHGR_EXPANSION   = 0x07,
};

enum VALIDATOR_CMD {
    VLD_RESET     = 0x00,
    VLD_SETUP     = 0x01,
    VLD_SECURITY  = 0x02,
    VLD_POLL      = 0x03,
    VLD_BILL_TYPE = 0x04,
    VLD_ESCROW    = 0x05,
    VLD_STACKER   = 0x06,
    VLD_EXPANSION = 0x07,
};

typedef enum {
    INACTIVE_STATE,
    DISABLED_STATE,
    ENABLED_STATE,
    IDLE_STATE,
    VEND_STATE,
} device_state_t;

//------------------------------------------------------------------------//
// Physical device snapshots (written by controller task, read by target)
//------------------------------------------------------------------------//

typedef struct {
    uint8_t        feature_level;
    uint16_t       country_code;
    uint8_t        scale_factor;
    uint8_t        decimal_places;
    uint16_t       coin_type_routing;
    uint16_t       tube_full_status;
    uint8_t        tube_counts[16];
    uint8_t        coin_credit[16];
    uint8_t        poll_fail_count;
    device_state_t state;
} changer_t;

typedef struct {
    uint8_t        feature_level;
    uint16_t       country_code;
    uint16_t       scale_factor;
    uint8_t        decimal_places;
    uint16_t       bill_stacker_capacity;
    uint8_t        bill_credit[16];
    uint16_t       stacker_count;
    uint32_t       stacker_value;   // sum of bill_credit of bills stacked since boot (scale units)
    bool           stacker_full;
    uint8_t        poll_fail_count;
    device_state_t state;
} validator_t;

static changer_t   phys_changer   = { .state = INACTIVE_STATE };
static validator_t phys_validator = { .state = INACTIVE_STATE };

static const uint8_t CHANGER_DEFAULT_COIN_CREDIT[16]  = {0};
#define CHANGER_DEFAULT_FEATURE_LEVEL  0x03
#define CHANGER_DEFAULT_COIN_ROUTING   0x000F

static const uint8_t VALIDATOR_DEFAULT_BILL_CREDIT[16] = {0};
#define VALIDATOR_DEFAULT_SCALE_FACTOR 0x0064

//------------------------------------------------------------------------//
// Bridge queues (coin + bill only; cashless is self-contained on target)
//------------------------------------------------------------------------//

typedef struct {
    uint8_t coin_type;
    uint8_t tube_count;
    uint8_t routing;
} coin_deposit_evt_t;
static QueueHandle_t coin_to_target_queue;

typedef struct {
    enum { COIN_REQ_TYPE_ENABLE, COIN_REQ_DISPENSE } type;
    uint16_t coin_enable;
    uint16_t dispense_enable;
    uint8_t  dispense_value;
} coin_from_target_evt_t;
static QueueHandle_t coin_from_target_queue;

typedef struct { uint8_t bill_type; } bill_stack_evt_t;
static QueueHandle_t bill_to_target_queue;

typedef struct {
    enum { BILL_REQ_TYPE_ENABLE, BILL_REQ_ESCROW } type;
    uint16_t bill_enable;
    uint16_t bill_escrow_enable;
    uint8_t  escrow_command;
} bill_from_target_evt_t;
static QueueHandle_t bill_from_target_queue;

// Credit queue: uint16_t funds (device scale units; 0xFFFF = unlimited).
// Pushed by BLE payment handler and MQTT RPC credit command.
static QueueHandle_t mdb_session_queue;

//------------------------------------------------------------------------//
// Connectivity globals
//------------------------------------------------------------------------//

enum BIT_STATUS {
    BIT_STATUS_MQTT       = (1 << 0),
    BIT_STATUS_MDB        = (1 << 1),
    BIT_STATUS_PSSKEY     = (1 << 2),
    BIT_STATUS_DOMAIN     = (1 << 3),
    BIT_STATUS_TRIGGER    = (1 << 4),
    MASK_STATUS_INSTALLED = (BIT_STATUS_PSSKEY | BIT_STATUS_DOMAIN)
};

EventGroupHandle_t xLedEventGroup;

char my_subdomain[32];
#define PASSKEY_LEN 18
char my_passkey[PASSKEY_LEN + 1];

esp_mqtt_client_handle_t mqtt_client = NULL;
led_strip_handle_t led_strip;

static char s_ip_wifi[16] = "";

#define WIFI_BACKOFF_MIN_MS  5000
#define WIFI_BACKOFF_MAX_MS  300000
static uint32_t wifi_backoff_ms = WIFI_BACKOFF_MIN_MS;
static esp_timer_handle_t wifi_retry_timer;

#define RPC_FRESHNESS_SEC  10
#define BLE_FRESHNESS_SEC  60

// Target cashless flags — set by BLE/MQTT tasks, consumed by target task.
static volatile device_state_t target_cshl_state  = INACTIVE_STATE;
static volatile bool vend_approved_todo   = false;
static volatile bool vend_denied_todo     = false;
static volatile bool session_end_todo     = false;
static volatile bool session_cancel_todo  = false;
static volatile bool out_of_sequence_todo = false;

// Big-endian helpers for BLE wire payload.
static inline uint32_t read_u32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static inline uint16_t read_u16(const uint8_t *p) {
    return ((uint16_t)p[0] << 8) | p[1];
}
static inline void write_u32(uint8_t *p, uint32_t v) {
    p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}
static inline void write_u16(uint8_t *p, uint16_t v) {
    p[0] = v >> 8; p[1] = v;
}

//------------------------------------------------------------------------//
// BLE payment helpers
// Wire: 19 bytes — [0] CMD | [1-4] PRICE u32 cents | [5-6] ITEM u16 |
//                  [7-10] TIME u32 | [11-14] reserved | [15-18] HMAC[:4]
//------------------------------------------------------------------------//

static void ble_encode_with_passkey(uint8_t cmd, uint16_t item_price,
                                    uint16_t item_number, uint8_t *out) {
    uint32_t price_wire = (uint32_t)TO_SCALE_FACTOR(
        FROM_SCALE_FACTOR(item_price, CONFIG_MDB_SCALE_FACTOR, CONFIG_MDB_DECIMAL_PLACES), 1, 2);
    out[0] = cmd;
    write_u32(&out[1], price_wire);
    write_u16(&out[5], item_number);
    write_u32(&out[7], (uint32_t)time(NULL));
    write_u32(&out[11], 0);
    unsigned char hmac[32];
    calculate_hmac((const char *)out, 15, hmac);
    memcpy(out + 15, hmac, 4);
}

static esp_err_t ble_decode_with_passkey(uint16_t *item_price,
                                         uint16_t *item_number,
                                         const uint8_t *in) {
    unsigned char hmac[32];
    calculate_hmac((const char *)in, 15, hmac);
    uint8_t diff = 0;
    for (int i = 0; i < 4; i++) diff |= hmac[i] ^ in[15 + i];
    if (diff) return ESP_ERR_INVALID_CRC;
    int32_t ts = (int32_t)read_u32(&in[7]);
    if (abs((int32_t)time(NULL) - ts) > BLE_FRESHNESS_SEC) return ESP_ERR_TIMEOUT;
    if (item_price)
        *item_price = (uint16_t)TO_SCALE_FACTOR(
            FROM_SCALE_FACTOR((int32_t)read_u32(&in[1]), 1, 2),
            CONFIG_MDB_SCALE_FACTOR, CONFIG_MDB_DECIMAL_PLACES);
    if (item_number)
        *item_number = read_u16(&in[5]);
    return ESP_OK;
}

//------------------------------------------------------------------------//
// Target port: bit-banged 9-bit MDB I/O (slave toward VMC)
//------------------------------------------------------------------------//

static QueueHandle_t mdb_rx_queue;

static void IRAM_ATTR mdb_rx_falling_isr(void *arg) {
    gpio_intr_disable(PIN_TARGET_RX);
    uint16_t v = 0;
    ets_delay_us(156);
    for (int x = 0; x < 9; x++) {
        v |= (gpio_get_level(PIN_TARGET_RX) << x);
        ets_delay_us(104);
    }
    xQueueSendFromISR(mdb_rx_queue, &v, NULL);
    gpio_intr_enable(PIN_TARGET_RX);
}

static uint16_t read_9(uint8_t *checksum) {
    uint16_t v = 0;
    xQueueReceive(mdb_rx_queue, &v, portMAX_DELAY);
    if (checksum) *checksum += (uint8_t)v;
    return v;
}

static void write_9(uint16_t nth9) {
    gpio_set_level(PIN_TARGET_TX, 0);
    ets_delay_us(104);
    for (uint8_t x = 0; x < 9; x++) {
        gpio_set_level(PIN_TARGET_TX, (nth9 >> x) & 1);
        ets_delay_us(104);
    }
    gpio_set_level(PIN_TARGET_TX, 1);
    ets_delay_us(104);
}

static void write_payload_9(uint8_t *payload, uint8_t length) {
    uint8_t checksum = 0x00;
    gpio_intr_disable(PIN_TARGET_RX);
    for (int x = 0; x < length; x++) {
        checksum += payload[x];
        write_9(payload[x]);
    }
    write_9(BIT_MODE_SET | checksum);
    ets_delay_us(200);
    xQueueReset(mdb_rx_queue);
    GPIO.status_w1tc = (1U << PIN_TARGET_RX);
    gpio_intr_enable(PIN_TARGET_RX);
}

//------------------------------------------------------------------------//
// Controller port: hardware UART2 9-bit MDB I/O (master toward peripherals)
//------------------------------------------------------------------------//

static void write_controller_9(uint16_t nth9) {
    uint8_t ones = __builtin_popcount((uint8_t)nth9);
    uart_wait_tx_done(UART_NUM_2, pdMS_TO_TICKS(250));
    if ((nth9 >> 8) & 1)
        uart_set_parity(UART_NUM_2, ones % 2 ? UART_PARITY_EVEN : UART_PARITY_ODD);
    else
        uart_set_parity(UART_NUM_2, ones % 2 ? UART_PARITY_ODD : UART_PARITY_EVEN);
    uart_write_bytes(UART_NUM_2, (uint8_t *)&nth9, 1);
    uart_wait_tx_done(UART_NUM_2, pdMS_TO_TICKS(250));
}

static void write_payload_controller_9(uint8_t *payload, uint8_t length) {
    uint8_t checksum = 0;
    write_controller_9((checksum = payload[0]) | BIT_MODE_SET);
    for (uint8_t x = 1; x < length; x++) {
        write_controller_9(payload[x]);
        checksum += payload[x];
    }
    write_controller_9(checksum);
}

//------------------------------------------------------------------------//
// LED status task
//------------------------------------------------------------------------//

#define LED_LVL 30

static void led_status_task(void *pvParameters) {
    while (1) {
        EventBits_t b = xEventGroupWaitBits(xLedEventGroup, BIT_STATUS_TRIGGER,
                                            pdTRUE, pdFALSE, portMAX_DELAY);
        bool installed = (b & MASK_STATUS_INSTALLED) == MASK_STATUS_INSTALLED;
        bool net = b & BIT_STATUS_MQTT;
        bool mdb = b & BIT_STATUS_MDB;
        if (!installed)      led_strip_set_pixel(led_strip, 0, LED_LVL, LED_LVL, LED_LVL);
        else if (net && mdb) led_strip_set_pixel(led_strip, 0,       0, LED_LVL,       0);
        else if (net)        led_strip_set_pixel(led_strip, 0, LED_LVL,       0, LED_LVL);
        else if (mdb)        led_strip_set_pixel(led_strip, 0,       0,       0, LED_LVL);
        else                 led_strip_set_pixel(led_strip, 0, LED_LVL,       0,       0);
        led_strip_refresh(led_strip);
    }
}

//------------------------------------------------------------------------//
// BLE event handlers
//------------------------------------------------------------------------//

static void ble_pax_event_handler(uint16_t devices_count) {
    char topic[64], msg[48], line[128];
    snprintf(msg, sizeof(msg), "%u:%lld", devices_count, (long long)time(NULL));
    rpc_sign_text(msg, line, sizeof(line));
    snprintf(topic, sizeof(topic), "domain.vmflow.xyz/%s/paxcounter", my_subdomain);
    esp_mqtt_client_enqueue(mqtt_client, topic, line, 0, 1, 0, 1);
}

static void ble_event_handler(char *ble_payload) {
    switch ((uint8_t)ble_payload[0]) {
    case 0x00: { // set domain
        nvs_handle_t h;
        if (nvs_open("vmflow", NVS_READWRITE, &h) != ESP_OK) break;
        size_t s_len;
        if (nvs_get_str(h, "domain", NULL, &s_len) != ESP_OK) {
            snprintf(my_subdomain, sizeof(my_subdomain), "%s", ble_payload + 1);
            nvs_set_str(h, "domain", my_subdomain);
            nvs_commit(h);
            char myhost[64];
            snprintf(myhost, sizeof(myhost), "%s.vmflow.xyz", my_subdomain);
            ble_set_device_name(myhost);
            xEventGroupSetBits(xLedEventGroup, BIT_STATUS_DOMAIN | BIT_STATUS_TRIGGER);
        }
        nvs_close(h);
        break;
    }
    case 0x01: { // set passkey
        nvs_handle_t h;
        if (nvs_open("vmflow", NVS_READWRITE, &h) != ESP_OK) break;
        size_t s_len;
        if (nvs_get_str(h, "passkey", NULL, &s_len) != ESP_OK) {
            snprintf(my_passkey, sizeof(my_passkey), "%s", ble_payload + 1);
            nvs_set_str(h, "passkey", my_passkey);
            nvs_commit(h);
            xEventGroupSetBits(xLedEventGroup, BIT_STATUS_PSSKEY | BIT_STATUS_TRIGGER);
        }
        nvs_close(h);
        break;
    }
    case 0x02: { // BLE credit — open unlimited session
        uint16_t funds = 0xFFFF;
        xQueueSend(mdb_session_queue, &funds, 0);
        break;
    }
    case 0x03: { // BLE vend approve (signed by phone)
        // Only ever raise the flag; never clear it, so a decision already
        // pending for the target task cannot be lost to this cross-task write.
        if (ble_decode_with_passkey(NULL, NULL, (const uint8_t *)ble_payload) == ESP_OK
            && target_cshl_state == VEND_STATE)
            vend_approved_todo = true;
        break;
    }
    case 0x04: // BLE session cancel
        if (target_cshl_state >= IDLE_STATE) session_cancel_todo = true;
        break;
    case 0x06: { // set WiFi SSID
        esp_wifi_disconnect();
        wifi_config_t wifi_cfg = {0};
        esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg);
        snprintf((char *)wifi_cfg.sta.ssid, sizeof(wifi_cfg.sta.ssid), "%s", ble_payload + 1);
        esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
        break;
    }
    case 0x07: { // set WiFi password + connect
        wifi_config_t wifi_cfg = {0};
        esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg);
        snprintf((char *)wifi_cfg.sta.password, sizeof(wifi_cfg.sta.password), "%s", ble_payload + 1);
        esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
        esp_wifi_connect();
        break;
    }
    }
}

//------------------------------------------------------------------------//
// WiFi / IP event handlers
//------------------------------------------------------------------------//

static void wifi_retry_cb(void *arg) { esp_wifi_connect(); }

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data) {
    switch (event_id) {
    case WIFI_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case WIFI_EVENT_STA_CONNECTED:
        wifi_backoff_ms = WIFI_BACKOFF_MIN_MS;
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        esp_timer_start_once(wifi_retry_timer, (uint64_t)wifi_backoff_ms * 1000);
        wifi_backoff_ms = wifi_backoff_ms * 2 < WIFI_BACKOFF_MAX_MS
                          ? wifi_backoff_ms * 2 : WIFI_BACKOFF_MAX_MS;
        break;
    }
}

static void ip_event_handler(void *arg, esp_event_base_t base,
                             int32_t event_id, void *event_data) {
    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)event_data;
        snprintf(s_ip_wifi, sizeof(s_ip_wifi), IPSTR, IP2STR(&ev->ip_info.ip));
        ESP_LOGI(TAG, "WiFi IP: %s", s_ip_wifi);
    } else if (event_id == IP_EVENT_STA_LOST_IP) {
        s_ip_wifi[0] = '\0';
    }
}

//------------------------------------------------------------------------//
// MQTT / RPC
//------------------------------------------------------------------------//

// Money currently held in the coin changer tubes, in cents (scale=1, dec=2).
// Live and accurate: derived from the physical changer's tube counts.
static uint32_t coin_vault_cents(void) {
    if (phys_changer.state == INACTIVE_STATE || phys_changer.scale_factor == 0)
        return 0;
    uint32_t raw = 0;
    for (int i = 0; i < 16; i++)
        raw += (uint32_t)phys_changer.tube_counts[i] * phys_changer.coin_credit[i];
    return (uint32_t)TO_SCALE_FACTOR(
        FROM_SCALE_FACTOR(raw, phys_changer.scale_factor, phys_changer.decimal_places), 1, 2);
}

// Money in the bill stacker, in cents. Best-effort: MDB does not report a
// stacked bill's denomination, so this sums bills seen stacked since boot and
// therefore resets on power cycle / stacker emptying.
static uint32_t bill_vault_cents(void) {
    if (phys_validator.scale_factor == 0) return 0;
    return (uint32_t)TO_SCALE_FACTOR(
        FROM_SCALE_FACTOR(phys_validator.stacker_value,
                          phys_validator.scale_factor, phys_validator.decimal_places), 1, 2);
}

static void rpc_publish_info(void) {
    const esp_app_desc_t *app = esp_app_get_description();

    char topic[64], json[512];
    int n = snprintf(json, sizeof(json),
        "{\"version\":\"%s\",\"uptime_s\":%lld,"
        "\"free_heap\":%lu,\"min_free_heap\":%lu,"
        "\"ip_wifi\":\"%s\","
        "\"coin_vault_cents\":%lu,\"bill_stacker_count\":%u}",
        app->version,
        (long long)(esp_timer_get_time() / 1000000),
        (unsigned long)esp_get_free_heap_size(),
        (unsigned long)esp_get_minimum_free_heap_size(),
        s_ip_wifi,
        (unsigned long)coin_vault_cents(),
        phys_validator.stacker_count);

    snprintf(topic, sizeof(topic), "domain.vmflow.xyz/%s/rpc/info", my_subdomain);
    esp_mqtt_client_enqueue(mqtt_client, topic, json, n, 1, 0, 1);
}

// Cash currently in the machine: coin tubes (live) + bill stacker (best-effort).
static void rpc_publish_safe(void) {
    char topic[64], json[192];
    int n = snprintf(json, sizeof(json),
        "{\"coin_vault_cents\":%lu,\"bill_vault_cents\":%lu,"
        "\"bill_stacker_count\":%u}",
        (unsigned long)coin_vault_cents(),
        (unsigned long)bill_vault_cents(),
        phys_validator.stacker_count);

    snprintf(topic, sizeof(topic), "domain.vmflow.xyz/%s/rpc/safe", my_subdomain);
    esp_mqtt_client_enqueue(mqtt_client, topic, json, n, 1, 0, 1);
}

// Publish a signed "price:item:timestamp" event (vend_ok / vend_fail / sale).
// price/item are in device scale units; converted to cents on the wire.
static void publish_signed_event(const char *event, uint16_t price, uint16_t item) {
    if (!mqtt_client) return;
    uint32_t pw = (uint32_t)TO_SCALE_FACTOR(
        FROM_SCALE_FACTOR(price, CONFIG_MDB_SCALE_FACTOR, CONFIG_MDB_DECIMAL_PLACES), 1, 2);
    char topic[64], msg[64], line[160];
    snprintf(msg, sizeof(msg), "%lu:%u:%lld",
             (unsigned long)pw, item, (long long)time(NULL));
    rpc_sign_text(msg, line, sizeof(line));
    snprintf(topic, sizeof(topic), "domain.vmflow.xyz/%s/%s", my_subdomain, event);
    esp_mqtt_client_enqueue(mqtt_client, topic, line, 0, 1, 0, 1);
}

static void ota_task(void *arg) {
    const char *url = (const char *)arg;
    esp_http_client_config_t http_cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 30000,
        .keep_alive_enable = true,
        .buffer_size = 2048,
        .buffer_size_tx = 4096,
    };
    esp_https_ota_config_t ota_cfg = { .http_config = &http_cfg };
    if (esp_https_ota(&ota_cfg) == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    } else {
        vTaskDelete(NULL);
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED: {
        char topic[64], buf[32];
        snprintf(topic, sizeof(topic), "%s.vmflow.xyz/#", my_subdomain);
        esp_mqtt_client_subscribe(client, topic, 0);
        snprintf(topic, sizeof(topic), "domain.vmflow.xyz/%s/status", my_subdomain);
        snprintf(buf, sizeof(buf), "online,%d", (int)esp_reset_reason());
        esp_mqtt_client_enqueue(client, topic, buf, 0, 1, 1, 1);
        xEventGroupSetBits(xLedEventGroup, BIT_STATUS_MQTT | BIT_STATUS_TRIGGER);
        break;
    }
    case MQTT_EVENT_DISCONNECTED:
        xEventGroupClearBits(xLedEventGroup, BIT_STATUS_MQTT);
        xEventGroupSetBits(xLedEventGroup, BIT_STATUS_TRIGGER);
        break;

    case MQTT_EVENT_DATA: {
        if (event->topic_len <= 4) break;
        if (strncmp(event->topic + event->topic_len - 4, "/rpc", 4) != 0) break;

        int len = event->data_len < 127 ? event->data_len : 127;
        char *last_colon = memrchr(event->data, ':', len);
        if (!last_colon) break;

        int prefix_len = last_colon - event->data;
        char hmac[65];
        snprintf(hmac, sizeof(hmac), "%.*s", len - prefix_len - 1, last_colon + 1);

        if (!rpc_verify_hmac(event->data, prefix_len, hmac)) {
            ESP_LOGW(TAG, "RPC rejected: bad HMAC");
            break;
        }

        char cmd[32], args[64];
        unsigned int ts;
        if (sscanf(event->data, "%31[^:]:%63[^:]:%u", cmd, args, &ts) != 3) break;

        long dt = (long)(time(NULL) - (time_t)ts);
        if (labs(dt) > RPC_FRESHNESS_SEC) {
            ESP_LOGW(TAG, "RPC rejected: stale (dt=%ld)", dt);
            break;
        }

        bool has_args = (args[0] != '\0' && strcmp(args, "-") != 0);
        char topic_confirm[64];
        snprintf(topic_confirm, sizeof(topic_confirm),
                 "domain.vmflow.xyz/%s/rpc/confirm", my_subdomain);

        if (strcmp(cmd, "info") == 0) {
            rpc_publish_info();
        } else if (strcmp(cmd, "safe") == 0) {
            rpc_publish_safe();
        } else if (strcmp(cmd, "credit") == 0 && has_args) {
            int32_t price_wire = (int32_t)strtol(args, NULL, 10);
            uint16_t funds = (uint16_t)TO_SCALE_FACTOR(
                FROM_SCALE_FACTOR(price_wire, 1, 2),
                CONFIG_MDB_SCALE_FACTOR, CONFIG_MDB_DECIMAL_PLACES);
            xQueueSend(mdb_session_queue, &funds, 0);
            esp_mqtt_client_enqueue(client, topic_confirm, "ok", 0, 1, 0, 1);
        } else if (strcmp(cmd, "oos") == 0) {
            out_of_sequence_todo = true;
            esp_mqtt_client_enqueue(client, topic_confirm, "ok", 0, 1, 0, 1);
        } else if (strcmp(cmd, "echo") == 0) {
            char topic[64], buf[24];
            snprintf(topic, sizeof(topic), "domain.vmflow.xyz/%s/rpc/echo", my_subdomain);
            snprintf(buf, sizeof(buf), "%u", ts);
            esp_mqtt_client_enqueue(client, topic, buf, 0, 0, 0, 1);
        } else if (strcmp(cmd, "restart") == 0) {
            esp_mqtt_client_publish(client, topic_confirm, "ok", 0, 1, 0);
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
        } else if (strcmp(cmd, "ota") == 0) {
            static char ota_url[160];
            if (has_args)
                snprintf(ota_url, sizeof(ota_url),
                    "https://github.com/nodestark/mdb-esp32-master/releases/download/%s/mdb-master-esp32s3.bin",
                    args);
            else
                snprintf(ota_url, sizeof(ota_url),
                    "https://github.com/nodestark/mdb-esp32-master/releases/latest/download/mdb-master-esp32s3.bin");
            esp_mqtt_client_enqueue(client, topic_confirm, "ok", 0, 1, 0, 1);
            xTaskCreate(ota_task, "ota_task", 8192, ota_url, 5, NULL);
        }
        break;
    }
    case MQTT_EVENT_ERROR:
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
            ESP_LOGE(TAG, "MQTT TCP error: errno=%d",
                     event->error_handle->esp_transport_sock_errno);
        break;
    default:
        break;
    }
}

//------------------------------------------------------------------------//
// mdb_controller_task — drives coin changer (0x08) and bill validator (0x30)
//------------------------------------------------------------------------//

void mdb_controller_task(void *pvParameters) {
    uint8_t tx[36], rx[36];
    size_t  len;
    const uint8_t await = 125;

    for (;;) {
        uart_flush(UART_NUM_2);

        //------------------------------------------------------------------//
        // 0x08 Coin Changer
        //------------------------------------------------------------------//
        if (phys_changer.state == INACTIVE_STATE) {

            tx[0] = (ADDR_CHANGER & BIT_ADD_SET) | (CHGR_RESET & BIT_CMD_SET);
            write_payload_controller_9(tx, 1);
            len = uart_read_bytes(UART_NUM_2, rx, 1, pdMS_TO_TICKS(await));

            if (len == 1) {
                tx[0] = (ADDR_CHANGER & BIT_ADD_SET) | (CHGR_POLL & BIT_CMD_SET);
                write_payload_controller_9(tx, 1);
                len = uart_read_bytes(UART_NUM_2, rx, 2, pdMS_TO_TICKS(await));
                if (len == 2 && rx[0] == 0x0B) {
                    write_controller_9(ACK | BIT_MODE_SET);
                    phys_changer.state = DISABLED_STATE;
                }
            }

        } else if (phys_changer.state == DISABLED_STATE) {

            tx[0] = (ADDR_CHANGER & BIT_ADD_SET) | (CHGR_SETUP & BIT_CMD_SET);
            write_payload_controller_9(tx, 1);
            len = uart_read_bytes(UART_NUM_2, rx, 24, pdMS_TO_TICKS(await));

            if (len == 24) {
                uart_flush_input(UART_NUM_2);
                write_controller_9(ACK | BIT_MODE_SET);
                phys_changer.feature_level     = rx[0];
                phys_changer.country_code      = ((uint16_t)rx[1] << 8) | rx[2];
                phys_changer.scale_factor      = rx[3];
                phys_changer.decimal_places    = rx[4];
                phys_changer.coin_type_routing = ((uint16_t)rx[5] << 8) | rx[6];
                for (uint8_t i = 0; i < 16; i++) phys_changer.coin_credit[i] = rx[7 + i];
                vTaskDelay(pdMS_TO_TICKS(5));

                tx[0] = (ADDR_CHANGER & BIT_ADD_SET) | (CHGR_TUBE_STATUS & BIT_CMD_SET);
                write_payload_controller_9(tx, 1);
                len = uart_read_bytes(UART_NUM_2, rx, 19, pdMS_TO_TICKS(await));
                if (len == 19) {
                    uart_flush_input(UART_NUM_2);
                    write_controller_9(ACK | BIT_MODE_SET);
                    phys_changer.tube_full_status = ((uint16_t)rx[0] << 8) | rx[1];
                    for (uint8_t i = 0; i < 16; i++) phys_changer.tube_counts[i] = rx[2 + i];
                }
                vTaskDelay(pdMS_TO_TICKS(5));

                tx[0] = (ADDR_CHANGER & BIT_ADD_SET) | (CHGR_COIN_TYPE & BIT_CMD_SET);
                tx[1] = 0xFF; tx[2] = 0xFF; tx[3] = 0x00; tx[4] = 0x00;
                write_payload_controller_9(tx, 5);
                len = uart_read_bytes(UART_NUM_2, rx, 5, pdMS_TO_TICKS(await));
                if (len == 5) write_controller_9(ACK | BIT_MODE_SET);

                phys_changer.state = ENABLED_STATE;
                ESP_LOGI(TAG, "Changer Enabled: scale=%d dec=%d routing=0x%04X",
                         phys_changer.scale_factor, phys_changer.decimal_places,
                         phys_changer.coin_type_routing);
            }

        } else {

            coin_from_target_evt_t req;
            if (xQueueReceive(coin_from_target_queue, &req, 0)) {
                if (req.type == COIN_REQ_TYPE_ENABLE) {
                    tx[0] = (ADDR_CHANGER & BIT_ADD_SET) | (CHGR_COIN_TYPE & BIT_CMD_SET);
                    tx[1] = req.coin_enable >> 8;     tx[2] = req.coin_enable;
                    tx[3] = req.dispense_enable >> 8; tx[4] = req.dispense_enable;
                    write_payload_controller_9(tx, 5);
                    uart_read_bytes(UART_NUM_2, rx, 5, pdMS_TO_TICKS(await));
                } else if (req.type == COIN_REQ_DISPENSE) {
                    tx[0] = (ADDR_CHANGER & BIT_ADD_SET) | (CHGR_DISPENSE & BIT_CMD_SET);
                    tx[1] = req.dispense_value;
                    write_payload_controller_9(tx, 2);
                    uart_read_bytes(UART_NUM_2, rx, 1, pdMS_TO_TICKS(await));
                }
                uart_flush(UART_NUM_2);
            }

            tx[0] = (ADDR_CHANGER & BIT_ADD_SET) | (CHGR_POLL & BIT_CMD_SET);
            write_payload_controller_9(tx, 1);
            len = uart_read_bytes(UART_NUM_2, rx, 17, pdMS_TO_TICKS(await));

            if (len == 1) {
                phys_changer.poll_fail_count = 0;
            } else if (len > 1) {
                phys_changer.poll_fail_count = 0;
                write_controller_9(ACK | BIT_MODE_SET);

                if (len == 2 && rx[0] == 0x0B) {
                    phys_changer.state = INACTIVE_STATE;
                } else {
                    uint8_t dep_types[16], dep_routing[16], dep_count = 0;
                    for (uint8_t i = 0; i + 1 < len; i++) {
                        uint8_t ev = rx[i];
                        if ((ev & 0xC0) == 0x40) {
                            if (dep_count < 16) {
                                dep_types[dep_count]   = ev & 0x0F;
                                dep_routing[dep_count] = (ev >> 4) & 0x03;
                                dep_count++;
                            }
                            i++;
                        }
                    }
                    if (dep_count > 0) {
                        tx[0] = (ADDR_CHANGER & BIT_ADD_SET) | (CHGR_TUBE_STATUS & BIT_CMD_SET);
                        write_payload_controller_9(tx, 1);
                        size_t tl = uart_read_bytes(UART_NUM_2, rx, 19, pdMS_TO_TICKS(await));
                        if (tl == 19) {
                            write_controller_9(ACK | BIT_MODE_SET);
                            phys_changer.tube_full_status = ((uint16_t)rx[0] << 8) | rx[1];
                            for (uint8_t i = 0; i < 16; i++) phys_changer.tube_counts[i] = rx[2 + i];
                        }
                        for (uint8_t i = 0; i < dep_count; i++) {
                            coin_deposit_evt_t evt = {
                                dep_types[i],
                                phys_changer.tube_counts[dep_types[i]],
                                dep_routing[i],
                            };
                            xQueueSend(coin_to_target_queue, &evt, 0);
                        }
                    }
                }
            } else {
                if (++phys_changer.poll_fail_count >= 10) {
                    phys_changer.state = INACTIVE_STATE;
                    ESP_LOGW(TAG, "Changer: poll timeout");
                }
            }
        }

        uart_flush(UART_NUM_2);

        //------------------------------------------------------------------//
        // 0x30 Bill Validator
        //------------------------------------------------------------------//
        if (phys_validator.state == INACTIVE_STATE) {

            tx[0] = (ADDR_VALIDATOR & BIT_ADD_SET) | (VLD_RESET & BIT_CMD_SET);
            write_payload_controller_9(tx, 1);
            len = uart_read_bytes(UART_NUM_2, rx, 1, pdMS_TO_TICKS(await));

            if (len == 1) {
                tx[0] = (ADDR_VALIDATOR & BIT_ADD_SET) | (VLD_POLL & BIT_CMD_SET);
                write_payload_controller_9(tx, 1);
                len = uart_read_bytes(UART_NUM_2, rx, 2, pdMS_TO_TICKS(await));
                if (len == 2 && rx[0] == 0x06) {
                    write_controller_9(ACK | BIT_MODE_SET);
                    phys_validator.state = DISABLED_STATE;
                }
            }

        } else if (phys_validator.state == DISABLED_STATE) {

            tx[0] = (ADDR_VALIDATOR & BIT_ADD_SET) | (VLD_SETUP & BIT_CMD_SET);
            write_payload_controller_9(tx, 1);
            len = uart_read_bytes(UART_NUM_2, rx, 28, pdMS_TO_TICKS(await));

            if (len == 28) {
                uart_flush_input(UART_NUM_2);
                write_controller_9(ACK | BIT_MODE_SET);
                phys_validator.feature_level         = rx[0];
                phys_validator.country_code          = ((uint16_t)rx[1] << 8) | rx[2];
                phys_validator.scale_factor          = ((uint16_t)rx[3] << 8) | rx[4];
                phys_validator.decimal_places        = rx[5];
                phys_validator.bill_stacker_capacity = ((uint16_t)rx[6] << 8) | rx[7];
                for (uint8_t i = 0; i < 16; i++) phys_validator.bill_credit[i] = rx[11 + i];

                tx[0] = (ADDR_VALIDATOR & BIT_ADD_SET) | (VLD_BILL_TYPE & BIT_CMD_SET);
                tx[1] = 0xFF; tx[2] = 0xFF; tx[3] = 0xFF; tx[4] = 0xFF;
                write_payload_controller_9(tx, 5);
                len = uart_read_bytes(UART_NUM_2, rx, 1, pdMS_TO_TICKS(await));

                if (len == 1) {
                    phys_validator.state = ENABLED_STATE;
                    ESP_LOGI(TAG, "Validator Enabled: scale=%d dec=%d capacity=%d",
                             phys_validator.scale_factor, phys_validator.decimal_places,
                             phys_validator.bill_stacker_capacity);

                    tx[0] = (ADDR_VALIDATOR & BIT_ADD_SET) | (VLD_STACKER & BIT_CMD_SET);
                    write_payload_controller_9(tx, 1);
                    size_t sl = uart_read_bytes(UART_NUM_2, rx, 3, pdMS_TO_TICKS(await));
                    if (sl == 3) {
                        write_controller_9(ACK | BIT_MODE_SET);
                        uint16_t val = ((uint16_t)rx[0] << 8) | rx[1];
                        phys_validator.stacker_full  = (val & 0x8000) != 0;
                        phys_validator.stacker_count = val & 0x7FFF;
                    }
                }
            }

        } else {

            bill_from_target_evt_t req;
            if (xQueueReceive(bill_from_target_queue, &req, 0)) {
                if (req.type == BILL_REQ_TYPE_ENABLE) {
                    tx[0] = (ADDR_VALIDATOR & BIT_ADD_SET) | (VLD_BILL_TYPE & BIT_CMD_SET);
                    tx[1] = req.bill_enable >> 8;        tx[2] = req.bill_enable;
                    tx[3] = req.bill_escrow_enable >> 8; tx[4] = req.bill_escrow_enable;
                    write_payload_controller_9(tx, 5);
                    uart_read_bytes(UART_NUM_2, rx, 1, pdMS_TO_TICKS(await));
                } else if (req.type == BILL_REQ_ESCROW) {
                    tx[0] = (ADDR_VALIDATOR & BIT_ADD_SET) | (VLD_ESCROW & BIT_CMD_SET);
                    tx[1] = req.escrow_command;
                    write_payload_controller_9(tx, 2);
                    uart_read_bytes(UART_NUM_2, rx, 1, pdMS_TO_TICKS(await));
                }
                uart_flush(UART_NUM_2);
            }

            tx[0] = (ADDR_VALIDATOR & BIT_ADD_SET) | (VLD_POLL & BIT_CMD_SET);
            write_payload_controller_9(tx, 1);
            len = uart_read_bytes(UART_NUM_2, rx, 17, pdMS_TO_TICKS(await));

            if (len == 1) {
                phys_validator.poll_fail_count = 0;
            } else if (len > 1) {
                phys_validator.poll_fail_count = 0;
                write_controller_9(ACK | BIT_MODE_SET);

                if (len == 2 && rx[0] == 0x06) {
                    phys_validator.state = INACTIVE_STATE;
                } else {
                    bool bill_stacked = false;
                    for (uint8_t i = 0; i + 1 < len; i++) {
                        uint8_t ev = rx[i];
                        if ((ev & 0x80) && !(ev & 0x40)) {
                            uint8_t bt = ev & 0x0F;
                            bill_stacked = true;
                            // Track vault value: MDB never reports a stacked
                            // bill's denomination, so accumulate it here.
                            phys_validator.stacker_value += phys_validator.bill_credit[bt];
                            bill_stack_evt_t evt = { bt };
                            xQueueSend(bill_to_target_queue, &evt, 0);
                        } else if ((ev & 0x90) == 0x90) {
                            tx[0] = (ADDR_VALIDATOR & BIT_ADD_SET) | (VLD_ESCROW & BIT_CMD_SET);
                            tx[1] = 0x01;
                            write_payload_controller_9(tx, 2);
                            uart_read_bytes(UART_NUM_2, rx, 1, pdMS_TO_TICKS(await));
                        }
                    }
                    if (bill_stacked) {
                        tx[0] = (ADDR_VALIDATOR & BIT_ADD_SET) | (VLD_STACKER & BIT_CMD_SET);
                        write_payload_controller_9(tx, 1);
                        size_t sl = uart_read_bytes(UART_NUM_2, rx, 3, pdMS_TO_TICKS(await));
                        if (sl == 3) {
                            write_controller_9(ACK | BIT_MODE_SET);
                            uint16_t val = ((uint16_t)rx[0] << 8) | rx[1];
                            phys_validator.stacker_full  = (val & 0x8000) != 0;
                            phys_validator.stacker_count = val & 0x7FFF;
                        }
                    }
                }
            } else {
                if (++phys_validator.poll_fail_count >= 10) {
                    phys_validator.state = INACTIVE_STATE;
                    ESP_LOGW(TAG, "Validator: poll timeout");
                }
            }
        }
    }
}

//------------------------------------------------------------------------//
// mdb_target_task — emulates 0x10/0x08/0x30 toward VMC, always online.
//
// Cashless business rule (self-contained — no external cashless is bridged):
//  1. Credit is granted out-of-band (BLE phone app or MQTT `credit` RPC) and
//     queued in mdb_session_queue.
//  2. A session may only Begin while the reader is ENABLED by the VMC; the
//     queued amount becomes the session's available funds.
//  3. A vend is approved locally iff item_price <= funds (0xFFFF = unlimited
//     BLE session, deferred to a phone-signed approve). Funds are debited on
//     Vend Success.
//  4. Credit is bound to a single session: it is destroyed on Session End and
//     on Reset, so no balance ever carries over or approves an out-of-session
//     vend.
//------------------------------------------------------------------------//

#define COIN_POLL_QSIZE 32
#define BILL_POLL_QSIZE 16

void mdb_target_task(void *pvParameters) {
    gpio_install_isr_service(0);
    gpio_isr_handler_add(PIN_TARGET_RX, mdb_rx_falling_isr, NULL);

    uint8_t payload[36];

    // ---- Cashless (0x10) ----
    bool     cshl_reset_todo       = false;
    int64_t  cshl_session_start_us = 0;
    uint16_t cshl_funds_available  = 0;
    uint16_t cshl_item_price       = 0;
    uint16_t cshl_item_number      = 0;

    // ---- Coin changer (0x08) ----
    bool     coin_reset_todo      = false;
    uint16_t coin_enable          = 0x0000;
    uint16_t coin_dispense_enable = 0x0000;
    uint8_t  coin_poll_q[COIN_POLL_QSIZE];
    uint8_t  coin_poll_q_head = 0, coin_poll_q_tail = 0;

    // ---- Bill validator (0x30) ----
    bool     bill_reset_todo      = false;
    uint16_t bill_enable          = 0x0000;
    uint16_t bill_escrow_enable   = 0x0000;
    uint8_t  bill_poll_q[BILL_POLL_QSIZE];
    uint8_t  bill_poll_q_head = 0, bill_poll_q_tail = 0;

    for (;;) {
        uint8_t checksum     = 0x00;
        uint8_t available_tx = 0;

        uint16_t incoming = read_9(&checksum);
        if (!(incoming & BIT_MODE_SET)) continue;
        if ((uint8_t)incoming == ACK)   continue;
        if ((uint8_t)incoming == RET)   continue;
        if ((uint8_t)incoming == NAK)   continue;

        uint8_t addr = incoming & BIT_ADD_SET;
        uint8_t cmd  = incoming & BIT_CMD_SET;

        //================================================================//
        // 0x10 Cashless — self-contained, always online
        //================================================================//
        if (addr == ADDR_CASHLESS) {
            switch (cmd) {
            case CSHL_RESET: {
                if (read_9(NULL) != checksum) continue;
                cshl_reset_todo      = true;
                target_cshl_state    = INACTIVE_STATE;
                cshl_funds_available = 0;   // credit never survives a reset
                xEventGroupClearBits(xLedEventGroup, BIT_STATUS_MDB);
                xEventGroupSetBits(xLedEventGroup, BIT_STATUS_TRIGGER);
                break;
            }
            case CSHL_SETUP: {
                switch (read_9(&checksum)) {
                case 0x00: { // Config Data
                    read_9(&checksum); read_9(&checksum);
                    read_9(&checksum); read_9(&checksum);
                    if (read_9(NULL) != checksum) continue;
                    payload[0] = 0x01;
                    payload[1] = 1;
                    payload[2] = CONFIG_MDB_CURRENCY_CODE >> 8;
                    payload[3] = CONFIG_MDB_CURRENCY_CODE & 0xFF;
                    payload[4] = CONFIG_MDB_SCALE_FACTOR;
                    payload[5] = CONFIG_MDB_DECIMAL_PLACES;
                    payload[6] = 3;
                    payload[7] = 0b00001001;
                    available_tx = 8;
                    target_cshl_state = DISABLED_STATE;
                    break;
                }
                case 0x01: // Max/Min Prices
                    read_9(&checksum); read_9(&checksum);
                    read_9(&checksum); read_9(&checksum);
                    if (read_9(NULL) != checksum) continue;
                    break;
                }
                break;
            }
            case CSHL_POLL: {
                if (read_9(NULL) != checksum) continue;
                if (cshl_reset_todo) {
                    cshl_reset_todo   = false;
                    payload[0]        = 0x00;
                    available_tx      = 1;
                    target_cshl_state = DISABLED_STATE;
                } else if (target_cshl_state == ENABLED_STATE) {
                    // Only begin a session while the reader is ENABLED by the
                    // VMC. Pulling credit in any other state sends an
                    // out-of-sequence Begin Session and loses the payment.
                    uint16_t funds;
                    if (xQueueReceive(mdb_session_queue, &funds, 0)) {
                        cshl_funds_available  = funds;
                        target_cshl_state     = IDLE_STATE;
                        cshl_session_start_us = esp_timer_get_time();
                        payload[0] = 0x03;
                        payload[1] = funds >> 8;
                        payload[2] = funds;
                        available_tx = 3;
                        xEventGroupSetBits(xLedEventGroup, BIT_STATUS_MDB | BIT_STATUS_TRIGGER);
                    }
                } else if (session_cancel_todo) {
                    session_cancel_todo = false;
                    payload[0] = 0x04;
                    available_tx = 1;
                } else if (vend_approved_todo) {
                    vend_approved_todo = false;
                    payload[0] = 0x05;
                    payload[1] = cshl_item_price >> 8;
                    payload[2] = cshl_item_price;
                    available_tx = 3;
                } else if (vend_denied_todo) {
                    vend_denied_todo  = false;
                    target_cshl_state = IDLE_STATE;
                    payload[0] = 0x06;
                    available_tx = 1;
                } else if (session_end_todo) {
                    session_end_todo     = false;
                    target_cshl_state    = ENABLED_STATE;
                    cshl_funds_available = 0;   // no credit carries into the next session
                    payload[0] = 0x07;
                    available_tx = 1;
                    xEventGroupClearBits(xLedEventGroup, BIT_STATUS_MDB);
                    xEventGroupSetBits(xLedEventGroup, BIT_STATUS_TRIGGER);
                } else if (out_of_sequence_todo) {
                    out_of_sequence_todo = false;
                    payload[0] = 0x0B;
                    available_tx = 1;
                } else if (target_cshl_state >= IDLE_STATE) {
                    if (esp_timer_get_time() - cshl_session_start_us > 60LL * 1000000LL)
                        session_cancel_todo = true;
                }
                break;
            }
            case CSHL_VEND: {
                switch (read_9(&checksum)) {
                case 0x00: { // Vend Request
                    cshl_item_price  = ((uint16_t)read_9(&checksum) << 8) | read_9(&checksum);
                    cshl_item_number = ((uint16_t)read_9(&checksum) << 8) | read_9(&checksum);
                    if (read_9(NULL) != checksum) continue;
                    target_cshl_state = VEND_STATE;
                    // Auto approve/deny based on available funds. 0xFFFF is an
                    // unlimited (BLE) session: defer the decision to the phone
                    // (BLE cmd 0x03). Any other amount decides locally.
                    if (cshl_funds_available != 0xFFFF) {
                        if (cshl_item_price <= cshl_funds_available)
                            vend_approved_todo = true;
                        else
                            vend_denied_todo = true;
                    }
                    uint8_t b[19];
                    ble_encode_with_passkey(0x0A, cshl_item_price, cshl_item_number, b);
                    ble_notify_send((char *)b, sizeof(b));
                    ESP_LOGI(TAG, "Target: Vend Request price=%u item=%u",
                             cshl_item_price, cshl_item_number);
                    break;
                }
                case 0x01: // Vend Cancel
                    if (read_9(NULL) != checksum) continue;
                    vend_denied_todo = true;
                    break;
                case 0x02: { // Vend Success
                    cshl_item_number = ((uint16_t)read_9(&checksum) << 8) | read_9(&checksum);
                    if (read_9(NULL) != checksum) continue;
                    target_cshl_state = IDLE_STATE;
                    // Deduct the vended price so multi-vend sessions keep an
                    // accurate balance (unlimited 0xFFFF sessions stay open).
                    if (cshl_funds_available != 0xFFFF &&
                        cshl_item_price <= cshl_funds_available)
                        cshl_funds_available -= cshl_item_price;
                    uint8_t b[19];
                    ble_encode_with_passkey(0x0B, cshl_item_price, cshl_item_number, b);
                    ble_notify_send((char *)b, sizeof(b));
                    publish_signed_event("vend_ok", cshl_item_price, cshl_item_number);
                    ESP_LOGI(TAG, "Target: Vend Success item=%u", cshl_item_number);
                    break;
                }
                case 0x03: { // Vend Failure
                    if (read_9(NULL) != checksum) continue;
                    target_cshl_state = IDLE_STATE;
                    uint8_t b[19];
                    ble_encode_with_passkey(0x0C, cshl_item_price, cshl_item_number, b);
                    ble_notify_send((char *)b, sizeof(b));
                    publish_signed_event("vend_fail", cshl_item_price, cshl_item_number);
                    break;
                }
                case 0x04: { // Session Complete
                    if (read_9(NULL) != checksum) continue;
                    session_end_todo = true;
                    uint8_t b[19];
                    ble_encode_with_passkey(0x0D, cshl_item_price, cshl_item_number, b);
                    ble_notify_send((char *)b, sizeof(b));
                    break;
                }
                case 0x05: { // Cash Sale → MQTT only
                    uint16_t sp = ((uint16_t)read_9(&checksum) << 8) | read_9(&checksum);
                    uint16_t si = ((uint16_t)read_9(&checksum) << 8) | read_9(&checksum);
                    if (read_9(NULL) != checksum) continue;
                    publish_signed_event("sale", sp, si);
                    break;
                }
                }
                break;
            }
            case CSHL_READER: {
                switch (read_9(&checksum)) {
                case 0x00: // Reader Disable
                    if (read_9(NULL) != checksum) continue;
                    target_cshl_state = DISABLED_STATE;
                    xEventGroupClearBits(xLedEventGroup, BIT_STATUS_MDB);
                    xEventGroupSetBits(xLedEventGroup, BIT_STATUS_TRIGGER);
                    break;
                case 0x01: // Reader Enable
                    if (read_9(NULL) != checksum) continue;
                    target_cshl_state = ENABLED_STATE;
                    xEventGroupSetBits(xLedEventGroup, BIT_STATUS_MDB | BIT_STATUS_TRIGGER);
                    break;
                case 0x02: // Reader Cancel
                    if (read_9(NULL) != checksum) continue;
                    payload[0] = 0x08;
                    available_tx = 1;
                    break;
                }
                break;
            }
            case CSHL_EXPANSION: {
                uint8_t sub = (uint8_t)read_9(&checksum);
                if (sub == 0x00) {
                    for (uint8_t x = 0; x < 29; x++) read_9(&checksum);
                    if (read_9(NULL) != checksum) continue;
                    payload[0] = 0x09;
                    memcpy(&payload[1], "VMF", 3);
                    memset(&payload[4], ' ', 12);
                    memset(&payload[16], ' ', 12);
                    payload[28] = 0x00; payload[29] = 0x03;
                    available_tx = 30;
                }
                break;
            }
            default: continue;
            }

        //================================================================//
        // 0x08 Coin Changer — emulated, always online
        //================================================================//
        } else if (addr == ADDR_CHANGER) {
            switch (cmd) {
            case CHGR_RESET: {
                if (read_9(NULL) != checksum) continue;
                coin_reset_todo      = true;
                coin_enable          = 0x0000;
                coin_dispense_enable = 0x0000;
                break;
            }
            case CHGR_SETUP: {
                if (read_9(NULL) != checksum) continue;
                bool phys = (phys_changer.state != INACTIVE_STATE);
                // Report the real changer's country/currency code (see validator).
                uint16_t chg_cc = phys ? phys_changer.country_code : CONFIG_MDB_CURRENCY_CODE;
                payload[0] = phys ? phys_changer.feature_level : CHANGER_DEFAULT_FEATURE_LEVEL;
                payload[1] = chg_cc >> 8; payload[2] = chg_cc & 0xFF;
                payload[3] = phys ? phys_changer.scale_factor : 1;
                payload[4] = phys ? phys_changer.decimal_places : 2;
                payload[5] = phys ? (phys_changer.coin_type_routing >> 8)  : (CHANGER_DEFAULT_COIN_ROUTING >> 8);
                payload[6] = phys ? (phys_changer.coin_type_routing & 0xFF) : (CHANGER_DEFAULT_COIN_ROUTING & 0xFF);
                for (int i = 0; i < 16; i++)
                    payload[7 + i] = phys ? phys_changer.coin_credit[i] : CHANGER_DEFAULT_COIN_CREDIT[i];
                available_tx = 23;
                break;
            }
            case CHGR_TUBE_STATUS: {
                if (read_9(NULL) != checksum) continue;
                bool phys = (phys_changer.state != INACTIVE_STATE);
                payload[0] = phys ? (phys_changer.tube_full_status >> 8)  : 0;
                payload[1] = phys ? (phys_changer.tube_full_status & 0xFF) : 0;
                for (int i = 0; i < 16; i++)
                    payload[2 + i] = phys ? phys_changer.tube_counts[i] : 0;
                available_tx = 18;
                break;
            }
            case CHGR_POLL: {
                if (read_9(NULL) != checksum) continue;
                coin_deposit_evt_t dep;
                while (xQueueReceive(coin_to_target_queue, &dep, 0)) {
                    uint8_t next = (coin_poll_q_head + 2) % COIN_POLL_QSIZE;
                    if (next != coin_poll_q_tail) {
                        coin_poll_q[coin_poll_q_head] = 0x40 | (dep.routing << 4) | dep.coin_type;
                        coin_poll_q_head = (coin_poll_q_head + 1) % COIN_POLL_QSIZE;
                        coin_poll_q[coin_poll_q_head] = dep.tube_count;
                        coin_poll_q_head = (coin_poll_q_head + 1) % COIN_POLL_QSIZE;
                    }
                }
                if (coin_reset_todo) {
                    coin_reset_todo = false;
                    payload[0]   = 0x0B;
                    available_tx = 1;
                } else {
                    while (coin_poll_q_head != coin_poll_q_tail && available_tx < 16) {
                        payload[available_tx++] = coin_poll_q[coin_poll_q_tail];
                        coin_poll_q_tail = (coin_poll_q_tail + 1) % COIN_POLL_QSIZE;
                    }
                }
                break;
            }
            case CHGR_COIN_TYPE: {
                uint8_t en_h = (uint8_t)read_9(&checksum), en_l = (uint8_t)read_9(&checksum);
                uint8_t di_h = (uint8_t)read_9(&checksum), di_l = (uint8_t)read_9(&checksum);
                if (read_9(NULL) != checksum) continue;
                coin_enable          = ((uint16_t)en_h << 8) | en_l;
                coin_dispense_enable = ((uint16_t)di_h << 8) | di_l;
                coin_from_target_evt_t req = { COIN_REQ_TYPE_ENABLE, coin_enable, coin_dispense_enable, 0 };
                xQueueSend(coin_from_target_queue, &req, 0);
                break;
            }
            case CHGR_DISPENSE: {
                uint8_t val = (uint8_t)read_9(&checksum);
                if (read_9(NULL) != checksum) continue;
                coin_from_target_evt_t req = { COIN_REQ_DISPENSE, 0, 0, val };
                xQueueSend(coin_from_target_queue, &req, 0);
                break;
            }
            case CHGR_EXPANSION: {
                uint8_t sub = (uint8_t)read_9(&checksum);
                if (sub == 0x00) {
                    if (read_9(NULL) != checksum) continue;
                    memcpy(&payload[0], "MDB", 3); memset(&payload[3], ' ', 12);
                    memset(&payload[15], ' ', 12);
                    payload[27] = 0x00; payload[28] = 0x01;
                    payload[29] = payload[30] = payload[31] = payload[32] = 0x00;
                    available_tx = 33;
                } else if (sub == 0x01) {
                    read_9(&checksum); read_9(&checksum);
                    read_9(&checksum); read_9(&checksum);
                    if (read_9(NULL) != checksum) continue;
                }
                break;
            }
            default: continue;
            }

        //================================================================//
        // 0x30 Bill Validator — emulated, always online
        //================================================================//
        } else if (addr == ADDR_VALIDATOR) {
            switch (cmd) {
            case VLD_RESET: {
                if (read_9(NULL) != checksum) continue;
                bill_reset_todo    = true;
                bill_enable        = 0x0000;
                bill_escrow_enable = 0x0000;
                break;
            }
            case VLD_SETUP: {
                if (read_9(NULL) != checksum) continue;
                bool phys = (phys_validator.state != INACTIVE_STATE);
                // Report the real validator's country/currency code so the VMC
                // accepts it. Hardcoding 0xFFFF (unknown) makes many machines
                // display "NOTE INCOMPAT". Fall back to the configured currency
                // only while no physical device is bridged.
                uint16_t vld_cc = phys ? phys_validator.country_code : CONFIG_MDB_CURRENCY_CODE;
                payload[0]  = phys ? phys_validator.feature_level : 0x01;
                payload[1]  = vld_cc >> 8; payload[2] = vld_cc & 0xFF;
                payload[3]  = phys ? (phys_validator.scale_factor >> 8)  : (VALIDATOR_DEFAULT_SCALE_FACTOR >> 8);
                payload[4]  = phys ? (phys_validator.scale_factor & 0xFF) : (VALIDATOR_DEFAULT_SCALE_FACTOR & 0xFF);
                payload[5]  = phys ? phys_validator.decimal_places : 2;
                payload[6]  = phys ? (phys_validator.bill_stacker_capacity >> 8)  : 0;
                payload[7]  = phys ? (phys_validator.bill_stacker_capacity & 0xFF) : 0;
                payload[8]  = 0x00; payload[9] = 0x00; payload[10] = 0x00;
                for (int i = 0; i < 16; i++)
                    payload[11 + i] = phys ? phys_validator.bill_credit[i] : VALIDATOR_DEFAULT_BILL_CREDIT[i];
                available_tx = 27;
                break;
            }
            case VLD_SECURITY:
                read_9(&checksum); read_9(&checksum);
                if (read_9(NULL) != checksum) continue;
                break;
            case VLD_POLL: {
                if (read_9(NULL) != checksum) continue;
                bill_stack_evt_t stk;
                while (xQueueReceive(bill_to_target_queue, &stk, 0)) {
                    uint8_t next = (bill_poll_q_head + 1) % BILL_POLL_QSIZE;
                    if (next != bill_poll_q_tail) {
                        bill_poll_q[bill_poll_q_head] = 0x80 | stk.bill_type;
                        bill_poll_q_head = next;
                    }
                }
                if (bill_reset_todo) {
                    bill_reset_todo = false;
                    payload[0]   = 0x06;
                    available_tx = 1;
                } else {
                    while (bill_poll_q_head != bill_poll_q_tail && available_tx < 16) {
                        payload[available_tx++] = bill_poll_q[bill_poll_q_tail];
                        bill_poll_q_tail = (bill_poll_q_tail + 1) % BILL_POLL_QSIZE;
                    }
                }
                break;
            }
            case VLD_BILL_TYPE: {
                uint8_t en_h  = (uint8_t)read_9(&checksum), en_l  = (uint8_t)read_9(&checksum);
                uint8_t esc_h = (uint8_t)read_9(&checksum), esc_l = (uint8_t)read_9(&checksum);
                if (read_9(NULL) != checksum) continue;
                bill_enable        = ((uint16_t)en_h  << 8) | en_l;
                bill_escrow_enable = ((uint16_t)esc_h << 8) | esc_l;
                bill_from_target_evt_t req = { BILL_REQ_TYPE_ENABLE, bill_enable, bill_escrow_enable, 0 };
                xQueueSend(bill_from_target_queue, &req, 0);
                break;
            }
            case VLD_ESCROW: {
                uint8_t esc = (uint8_t)read_9(&checksum);
                if (read_9(NULL) != checksum) continue;
                bill_from_target_evt_t req = { BILL_REQ_ESCROW, 0, 0, esc };
                xQueueSend(bill_from_target_queue, &req, 0);
                break;
            }
            case VLD_STACKER: {
                if (read_9(NULL) != checksum) continue;
                bool phys = (phys_validator.state != INACTIVE_STATE);
                uint16_t val = phys
                    ? ((phys_validator.stacker_full ? 0x8000 : 0) | (phys_validator.stacker_count & 0x7FFF))
                    : 0;
                payload[0] = val >> 8; payload[1] = val & 0xFF;
                available_tx = 2;
                break;
            }
            case VLD_EXPANSION: {
                uint8_t sub = (uint8_t)read_9(&checksum);
                if (sub == 0x00) {
                    if (read_9(NULL) != checksum) continue;
                    memcpy(&payload[0], "VMF", 3); memset(&payload[3], ' ', 12);
                    memset(&payload[15], ' ', 12);
                    payload[27] = 0x00; payload[28] = 0x01;
                    available_tx = 29;
                }
                break;
            }
            default: continue;
            }
        } else {
            continue;
        }

        write_payload_9(payload, available_tx);
    }
}

//------------------------------------------------------------------------//
// app_main
//------------------------------------------------------------------------//

void app_main(void) {
    //---- Queues ----
    coin_to_target_queue   = xQueueCreate(16, sizeof(coin_deposit_evt_t));
    coin_from_target_queue = xQueueCreate(4,  sizeof(coin_from_target_evt_t));
    bill_to_target_queue   = xQueueCreate(16, sizeof(bill_stack_evt_t));
    bill_from_target_queue = xQueueCreate(4,  sizeof(bill_from_target_evt_t));
    mdb_session_queue      = xQueueCreate(1,  sizeof(uint16_t));
    mdb_rx_queue           = xQueueCreate(64, sizeof(uint16_t));

    xLedEventGroup = xEventGroupCreate();

    //---- LED WS2812 ----
    led_strip_config_t strip_config = {
        .strip_gpio_num = PIN_MDB_LED,
        .max_leds = 1,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10000000,
        .mem_block_symbols = 64,
    };
    led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
    xTaskCreate(led_status_task, "led_status_task", 2048, NULL, 1, NULL);
    xEventGroupSetBits(xLedEventGroup, BIT_STATUS_TRIGGER);

    //---- NVS ----
    nvs_flash_init();

    char myhost[64];
    strcpy(myhost, "0.vmflow.xyz");

    nvs_handle_t h;
    if (nvs_open("vmflow", NVS_READONLY, &h) == ESP_OK) {
        size_t s_len = 0;
        if (nvs_get_str(h, "passkey", NULL, &s_len) == ESP_OK) {
            nvs_get_str(h, "passkey", my_passkey, &s_len);
            if (nvs_get_str(h, "domain", NULL, &s_len) == ESP_OK) {
                nvs_get_str(h, "domain", my_subdomain, &s_len);
                snprintf(myhost, sizeof(myhost), "%s.vmflow.xyz", my_subdomain);
                xEventGroupSetBits(xLedEventGroup,
                    BIT_STATUS_PSSKEY | BIT_STATUS_DOMAIN | BIT_STATUS_TRIGGER);
            }
        }
        nvs_close(h);
    }

    rpc_auth_set_key(my_passkey);

    //---- Network stack ----
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT,   ESP_EVENT_ANY_ID, ip_event_handler,   NULL, NULL);

    const esp_timer_create_args_t wifi_timer_args = { .callback = wifi_retry_cb, .name = "wifi_retry" };
    esp_timer_create(&wifi_timer_args, &wifi_retry_timer);

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();

    //---- SNTP ----
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    //---- BLE — config provisioning + payment ----
    ble_init(myhost, ble_event_handler, ble_pax_event_handler);

    esp_timer_handle_t pax_timer;
    const esp_timer_create_args_t pax_args = {
        .callback = &ble_scan_start,
        .arg      = (void *)(uintptr_t)PAX_SCAN_DURATION_SEC,
        .name     = "paxcounter",
    };
    esp_timer_create(&pax_args, &pax_timer);
    esp_timer_start_periodic(pax_timer, PAX_SCAN_INTERVAL_US);

    //---- Controller UART2 ----
    uart_config_t uart_cfg = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_EVEN,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    uart_param_config(UART_NUM_2, &uart_cfg);
    uart_set_pin(UART_NUM_2, PIN_CONTROLLER_TX, PIN_CONTROLLER_RX, -1, -1);
    uart_driver_install(UART_NUM_2, 256, 256, 0, NULL, 0);

    //---- Target GPIO ----
    gpio_config_t rx_cfg = {
        .pin_bit_mask = 1ULL << PIN_TARGET_RX,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&rx_cfg);

    gpio_config_t tx_cfg = { .pin_bit_mask = 1ULL << PIN_TARGET_TX, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&tx_cfg);
    gpio_set_level(PIN_TARGET_TX, 1);

    xTaskCreatePinnedToCore(mdb_controller_task, "controller_task", 4096, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(mdb_target_task,     "target_task",     4096, NULL, 1, NULL, 1);

    //---- MQTT ----
    char lwt_topic[64];
    snprintf(lwt_topic, sizeof(lwt_topic), "domain.vmflow.xyz/%s/status", my_subdomain);

    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri              = "mqtt://mqtt.vmflow.xyz",
        .session.last_will.topic         = lwt_topic,
        .session.last_will.msg           = "offline",
        .session.last_will.qos           = 1,
        .session.last_will.retain        = 1,
        .session.keepalive               = 120,
        .network.timeout_ms              = 30000,
        .network.reconnect_timeout_ms    = 15000,
        .buffer.size                     = 2048,
        .buffer.out_size                 = 6144,
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}
