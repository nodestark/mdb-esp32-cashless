/*
 * VMflow.xyz
 *
 * mdb-slave-esp32s3.c - Vending machine peripheral
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
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
#include <esp_wifi.h>
#include <mqtt_client.h>
#include <esp_sntp.h>
#include <esp_netif_ppp.h>
#include <esp_modem_api.h>
#include <led_strip.h>

#include "nimble.h"
#include "eva-dts.h"
#include "rpc-auth.h"

#define TAG "mdb_cashless"

#define STRINGIFY_IMPL(x)   #x
#define STRINGIFY(x)        STRINGIFY_IMPL(x)

#define PIN_I2C_SDA         GPIO_NUM_10
#define PIN_I2C_SCL         GPIO_NUM_11
#define PIN_PULSE_1         GPIO_NUM_13
#define PIN_MDB_RX          GPIO_NUM_4
#define PIN_MDB_TX          GPIO_NUM_5
#define PIN_MDB_LED         GPIO_NUM_21
#define PIN_SIM7080G_RX     GPIO_NUM_18
#define PIN_SIM7080G_TX     GPIO_NUM_17
#define PIN_SIM7080G_PWR    GPIO_NUM_14
#define PIN_BUZZER_PWR      GPIO_NUM_12

#define TO_SCALE_FACTOR(p, scale_to, dec_to) (p / scale_to / pow(10, -(dec_to) ))
#define FROM_SCALE_FACTOR(p, scale_from, dec_from) (p * scale_from * pow(10, -(dec_from) ))

// Big-endian (de)serialization helpers for the BLE wire payload.
static inline uint32_t read_u32(const uint8_t *p) {
	return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}
static inline uint16_t read_u16(const uint8_t *p) {
	return ((uint16_t) p[0] << 8) | p[1];
}
static inline void write_u32(uint8_t *p, uint32_t v) {
	p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}
static inline void write_u16(uint8_t *p, uint16_t v) {
	p[0] = v >> 8; p[1] = v;
}

#define ACK 	0x00
#define RET 	0xAA
#define NAK 	0xFF

#define BIT_MODE_SET 	0b100000000
#define BIT_ADD_SET   	0b011111000
#define BIT_CMD_SET   	0b000000111

enum BIT_STATUS {
    BIT_STATUS_MQTT         = (1 << 0),
    BIT_STATUS_MDB          = (1 << 1),
    BIT_STATUS_PSSKEY       = (1 << 2),
    BIT_STATUS_DOMAIN       = (1 << 3),
    BIT_STATUS_BUZZER       = (1 << 4),
    BIT_STATUS_TRIGGER      = (1 << 5),
    MASK_STATUS_INSTALLED   = (BIT_STATUS_PSSKEY | BIT_STATUS_DOMAIN)
};

enum BIT_INTERNET {
    BIT_PPP_GOT_IP      = (1 << 0),
    BIT_PPP_LOST_IP     = (1 << 1),
    BIT_STA_GOT_IP      = (1 << 2),
    BIT_STA_LOST_IP     = (1 << 3),
};

EventGroupHandle_t xLedEventGroup;
EventGroupHandle_t xInternetEventGroup;

#define WIFI_BACKOFF_MIN_MS  5000
#define WIFI_BACKOFF_MAX_MS  300000
static uint32_t wifi_backoff_ms = WIFI_BACKOFF_MIN_MS;
static esp_timer_handle_t wifi_retry_timer;

static void wifi_retry_cb(void *arg) {
    esp_wifi_connect();
}

char my_subdomain[32];
#define PASSKEY_LEN 18
char my_passkey[PASSKEY_LEN + 1];

enum MDB_COMMAND_FLOW {
	RESET       = 0x00,
	SETUP       = 0x01,
	POLL        = 0x02,
	VEND        = 0x03,
	READER      = 0x04,
	EXPANSION   = 0x07
};

enum MDB_SETUP_FLOW {
	CONFIG_DATA = 0x00, MAX_MIN_PRICES = 0x01
};

enum MDB_VEND_FLOW {
	VEND_REQUEST        = 0x00,
	VEND_CANCEL         = 0x01,
	VEND_SUCCESS        = 0x02,
	VEND_FAILURE        = 0x03,
	SESSION_COMPLETE    = 0x04,
	CASH_SALE           = 0x05
};

enum MDB_READER_FLOW {
	READER_DISABLE  = 0x00,
	READER_ENABLE   = 0x01,
	READER_CANCEL   = 0x02
};

enum MDB_EXPANSION_FLOW {
	REQUEST_ID = 0x00, DIAGNOSTICS = 0xFF
};

typedef enum MACHINE_STATE {
	INACTIVE_STATE, DISABLED_STATE, ENABLED_STATE, IDLE_STATE, VEND_STATE
} machine_state_t;

machine_state_t machine_state = INACTIVE_STATE;

led_strip_handle_t led_strip;

bool session_begin_todo = false;
bool session_cancel_todo = false;
bool session_end_todo = false;
bool vend_approved_todo = false;
bool vend_denied_todo = false;
bool cashless_reset_todo = false;
bool out_of_sequence_todo = false;

uint16_t last_sale_price = 0;
uint16_t last_sale_item = 0;

time_t   last_vend_success_time = 0;

static char s_ip_wifi[16] = "";
static char s_ip_ppp[16]  = "";

#define RPC_FRESHNESS_SEC 10

esp_mqtt_client_handle_t mqtt_client = NULL;

static QueueHandle_t mdb_session_queue = NULL;
static QueueHandle_t mdb_rx_queue;

void ble_encode_with_passkey(uint8_t cmd, uint16_t item_price, uint16_t item_number, uint8_t *payload);
esp_err_t ble_decode_with_passkey(uint16_t *item_price, uint16_t *item_number, uint8_t *payload);

static void IRAM_ATTR mdb_rx_falling_isr(void *arg) {
    gpio_intr_disable(PIN_MDB_RX);

    uint16_t coming_read = 0x0000;

    ets_delay_us(156);

    for (int x = 0; x < 9; x++) {
        coming_read |= (gpio_get_level(PIN_MDB_RX) << x);
        ets_delay_us(104);
    }
    xQueueSendFromISR(mdb_rx_queue, &coming_read, NULL);

    gpio_intr_enable(PIN_MDB_RX);
}

uint16_t read_9(uint8_t *checksum) {
    uint16_t coming_read = 0;
    xQueueReceive(mdb_rx_queue, &coming_read, portMAX_DELAY);

    if (checksum)
        *checksum += coming_read;

    return coming_read;
}

void write_9(uint16_t nth9) {
    gpio_set_level(PIN_MDB_TX, 0);
    ets_delay_us(104);

	for (uint8_t x = 0; x < 9; x++) {
		gpio_set_level(PIN_MDB_TX, (nth9 >> x) & 1);
		ets_delay_us(104);
	}

    gpio_set_level(PIN_MDB_TX, 1);
    ets_delay_us(104);
}

void write_payload_9(uint8_t *mdb_payload, uint8_t length) {
	uint8_t checksum = 0x00;

	for (int x = 0; x < length; x++) {
		checksum += mdb_payload[x];
		write_9(mdb_payload[x]);
	}

	write_9(BIT_MODE_SET | checksum);
}
void mdb_cashless_task(void *pvParameters) {
	// Install the RX edge ISR from this task so the GPIO interrupt is allocated
	// on this task's core (APP_CPU/core 1), isolated from core-0 network ISRs
	// that would otherwise skew the bit-sampling timing.
	gpio_install_isr_service(0);
	gpio_isr_handler_add(PIN_MDB_RX, mdb_rx_falling_isr, NULL);

	time_t session_begin_time = 0;

	uint16_t funds_available = 0;
	uint16_t item_price = 0;
	uint16_t item_number = 0;

	uint8_t mdb_payload[36];
	uint8_t available_tx = 0;

	for (;;) {
		uint8_t checksum = 0x00;

		uint16_t coming_read = read_9(&checksum);

		if (!(coming_read & BIT_MODE_SET)) continue;

		if ((uint8_t) coming_read == ACK) continue;
		if ((uint8_t) coming_read == RET) continue;
		if ((uint8_t) coming_read == NAK) continue;

		if ((coming_read & BIT_ADD_SET) != CONFIG_CASHLESS_DEVICE_ADDRESS) continue;

		available_tx = 0;

		switch (coming_read & BIT_CMD_SET) {
		case RESET: {
			if (read_9(NULL) != checksum) continue;

			cashless_reset_todo = true;
			machine_state = INACTIVE_STATE;

			xEventGroupClearBits(xLedEventGroup, BIT_STATUS_MDB);
			xEventGroupSetBits(xLedEventGroup, BIT_STATUS_TRIGGER);

			ESP_LOGI( TAG, "RESET");
			break;
		}
		case SETUP: {
			switch (read_9(&checksum)) {
			case CONFIG_DATA: {
				uint8_t vmc_feature_level = read_9(&checksum);
				uint8_t vmc_columns_on_display = read_9(&checksum);
				uint8_t vmc_rows_on_display = read_9(&checksum);
				uint8_t vmc_display_info = read_9(&checksum);
				
				if (read_9(NULL) != checksum) continue;

				machine_state = DISABLED_STATE;

				mdb_payload[0] = 0x01;
				mdb_payload[1] = 1;
				mdb_payload[2] = CONFIG_MDB_CURRENCY_CODE >> 8;
				mdb_payload[3] = CONFIG_MDB_CURRENCY_CODE & 0xff;
				mdb_payload[4] = CONFIG_MDB_SCALE_FACTOR;
				mdb_payload[5] = CONFIG_MDB_DECIMAL_PLACES;
				mdb_payload[6] = 3;
				mdb_payload[7] = 0b00001001;
				available_tx = 8;

				ESP_LOGI( TAG, "CONFIG_DATA");
				break;
			}
			case MAX_MIN_PRICES: {
				uint16_t max_price = (read_9(&checksum) << 8) | read_9(&checksum);
				uint16_t min_price = (read_9(&checksum) << 8) | read_9(&checksum);

				(void) max_price;
				(void) min_price;

				if (read_9(NULL) != checksum) continue;

				ESP_LOGI( TAG, "MAX_MIN_PRICES");
				break;
			}
			}

			break;
		}
		case POLL: {
			if (read_9(NULL) != checksum) continue;

			if (cashless_reset_todo) {
				cashless_reset_todo = false;
				mdb_payload[0] = 0x00;
				available_tx = 1;

			} else if (machine_state <= ENABLED_STATE && xQueueReceive(mdb_session_queue, &funds_available, 0)) {
				session_begin_todo = false;

				machine_state = IDLE_STATE;

				mdb_payload[0] = 0x03;
				mdb_payload[1] = funds_available >> 8;
				mdb_payload[2] = funds_available;
				available_tx = 3;

				time( &session_begin_time);

			} else if (session_cancel_todo) {
				session_cancel_todo = false;

				mdb_payload[0] = 0x04;
				available_tx = 1;

			} else if (vend_approved_todo) {
				vend_approved_todo = false;

				mdb_payload[0] = 0x05;
				mdb_payload[1] = item_price >> 8;
				mdb_payload[2] = item_price;
				available_tx = 3;

			} else if (vend_denied_todo) {
				vend_denied_todo = false;

				mdb_payload[0] = 0x06;
				available_tx = 1;
				machine_state = IDLE_STATE;

			} else if (session_end_todo) {
				session_end_todo = false;

				mdb_payload[0] = 0x07;
				available_tx = 1;
				machine_state = ENABLED_STATE;

			} else if (out_of_sequence_todo) {
				out_of_sequence_todo = false;

				mdb_payload[0] = 0x0b;
				available_tx = 1;

			} else {
				time_t now = time(NULL);

				if (machine_state >= IDLE_STATE && (now - session_begin_time) > 60) {
					session_cancel_todo = true;
				}
			}

			break;
		}
		case VEND: {
			switch (read_9(&checksum)) {
			case VEND_REQUEST: {
				item_price = (read_9(&checksum) << 8) | read_9(&checksum);
				item_number = (read_9(&checksum) << 8) | read_9(&checksum);

				if (read_9(NULL) != checksum) continue;

				machine_state = VEND_STATE;

				if(funds_available && (funds_available != 0xffff)){
					if (item_price <= funds_available) {
						vend_approved_todo = true;
					} else {
						vend_denied_todo = true;
					}
				}

				uint8_t payload[19];
				ble_encode_with_passkey(0x0a, item_price, item_number, payload);
				ble_notify_send((char*) payload, sizeof(payload));

				ESP_LOGI( TAG, "VEND_REQUEST");
				break;
			}
			case VEND_CANCEL: {
				if (read_9(NULL) != checksum) continue;

				vend_denied_todo = true;
				break;
			}
			case VEND_SUCCESS: {
				item_number = (read_9(&checksum) << 8) | read_9(&checksum);

				if (read_9(NULL) != checksum) continue;

				machine_state = IDLE_STATE;

				last_sale_price = item_price;
				last_sale_item  = item_number;
				last_vend_success_time = time(NULL);

				uint8_t payload[19];
				ble_encode_with_passkey(0x0b, item_price, item_number, payload);
				ble_notify_send((char*) payload, sizeof(payload));

				ESP_LOGI( TAG, "VEND_SUCCESS");
				break;
			}
			case VEND_FAILURE: {
				if (read_9(NULL) != checksum) continue;

				machine_state = IDLE_STATE;

				uint8_t payload[19];
				ble_encode_with_passkey(0x0c, item_price, item_number, payload);
				ble_notify_send((char*) payload, sizeof(payload));

                char topic[64], msg[64], line[160];
                snprintf(msg, sizeof(msg), "%u:%u:%lld", item_price, item_number, (long long) time(NULL));
                rpc_sign_text(msg, line, sizeof(line));
                snprintf(topic, sizeof(topic), "domain.vmflow.xyz/%s/vend_fail", my_subdomain);
                esp_mqtt_client_enqueue(mqtt_client, topic, line, 0, 1, 0, 1);

				break;
			}
			case SESSION_COMPLETE: {
				if (read_9(NULL) != checksum) continue;

				session_end_todo = true;

				uint8_t payload[19];
				ble_encode_with_passkey(0x0d, item_price, item_number, payload);
				ble_notify_send((char*) payload, sizeof(payload));

				ESP_LOGI( TAG, "SESSION_COMPLETE");
				break;
			}
			case CASH_SALE: {
				uint16_t item_price = (read_9(&checksum) << 8) | read_9(&checksum);
				uint16_t item_number = (read_9(&checksum) << 8) | read_9(&checksum);

				if (read_9(NULL) != checksum) continue;

				uint32_t price_wire = TO_SCALE_FACTOR( FROM_SCALE_FACTOR(item_price, CONFIG_MDB_SCALE_FACTOR, CONFIG_MDB_DECIMAL_PLACES), 1, 2);

				char topic[64], msg[64], line[160];
				snprintf(msg, sizeof(msg), "%lu:%u:%lld", (unsigned long) price_wire, item_number, (long long) time(NULL));
				rpc_sign_text(msg, line, sizeof(line));

				snprintf(topic, sizeof(topic), "domain.vmflow.xyz/%s/sale", my_subdomain);
				esp_mqtt_client_enqueue(mqtt_client, topic, line, 0, 1, 0, 1);

				ESP_LOGI( TAG, "CASH_SALE");
				break;
			}
			}

			break;
		}
		case READER: {
			switch (read_9(&checksum)) {
			case READER_DISABLE: {
				if (read_9(NULL) != checksum) continue;

				machine_state = DISABLED_STATE;

				xEventGroupClearBits(xLedEventGroup, BIT_STATUS_MDB);
				xEventGroupSetBits(xLedEventGroup, BIT_STATUS_TRIGGER);

				break;
			}
			case READER_ENABLE: {
				if (read_9(NULL) != checksum) continue;

				machine_state = ENABLED_STATE;

				xEventGroupSetBits(xLedEventGroup, BIT_STATUS_MDB | BIT_STATUS_TRIGGER);

				break;
			}
			case READER_CANCEL: {
				if (read_9(NULL) != checksum) continue;

				mdb_payload[ 0 ] = 0x08;
				available_tx = 1;

				ESP_LOGI( TAG, "READER_CANCEL");
				break;
			}
			}

			break;
		}
		case EXPANSION: {
			switch (read_9(&checksum)) {
			case REQUEST_ID: {
				for(uint8_t x= 0; x < 29; x++) read_9(&checksum);

				if (read_9(NULL) != checksum) continue;

				mdb_payload[ 0 ] = 0x09;

				memcpy( &mdb_payload[1], "VMF", 3);
				memcpy( &mdb_payload[4], "            ", 12);
				memcpy( &mdb_payload[16], "            ", 12);
				mdb_payload[28] = 0x00;
				mdb_payload[29] = 0x03;

				available_tx = 30;

				ESP_LOGI( TAG, "REQUEST_ID");
				break;
			}
			}

			break;
		}
		}

		write_payload_9(mdb_payload, available_tx);
	}
}

/*
 * Agent-facing interfaces (MQTT, every message signed with the per-device passkey).
 *
 * Inbound RPC — topic <sub>.vmflow.xyz/rpc, payload "<cmd>:<params>:<ts>:<hmac_hex>".
 *   hmac = HMAC-SHA256(passkey, everything-before-the-last-colon) in lowercase hex;
 *   <ts> is Unix seconds, accepted only within RPC_FRESHNESS_SEC. <params> is fixed-width
 *   (never empty): commands without an argument send "-" as a sentinel. Commands:
 *     dex:-            trigger an on-demand DEX/telemetry read
 *     info:-           publish device snapshot JSON on .../rpc/info
 *     credit:<amount>  grant credit (amount scaled to 1/100 units)
 *     oos:-            send MDB "command out of sequence" to the VMC
 *     echo:-           reply <ts> on .../rpc/echo (liveness + RTT probe)
 *     buzzer:-         1s beep
 *     restart:-        ack on .../rpc/restart, then reboot
 *     ota:<tag>        pull app image from GitHub release (ota:- = latest, or pinned tag)
 *                      over HTTPS; progress on .../rpc/ota, then reboot
 *
 * Outbound — signed text "<fields>:<ts>:<hmac_hex>":
 *     sale  "<price>:<item>:<ts>:<hmac>"  on  .../sale
 *     pax   "<count>:<ts>:<hmac>"         on  .../paxcounter
 *
 * BLE wire payload (phone app) — 19 bytes:
 *   [0] CMD | [1-4] PRICE u32 | [5-6] ITEM u16 | [7-10] TIME u32 |
 *   [11-14] reserved=0 | [15-18] HMAC-SHA256(passkey, bytes 0-14)[:4]
 */
esp_err_t ble_decode_with_passkey(uint16_t *item_price, uint16_t *item_number, uint8_t *payload) {
	unsigned char hmac[32];
	calculate_hmac((const char*) payload, 15, hmac);

	uint8_t diff = 0;
	for(int x = 0; x < 4; x++){
		diff |= hmac[x] ^ payload[15 + x];
	}

    if(diff != 0){
        return ESP_ERR_INVALID_CRC;
    }

    int32_t timestamp = read_u32(&payload[7]);

    time_t now = time(NULL);

    if( abs((int32_t) now - timestamp) > 8 ){
        return ESP_ERR_TIMEOUT;
    }

    int32_t item_price_32 = read_u32(&payload[1]);

    if(item_price)
        *item_price = TO_SCALE_FACTOR( FROM_SCALE_FACTOR(item_price_32, 1, 2), CONFIG_MDB_SCALE_FACTOR, CONFIG_MDB_DECIMAL_PLACES);

    if(item_number)
        *item_number = read_u16(&payload[5]);

    return ESP_OK;
}

void ble_encode_with_passkey(uint8_t cmd, uint16_t item_price, uint16_t item_number, uint8_t *payload) {
    uint32_t item_price_32 = TO_SCALE_FACTOR( FROM_SCALE_FACTOR(item_price, CONFIG_MDB_SCALE_FACTOR, CONFIG_MDB_DECIMAL_PLACES), 1, 2);

	time_t now = time(NULL);

    payload[0] = cmd;

	write_u32(&payload[1], item_price_32);
	write_u16(&payload[5], item_number);
	write_u32(&payload[7], (uint32_t) now);
	write_u32(&payload[11], 0);

	unsigned char hmac[32];
	calculate_hmac((const char*) payload, 15, hmac);
	memcpy(payload + 15, hmac, 4);
}

#define LED_LVL 30   // per-channel brightness; same level on every lit channel

void led_status_task(void *pvParameters) {
    while(1){
        EventBits_t b = xEventGroupWaitBits(xLedEventGroup, BIT_STATUS_TRIGGER, pdTRUE, pdFALSE, portMAX_DELAY);

        bool installed = (b & MASK_STATUS_INSTALLED) == MASK_STATUS_INSTALLED;
        bool net = b & BIT_STATUS_MQTT;
        bool mdb = b & BIT_STATUS_MDB;

        if (!installed)      led_strip_set_pixel(led_strip, 0, LED_LVL, LED_LVL, LED_LVL);   // white   (not installed)
        else if (net && mdb) led_strip_set_pixel(led_strip, 0,       0, LED_LVL,       0);   // green   (net + MDB)
        else if (net)        led_strip_set_pixel(led_strip, 0, LED_LVL,       0, LED_LVL);   // magenta (net, no MDB)
        else if (mdb)        led_strip_set_pixel(led_strip, 0,       0,       0, LED_LVL);   // blue    (MDB, no net)
        else                 led_strip_set_pixel(led_strip, 0, LED_LVL,       0,       0);   // red     (nothing)
        led_strip_refresh(led_strip);

        if (b & BIT_STATUS_BUZZER) {
            gpio_set_level(PIN_BUZZER_PWR, 1);
            vTaskDelay(pdMS_TO_TICKS(1000));
            gpio_set_level(PIN_BUZZER_PWR, 0);

            xEventGroupClearBits(xLedEventGroup, BIT_STATUS_BUZZER);
        }
    }
}

void ble_pax_event_handler(uint16_t devices_count){
    char topic[64], msg[48], line[128];
    snprintf(msg, sizeof(msg), "%u:%lld", devices_count, (long long) time(NULL));
    rpc_sign_text(msg, line, sizeof(line));

    snprintf(topic, sizeof(topic), "domain.vmflow.xyz/%s/paxcounter", my_subdomain);
    esp_mqtt_client_enqueue(mqtt_client, topic, line, 0, 1, 0, 1);
}

void ble_event_handler(char *ble_payload) {
    printf("ble_event_handler %x\n", (uint8_t) ble_payload[0]);

	switch ( (uint8_t) ble_payload[0] ) {
    case 0x00: {
        nvs_handle_t handle;
		nvs_open("vmflow", NVS_READWRITE, &handle);

		size_t s_len;
		if (nvs_get_str(handle, "domain", NULL, &s_len) != ESP_OK) {
			strcpy(my_subdomain, ble_payload + 1);

			nvs_set_str(handle, "domain", my_subdomain);
			nvs_commit(handle);

			char myhost[64];
			snprintf(myhost, sizeof(myhost), "%s.vmflow.xyz", my_subdomain);

			ble_set_device_name(myhost);

            xEventGroupSetBits(xLedEventGroup, BIT_STATUS_DOMAIN | BIT_STATUS_TRIGGER);

			ESP_LOGI( TAG, "HOST= %s", myhost);
		}
		nvs_close(handle);
		break;
    }
    case 0x01: {
        nvs_handle_t handle;
		nvs_open("vmflow", NVS_READWRITE, &handle);

		size_t s_len;
		if (nvs_get_str(handle, "passkey", NULL, &s_len) != ESP_OK) {
			strcpy(my_passkey, ble_payload + 1);

			nvs_set_str(handle, "passkey", my_passkey);
			nvs_commit(handle);

            xEventGroupSetBits(xLedEventGroup, BIT_STATUS_PSSKEY | BIT_STATUS_TRIGGER);

			ESP_LOGI( TAG, "PASSKEY= %s", my_passkey);
		}
		nvs_close(handle);

        break;
    }
    case 0x02:
        uint16_t funds_available = 0xffff;
		xQueueSend(mdb_session_queue, &funds_available, 0);
		break;
	case 0x03:

        if(ble_decode_with_passkey(NULL, NULL, (uint8_t*) ble_payload) == ESP_OK){
            vend_approved_todo = (machine_state == VEND_STATE) ? true : false;
        }
        break;
    case 0x04:
    	session_cancel_todo = (machine_state >= IDLE_STATE) ? true : false;
        break;
    case 0x05:
        break;
    case 0x06: {
        esp_wifi_disconnect();

		wifi_config_t wifi_config = {0};
		esp_wifi_get_config(WIFI_IF_STA, &wifi_config);

		strcpy((char*) wifi_config.sta.ssid, ble_payload + 1);
		esp_wifi_set_config(WIFI_IF_STA, &wifi_config);

	    ESP_LOGI( TAG, "SSID= %s", wifi_config.sta.ssid);
        break;
    }
    case 0x07: {
        wifi_config_t wifi_config = {0};
		esp_wifi_get_config(WIFI_IF_STA, &wifi_config);

		strcpy((char*) wifi_config.sta.password, ble_payload + 1);
		esp_wifi_set_config(WIFI_IF_STA, &wifi_config);

		esp_wifi_connect();

	    ESP_LOGI( TAG, "PASSWORD= %s", wifi_config.sta.password);
		break;
    }
	}
}

// OTA worker: pulls the app image from a GitHub release asset over HTTPS, writes the inactive slot, then reboots.
// Runs in its own task because the download blocks for tens of seconds and must not stall the MQTT handler.
static void ota_task(void *arg) {
	const char *url = (const char *) arg;
	char topic[64];
	snprintf(topic, sizeof(topic), "domain.vmflow.xyz/%s/rpc/ota", my_subdomain);

	esp_http_client_config_t http_cfg = {
		.url = url,
		.crt_bundle_attach = esp_crt_bundle_attach,  // GitHub redirects to release-assets.githubusercontent.com (S3)
		.timeout_ms = 30000,
		.keep_alive_enable = true,
		// The S3 redirect target is a ~900-char signed URL; the default 512 B tx
		// buffer can't hold the request line, yielding "HTTP_CLIENT: Out of buffer".
		.buffer_size = 2048,
		.buffer_size_tx = 4096,
	};
	esp_https_ota_config_t ota_cfg = {
		.http_config = &http_cfg,
	};

	esp_err_t err = esp_https_ota(&ota_cfg);
	if (err == ESP_OK) {
		ESP_LOGW(TAG, "OTA success, rebooting into new image");
		vTaskDelay(pdMS_TO_TICKS(1000));
		esp_restart();
	} else {
		char msg[64];
		snprintf(msg, sizeof(msg), "fail:%s", esp_err_to_name(err));
		esp_mqtt_client_enqueue(mqtt_client, topic, msg, 0, 1, 0, 1);
		ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(err));
		vTaskDelete(NULL);
	}
}

// Device snapshot (JSON) on .../rpc/info for AI agents to consume.
static void rpc_publish_info(void) {
	const esp_app_desc_t *app = esp_app_get_description();

	char topic[64], json[384];
	int n = snprintf(json, sizeof(json),
		"{\"version\":\"%s\",\"uptime_s\":%lld,"
		"\"free_heap\":%lu,\"min_free_heap\":%lu,\"machine_state\":%d,"
		"\"last_sale_price\":%u,\"last_sale_item\":%u,"
		"\"last_vend_success_time\":%lld,"
		"\"ip_wifi\":\"%s\",\"ip_ppp\":\"%s\"}",
		app->version,
		(long long) (esp_timer_get_time() / 1000000),
		(unsigned long) esp_get_free_heap_size(),
		(unsigned long) esp_get_minimum_free_heap_size(),
		(int) machine_state,
		last_sale_price, last_sale_item,
		(long long) last_vend_success_time,
		s_ip_wifi, s_ip_ppp);

	snprintf(topic, sizeof(topic), "domain.vmflow.xyz/%s/rpc/info", my_subdomain);
	esp_mqtt_client_enqueue(mqtt_client, topic, json, n, 1, 0, 1);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
	esp_mqtt_event_handle_t event = event_data;
	esp_mqtt_client_handle_t mqtt_client = event->client;

	switch ((esp_mqtt_event_id_t) event_id) {
	case MQTT_EVENT_CONNECTED:

    	char topic[64], buf[32];
    	snprintf(topic, sizeof(topic), "%s.vmflow.xyz/#", my_subdomain);

    	esp_mqtt_client_subscribe(mqtt_client, topic, 0);

    	snprintf(topic, sizeof(topic), "domain.vmflow.xyz/%s/status", my_subdomain);
    	snprintf(buf, sizeof(buf), "online,%d", (int)esp_reset_reason());
    	esp_mqtt_client_enqueue(mqtt_client, topic, buf, 0, 1, 1, 1);

        xEventGroupSetBits(xLedEventGroup, BIT_STATUS_MQTT | BIT_STATUS_TRIGGER);

		break;
	case MQTT_EVENT_DISCONNECTED:

        xEventGroupClearBits(xLedEventGroup, BIT_STATUS_MQTT);
        xEventGroupSetBits(xLedEventGroup, BIT_STATUS_TRIGGER);

		break;
	case MQTT_EVENT_SUBSCRIBED: {
		char sub_topic[64];
		snprintf(sub_topic, sizeof(sub_topic), "%s.vmflow.xyz/#", my_subdomain);
		ESP_LOGI(TAG, "mqtt subscribed: %s", sub_topic);
		break;
	}
	case MQTT_EVENT_UNSUBSCRIBED:
		break;
	case MQTT_EVENT_PUBLISHED:
		ESP_LOGI(TAG, "MQTT published msg_id=%d", event->msg_id);
		break;
	case MQTT_EVENT_DATA:

	    ESP_LOGI(TAG, "MQTT data topic=%.*s len=%d data=%.*s", event->topic_len, event->topic, event->data_len, event->data_len, event->data);

		if (event->topic_len > 4 && strncmp(event->topic + event->topic_len - 4, "/rpc", 4) == 0) {
			// Wire format "<cmd>:<args>:<ts>:<hmac>".
			int len = event->data_len < 127 ? event->data_len : 127;

			// HMAC = hex over everything before the last ':'.
			char *last_colon = memrchr(event->data, ':', len);
			if (last_colon == NULL) break;

			int prefix_len = last_colon - event->data;
			char hmac[65];  // 64 hex chars + NUL
			snprintf(hmac, sizeof(hmac), "%.*s", len - prefix_len - 1, last_colon + 1);

			if (!rpc_verify_hmac(event->data, prefix_len, hmac)) {
				ESP_LOGW(TAG, "RPC rejected: bad HMAC");
				break;
			}

			char cmd[32], args[64];
			unsigned int ts;
			if (sscanf(event->data, "%31[^:]:%63[^:]:%u", cmd, args, &ts) != 3) {
				ESP_LOGW(TAG, "RPC rejected: malformed");
				break;
			}

			long dt = (long) (time(NULL) - (time_t) ts);
			if (labs(dt) > RPC_FRESHNESS_SEC) {
				ESP_LOGW(TAG, "RPC rejected: stale ts (dt=%ld)", dt);
				break;
			}

			bool has_args = (args[0] != '\0' && strcmp(args, "-") != 0);

			if (strcmp(cmd, "dex") == 0) {
				request_telemetry_data(NULL);
				ESP_LOGI(TAG, "RPC dex request started");
			} else if (strcmp(cmd, "info") == 0) {
				rpc_publish_info();
				ESP_LOGI(TAG, "RPC info published");
			} else if (strcmp(cmd, "credit") == 0 && has_args) {
				int32_t price_wire = (int32_t) strtol(args, NULL, 10);
				uint16_t funds_available = TO_SCALE_FACTOR( FROM_SCALE_FACTOR(price_wire, 1, 2), CONFIG_MDB_SCALE_FACTOR, CONFIG_MDB_DECIMAL_PLACES);

				xQueueSend(mdb_session_queue, &funds_available, 0);
				xEventGroupSetBits(xLedEventGroup, BIT_STATUS_BUZZER | BIT_STATUS_TRIGGER);

				char topic[64];
				snprintf(topic, sizeof(topic), "domain.vmflow.xyz/%s/rpc/credit", my_subdomain);
				esp_mqtt_client_enqueue(mqtt_client, topic, "ok", 0, 1, 0, 1);

				ESP_LOGI( TAG, "RPC credit: Amount= %f", FROM_SCALE_FACTOR(funds_available, CONFIG_MDB_SCALE_FACTOR, CONFIG_MDB_DECIMAL_PLACES) );
			} else if (strcmp(cmd, "oos") == 0) {
				out_of_sequence_todo = true;
				ESP_LOGI(TAG, "RPC out-of-sequence queued");
			} else if (strcmp(cmd, "echo") == 0) {
				char topic[64], buf[24];
				snprintf(topic, sizeof(topic), "domain.vmflow.xyz/%s/rpc/echo", my_subdomain);
				snprintf(buf, sizeof(buf), "%u", ts);
				esp_mqtt_client_enqueue(mqtt_client, topic, buf, 0, 0, 0, 1);
				ESP_LOGI(TAG, "RPC echo");
			} else if (strcmp(cmd, "buzzer") == 0) {
				xEventGroupSetBits(xLedEventGroup, BIT_STATUS_BUZZER | BIT_STATUS_TRIGGER);
				ESP_LOGI(TAG, "RPC buzzer triggered");
			} else if (strcmp(cmd, "restart") == 0) {
				ESP_LOGW(TAG, "RPC restart requested");
				vTaskDelay(pdMS_TO_TICKS(500));
				esp_restart();
			} else if (strcmp(cmd, "ota") == 0) {
				// "ota:-" -> latest release; "ota:<tag>" -> pinned tag.
				static char ota_url[160];
				if (has_args)
					snprintf(ota_url, sizeof(ota_url), "https://github.com/nodestark/mdb-esp32-cashless/releases/download/%s/mdb-slave-esp32s3.bin", args);
				else
					snprintf(ota_url, sizeof(ota_url), "https://github.com/nodestark/mdb-esp32-cashless/releases/latest/download/mdb-slave-esp32s3.bin");

				const char *ota_target = has_args ? args : "latest";
				xTaskCreate(ota_task, "ota_task", 8192, ota_url, 5, NULL);
				ESP_LOGW(TAG, "RPC ota: %s", ota_url);
			} else {
				ESP_LOGW(TAG, "RPC unknown command: %s", cmd);
			}
		}

		break;
	case MQTT_EVENT_ERROR:
	    if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
	        ESP_LOGE(TAG, "MQTT TCP error: errno=%d", event->error_handle->esp_transport_sock_errno);
	    } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
	        ESP_LOGE(TAG, "MQTT connection refused: 0x%x", event->error_handle->connect_return_code);
	    }
		break;
	default:
	    ESP_LOGI( TAG, "Other event id: %d", event->event_id);
		break;
	}
}

static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    switch (event_id) {
        case IP_EVENT_PPP_GOT_IP: {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
            snprintf(s_ip_ppp, sizeof(s_ip_ppp), IPSTR, IP2STR(&event->ip_info.ip));
            ESP_LOGI(TAG, "ppp got IP: %s", s_ip_ppp);
            xEventGroupSetBits(xInternetEventGroup, BIT_PPP_GOT_IP);
            break;
        }
        case IP_EVENT_PPP_LOST_IP:
            s_ip_ppp[0] = '\0';
            ESP_LOGW(TAG, "ppp lost IP, restarting sim7080g_task");
            xEventGroupClearBits(xInternetEventGroup, BIT_PPP_GOT_IP);
            xEventGroupSetBits(xInternetEventGroup, BIT_PPP_LOST_IP);
            break;
        case IP_EVENT_STA_GOT_IP: {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
            snprintf(s_ip_wifi, sizeof(s_ip_wifi), IPSTR, IP2STR(&event->ip_info.ip));
            ESP_LOGI(TAG, "wifi got IP: %s", s_ip_wifi);
            xEventGroupSetBits(xInternetEventGroup, BIT_STA_GOT_IP);
            break;
        }
        case IP_EVENT_STA_LOST_IP:
            s_ip_wifi[0] = '\0';
            xEventGroupClearBits(xInternetEventGroup, BIT_STA_GOT_IP);
            xEventGroupSetBits(xInternetEventGroup, BIT_STA_LOST_IP);
            ESP_LOGW(TAG, "wifi lost IP");
            break;
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    switch (event_id) {
    case WIFI_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case WIFI_EVENT_STA_CONNECTED:
        wifi_backoff_ms = WIFI_BACKOFF_MIN_MS;   // reset na conexão
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        ESP_LOGW(TAG, "WiFi disconnected, retry in %lu ms", wifi_backoff_ms);
        esp_timer_start_once(wifi_retry_timer, (uint64_t) wifi_backoff_ms * 1000);
        wifi_backoff_ms *= 2;
        if (wifi_backoff_ms > WIFI_BACKOFF_MAX_MS) wifi_backoff_ms = WIFI_BACKOFF_MAX_MS;
        break;
    }
}

static void sim7080g_pulse_power(void) {
    gpio_set_level(PIN_SIM7080G_PWR, 1);
    vTaskDelay(pdMS_TO_TICKS(1200));
    gpio_set_level(PIN_SIM7080G_PWR, 0);

    ESP_LOGI(TAG, "waiting for SIM7080G boot...");
    vTaskDelay(pdMS_TO_TICKS(5000));
}

static esp_err_t sim7080g_wait_registered(esp_modem_dce_t *dce) {
    char resp[64];
    for (int i = 0; i < 30; i++) {
        memset(resp, 0, sizeof(resp));
        esp_modem_at(dce, "AT+CEREG?", resp, 3000);
        if (strstr(resp, ",1") || strstr(resp, ",5")) {
            ESP_LOGI(TAG, "registered: %s", resp);
            return ESP_OK;
        }
        ESP_LOGW(TAG, "not registered: %s", resp);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    ESP_LOGE(TAG, "registration timeout");

    return ESP_ERR_TIMEOUT;
}

// Owns SIM7080G + MQTT lifecycle. MQTT stays off while the modem (re)connects:
// on IP_EVENT_PPP_LOST_IP the task stops MQTT and re-runs the modem bring-up.
static void sim7080g_task(void *pvParameters) {
    gpio_set_direction(PIN_SIM7080G_PWR, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_SIM7080G_PWR, 0);

    esp_modem_dte_config_t dte_config = ESP_MODEM_DTE_DEFAULT_CONFIG();
    dte_config.uart_config.port_num   = UART_NUM_2;
    dte_config.uart_config.baud_rate  = 115200;
    dte_config.uart_config.tx_io_num  = PIN_SIM7080G_TX;
    dte_config.uart_config.rx_io_num  = PIN_SIM7080G_RX;
    dte_config.uart_config.rts_io_num = -1;
    dte_config.uart_config.cts_io_num = -1;

    esp_modem_dce_config_t dce_config = ESP_MODEM_DCE_DEFAULT_CONFIG(CONFIG_SIM7080G_APN);

    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_PPP();
    esp_netif_t *ppp_netif = esp_netif_new(&netif_cfg);
    esp_netif_set_route_prio(ppp_netif, 100);

    esp_modem_dce_t *dce = esp_modem_new_dev(ESP_MODEM_DCE_SIM7070, &dte_config, &dce_config, ppp_netif);
    assert(dce);

    //-------------------------- MQTT --------------------------//
    char lwt_topic[64];
    snprintf(lwt_topic, sizeof(lwt_topic), "domain.vmflow.xyz/%s/status", my_subdomain);

    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://mqtt.vmflow.xyz",
        .session.last_will.topic = lwt_topic,
        .session.last_will.msg = "offline",
        .session.last_will.qos = 1,
        .session.last_will.retain = 1,
        .session.keepalive = 120,
        .network.timeout_ms = 30000,
        .network.reconnect_timeout_ms = 15000,
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    while (1) {
        esp_modem_set_mode(dce, ESP_MODEM_MODE_COMMAND);

        esp_err_t ret = esp_modem_sync(dce);
        if (ret != ESP_OK) {
            sim7080g_pulse_power();

            ret = esp_modem_sync(dce);
            if (ret != ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(2000));
                ret = esp_modem_sync(dce);
            }
        } else {
            ESP_LOGI(TAG, "modem already on");
            esp_modem_at(dce, "AT+CFUN=1,1", NULL, 3000);
            vTaskDelay(pdMS_TO_TICKS(8000));
        }

        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "modem not found, skip PPP");
        } else {
            ESP_LOGI(TAG, "modem ok");

            esp_modem_at(dce, "AT+CNMP=38", NULL, 3000);
            esp_modem_at(dce, "AT+CMNB=" STRINGIFY(CONFIG_SIM7080G_CMNB), NULL, 3000);
            esp_modem_at(dce, "AT+CEREG=1", NULL, 3000);

            if (sim7080g_wait_registered(dce) == ESP_OK) {
                esp_modem_set_mode(dce, ESP_MODEM_MODE_DATA);
            }
        }

        xEventGroupWaitBits(xInternetEventGroup, BIT_PPP_GOT_IP | BIT_STA_GOT_IP, pdFALSE, pdFALSE, portMAX_DELAY);
        esp_mqtt_client_start(mqtt_client);

        xEventGroupWaitBits(xInternetEventGroup, BIT_PPP_LOST_IP, pdTRUE, pdTRUE, portMAX_DELAY);
        esp_mqtt_client_stop(mqtt_client);
    }
}

void app_main(void) {
    gpio_set_direction(PIN_BUZZER_PWR, GPIO_MODE_OUTPUT);
	gpio_set_level(PIN_BUZZER_PWR, 0);

	//---------------- Strip LED configuration -----------------//
	//----------------------------------------------------------//
    xLedEventGroup = xEventGroupCreate();
    xInternetEventGroup = xEventGroupCreate();

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

	//---------------- UART1 - EVA DTS DEX/DDCMP ---------------//
	//----------------------------------------------------------//
	telemetry_init();

	//-------------------- NETWORK STACK -----------------------//
	//----------------------------------------------------------//
	nvs_flash_init();

	esp_netif_init();
	esp_event_loop_create_default();

	esp_netif_t *wifi_netif = esp_netif_create_default_wifi_sta();
    esp_netif_set_route_prio(wifi_netif, 200);

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	esp_wifi_init(&cfg);

	esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL);
	esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, ip_event_handler, NULL, NULL);

	const esp_timer_create_args_t wifi_timer_args = {
		.callback = wifi_retry_cb,
		.name = "wifi_retry",
	};
	esp_timer_create(&wifi_timer_args, &wifi_retry_timer);

	esp_wifi_set_mode(WIFI_MODE_STA);
	esp_wifi_start();

    //-------------------------- SNTP --------------------------//
    //----------------------------------------------------------//
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

	//------------------------ BLUETOOTH -----------------------//
	//----------------------------------------------------------//
	char myhost[64];
	strcpy(myhost, "0.vmflow.xyz");

    nvs_handle_t handle;
	if (nvs_open("vmflow", NVS_READONLY, &handle) == ESP_OK) {
	    size_t s_len = 0;
	    if (nvs_get_str(handle, "passkey", NULL, &s_len) == ESP_OK) {
            nvs_get_str(handle, "passkey", my_passkey, &s_len);

            if (nvs_get_str(handle, "domain", NULL, &s_len) == ESP_OK) {
                nvs_get_str(handle, "domain", my_subdomain, &s_len);

                snprintf(myhost, sizeof(myhost), "%s.vmflow.xyz", my_subdomain);

                xEventGroupSetBits(xLedEventGroup, BIT_STATUS_PSSKEY | BIT_STATUS_DOMAIN | BIT_STATUS_TRIGGER);
            }
        }

		nvs_close(handle);
	}

	// HMAC key tracks the passkey buffer by reference; later BLE provisioning
	// writes into the same buffer and takes effect without re-registering.
	rpc_auth_set_key(my_passkey);

	ble_init(myhost, ble_event_handler, ble_pax_event_handler);

    esp_timer_handle_t periodic_pax_timer;

	const esp_timer_create_args_t periodic_pax_timer_args = {
		.callback   = &ble_scan_start,
        .arg        = (void*) (uintptr_t) PAX_SCAN_DURATION_SEC,
		.name       = "task_paxcounter"
	};
    esp_timer_create(&periodic_pax_timer_args, &periodic_pax_timer);
    esp_timer_start_periodic(periodic_pax_timer, PAX_SCAN_INTERVAL_US);

    //------------------------ MAIN TASKS ----------------------//
    //----------------------------------------------------------//
    gpio_set_direction(PIN_MDB_TX, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_MDB_TX, 1);

    mdb_rx_queue = xQueueCreate(16, sizeof(uint16_t));

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_MDB_RX),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&io_conf);

    mdb_session_queue = xQueueCreate(1, sizeof(uint16_t));

    // Bit-banged MDB pinned alone to core 1 at high prio so core-0 network never preempts a frame.
    xTaskCreatePinnedToCore(mdb_cashless_task, "mdb_cashless_task", 8192, NULL, configMAX_PRIORITIES - 2, NULL, 1);

    //------------------- SIM7080g STACK -----------------------//
	//----------------------------------------------------------//
    xTaskCreatePinnedToCore(sim7080g_task, "sim7080g_task", 4096, NULL, 5, NULL, 0);
}