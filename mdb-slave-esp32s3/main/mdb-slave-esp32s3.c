/*
 * VMflow.xyz
 *
 * mdb-slave-esp32s3.c - Vending machine peripheral
 *
 */

#include <esp_log.h>

#include <sdkconfig.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_wifi.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/ringbuf.h>
#include <math.h>
#include <mqtt_client.h>
#include <nvs_flash.h>
#include <stdio.h>
#include <string.h>
#include <esp_sntp.h>
#include <time.h>
#include <rom/ets_sys.h>


#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>

#include "led_strip.h"

#include "nimble.h"
#include "webui_server.h"

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "cJSON.h"

#define TAG "mdb_cashless"

#define PIN_I2C_SDA             GPIO_NUM_10
#define PIN_I2C_SCL             GPIO_NUM_11
#define PIN_PULSE_1             GPIO_NUM_13
#define PIN_MDB_RX              GPIO_NUM_4
#define PIN_MDB_TX              GPIO_NUM_5
#define PIN_MDB_LED             GPIO_NUM_21
#define PIN_DEX_RX              GPIO_NUM_8
#define PIN_DEX_TX              GPIO_NUM_9
#define PIN_SIM7080G_RX         GPIO_NUM_18
#define PIN_SIM7080G_TX         GPIO_NUM_17
#define PIN_SIM7080G_PWR        GPIO_NUM_14
#define PIN_BUZZER_PWR          GPIO_NUM_12

#define ADC_UNIT_THERMISTOR     ADC_UNIT_1
#define ADC_CHANNEL_THERMISTOR  ADC_CHANNEL_6   // Define the ADC unit, channel, and attenuation (NTC Thermistor)

// Functions for scale factor conversion
#define TO_SCALE_FACTOR(p, scale_to, dec_to) (p / scale_to / pow(10, -(dec_to) ))               // Converts to scale factor
#define FROM_SCALE_FACTOR(p, scale_from, dec_from) (p * scale_from * pow(10, -(dec_from) ))     // Converts from scale factor

#define ACK 	0x00  // Acknowledgment / Checksum correct
#define RET 	0xAA  // Retransmit previously sent data. Only VMC can send this
#define NAK 	0xFF  // Negative acknowledgment

// Bit masks for MDB operations
#define BIT_MODE_SET 	0b100000000
#define BIT_ADD_SET   	0b011111000
#define BIT_CMD_SET   	0b000000111

#define WIFI_MAX_RETRY  5

static int wifi_retry_num = 0;

enum BIT_EVENTS {
    BIT_EVT_INTERNET    = (1 << 0),
    BIT_EVT_MDB         = (1 << 1),
    BIT_EVT_PSSKEY      = (1 << 2),
    BIT_EVT_DOMAIN      = (1 << 3),
    BIT_EVT_BUZZER      = (1 << 4),
    BIT_EVT_TRIGGER     = (1 << 5),
    MASK_EVT_INSTALLED  = (BIT_EVT_PSSKEY | BIT_EVT_DOMAIN)
};

EventGroupHandle_t xLedEventGroup;

static bool mqtt_started = false;
static bool sntp_started = false;
static bool ota_in_progress = false;

char my_company_id[40];   // UUID from Supabase companies table
char my_device_id[40];    // UUID from Supabase embeddeds table
char my_passkey[18];
static char my_srv_url[128] = {0};  // Backend server URL for OTA downloads

// MDB address — initialised from Kconfig, overridden by NVS at boot
static uint8_t cashless_device_address = CONFIG_CASHLESS_DEVICE_ADDRESS;
#ifdef CONFIG_MDB_SNIFF_OTHER_CASHLESS
static uint8_t mdb_sniff_address = CONFIG_MDB_SNIFF_ADDRESS;
#endif

// Defining MDB commands as an enum
enum MDB_COMMAND_FLOW {
	RESET       = 0x00,
	SETUP       = 0x01,
	POLL        = 0x02,
	VEND        = 0x03,
	READER      = 0x04,
	EXPANSION   = 0x07
};

// Defining MDB setup flow
enum MDB_SETUP_FLOW {
	CONFIG_DATA = 0x00, MAX_MIN_PRICES = 0x01
};

// Defining MDB vending flow
enum MDB_VEND_FLOW {
	VEND_REQUEST        = 0x00,
	VEND_CANCEL         = 0x01,
	VEND_SUCCESS        = 0x02,
	VEND_FAILURE        = 0x03,
	SESSION_COMPLETE    = 0x04,
	CASH_SALE           = 0x05
};

// Defining MDB reader flow
enum MDB_READER_FLOW {
	READER_DISABLE  = 0x00,
	READER_ENABLE   = 0x01,
	READER_CANCEL   = 0x02
};

// Defining MDB expansion flow
enum MDB_EXPANSION_FLOW {
	REQUEST_ID = 0x00, DIAGNOSTICS = 0xFF
};

// Defining machine states
typedef enum MACHINE_STATE {
	INACTIVE_STATE, DISABLED_STATE, ENABLED_STATE, IDLE_STATE, VEND_STATE
} machine_state_t;

machine_state_t machine_state = INACTIVE_STATE; // Initial machine state

led_strip_handle_t led_strip;

// MDB Control flags
bool session_begin_todo = false;
bool session_cancel_todo = false;
bool session_end_todo = false;
bool vend_approved_todo = false;
bool vend_denied_todo = false;
bool cashless_reset_todo = false;
bool out_of_sequence_todo = false;

// MDB diagnostics
static uint32_t mdb_poll_count = 0;
static uint32_t mdb_checksum_errors = 0;
static const char *mdb_last_cmd = "none";
static machine_state_t mdb_prev_state = INACTIVE_STATE;

static const char *machine_state_name(machine_state_t s) {
    switch (s) {
        case INACTIVE_STATE: return "INACTIVE";
        case DISABLED_STATE: return "DISABLED";
        case ENABLED_STATE:  return "ENABLED";
        case IDLE_STATE:     return "IDLE";
        case VEND_STATE:     return "VEND";
        default:             return "UNKNOWN";
    }
}

RingbufHandle_t dexRingbuf;

// MQTT client handle
esp_mqtt_client_handle_t mqttClient = NULL;

// Message queues for communication
static QueueHandle_t mdbSessionQueue = NULL;

uint16_t read_9(uint8_t *checksum) {

	uint16_t coming_read = 0;

	// Wait start bit
	while (gpio_get_level(PIN_MDB_RX))
		;

	ets_delay_us(104);

	ets_delay_us(52);
	for(int x = 0; x < 9; x++){
		coming_read |= (gpio_get_level(PIN_MDB_RX) << x);
		ets_delay_us(104); // 9600bps
	}

	if (checksum)
		*checksum += coming_read;

	return coming_read;
}

void write_9(uint16_t nth9) {

    gpio_set_level(PIN_MDB_TX, 0);  // Start bit
    ets_delay_us(104); // 9600bps

	for (uint8_t x = 0; x < 9; x++) {

		gpio_set_level(PIN_MDB_TX, (nth9 >> x) & 1);
		ets_delay_us(104); // 9600bps
	}

    gpio_set_level(PIN_MDB_TX, 1);  // Stop bit
    ets_delay_us(104); // 9600bps
}

// Function to transmit the payload via bit-banging (using MDB protocol)
// Switches TX pin to output for the entire payload, then back to high-Z.
void write_payload_9(uint8_t *mdb_payload, uint8_t length) {

	gpio_set_direction(PIN_MDB_TX, GPIO_MODE_OUTPUT);
	gpio_set_level(PIN_MDB_TX, 1);  // idle HIGH before first start bit

	uint8_t checksum = 0x00;

	// Calculate checksum
	for (int x = 0; x < length; x++) {

		checksum += mdb_payload[x];
		write_9(mdb_payload[x]);
	}

	// CHK* ACK*
	write_9(BIT_MODE_SET | checksum);

	// Release the bus — back to high-Z so we don't interfere with other peripherals
	gpio_set_direction(PIN_MDB_TX, GPIO_MODE_INPUT);
}

void xorEncodeWithPasskey(uint8_t cmd, uint16_t itemPrice, uint16_t itemNumber, uint16_t paxCounter, uint8_t *payload);
uint8_t xorDecodeWithPasskey(uint16_t *itemPrice, uint16_t *itemNumber, uint8_t *payload);

// Main MDB loop function
void vTaskMdbEvent(void *pvParameters) {

	ESP_LOGW(TAG, "MDB TASK: starting, cashless_addr=0x%02X (%s)",
		cashless_device_address,
		cashless_device_address == 16 ? "Cashless #1" : "Cashless #2");

	time_t session_begin_time = 0;

	uint16_t fundsAvailable = 0;
	uint16_t itemPrice = 0;
	uint16_t itemNumber = 0;

	// Payload buffer and available transmission flag
	uint8_t mdb_payload[36];
	uint8_t available_tx = 0;

#ifdef CONFIG_MDB_SNIFF_OTHER_CASHLESS
	// State for passively sniffed Cashless Device #2 (e.g. Nayax credit card terminal)
	uint16_t sniff_itemPrice = 0;
	uint16_t sniff_itemNumber = 0;
#endif

	for (;;) {

		// In the MDB (Multi-Drop Bus) protocol, the last byte of a command or data packet is a checksum.
		uint8_t checksum = 0x00;

		// Read from MDB and check if the mode bit is set
		uint16_t coming_read = read_9(&checksum);

		if (coming_read & BIT_MODE_SET) {

			if ((uint8_t) coming_read == ACK) {
				// ACK
			} else if ((uint8_t) coming_read == RET) {
				// RET
			} else if ((uint8_t) coming_read == NAK) {
				// NAK
			} else if ((coming_read & BIT_ADD_SET) == cashless_device_address) {

				// Reset transmission availability
				available_tx = 0;

				// Command decoding based on incoming data
				switch (coming_read & BIT_CMD_SET) {

				case RESET: {

                    if (read_9(NULL) != checksum) { mdb_checksum_errors++; continue; }

                    // Reset during VEND_STATE is interpreted as VEND_SUCCESS

					cashless_reset_todo = true;
					machine_state = INACTIVE_STATE;
					mdb_last_cmd = "RESET";

                    xEventGroupClearBits(xLedEventGroup, BIT_EVT_MDB);
                    xEventGroupSetBits(xLedEventGroup, BIT_EVT_TRIGGER);

					ESP_LOGI( TAG, "RESET");
					break;
				}
				case SETUP: {
					switch (read_9(&checksum)) {

					case CONFIG_DATA: {
						uint8_t vmcFeatureLevel = read_9(&checksum);
						uint8_t vmcColumnsOnDisplay = read_9(&checksum);
						uint8_t vmcRowsOnDisplay = read_9(&checksum);
						uint8_t vmcDisplayInfo = read_9(&checksum);

						(void) vmcDisplayInfo;
						(void) vmcRowsOnDisplay;
						(void) vmcColumnsOnDisplay;
						(void) vmcFeatureLevel;

                        if (read_9(NULL) != checksum) { mdb_checksum_errors++; continue; }

						machine_state = DISABLED_STATE;
						mdb_last_cmd = "SETUP:CONFIG_DATA";

                        mdb_payload[0] = 0x01;                                  // Reader Config Data
                        mdb_payload[1] = 1;                                     // Reader Feature Level
						mdb_payload[2] = CONFIG_MDB_CURRENCY_CODE >> 8;         // Country Code High
						mdb_payload[3] = CONFIG_MDB_CURRENCY_CODE & 0xff;       // Country Code Low
						mdb_payload[4] = CONFIG_MDB_SCALE_FACTOR;               // Scale Factor
						mdb_payload[5] = CONFIG_MDB_DECIMAL_PLACES;             // Decimal Places
						mdb_payload[6] = 3;                                     // Maximum Response Time (5s)
						mdb_payload[7] = 0b00001001;                            // Miscellaneous Options
						available_tx = 8;

						// idf.py menuconfig -> "MDB Cashless Device"

						ESP_LOGI( TAG, "CONFIG_DATA");
						break;
					}
					case MAX_MIN_PRICES: {

						uint16_t maxPrice = (read_9(&checksum) << 8) | read_9(&checksum);
						uint16_t minPrice = (read_9(&checksum) << 8) | read_9(&checksum);

						(void) maxPrice;
						(void) minPrice;

                        if (read_9(NULL) != checksum) { mdb_checksum_errors++; continue; }

						mdb_last_cmd = "SETUP:MAX_MIN_PRICES";
						ESP_LOGI( TAG, "MAX_MIN_PRICES");
						break;
					}
					}

					break;
				}
				case POLL: {

				    if (read_9(NULL) != checksum) { mdb_checksum_errors++; continue; }

					mdb_poll_count++;

					// Log state changes
					if (machine_state != mdb_prev_state) {
						ESP_LOGW(TAG, "MDB STATE: %s -> %s (polls=%lu, chkErr=%lu)",
							machine_state_name(mdb_prev_state), machine_state_name(machine_state),
							mdb_poll_count, mdb_checksum_errors);
						mdb_prev_state = machine_state;
					}

					// Periodic log every 500 polls (~every 50s at typical VMC rate)
					if (mdb_poll_count % 500 == 0) {
						ESP_LOGI(TAG, "MDB DIAG: state=%s addr=0x%02X polls=%lu chkErr=%lu lastCmd=%s",
							machine_state_name(machine_state), cashless_device_address,
							mdb_poll_count, mdb_checksum_errors, mdb_last_cmd);
					}

					if (cashless_reset_todo) {
						// Just reset
						cashless_reset_todo = false;
						mdb_payload[0] = 0x00;
						available_tx = 1;

					} else if (machine_state <= ENABLED_STATE && xQueueReceive(mdbSessionQueue, &fundsAvailable, 0)) {
						// Begin session
						session_begin_todo = false;

						machine_state = IDLE_STATE;

						mdb_payload[0] = 0x03;
                        mdb_payload[1] = fundsAvailable >> 8;
                        mdb_payload[2] = fundsAvailable;
                        available_tx = 3;

						time( &session_begin_time);

					} else if (session_cancel_todo) {
						// Cancel session
						session_cancel_todo = false;

						mdb_payload[0] = 0x04;
						available_tx = 1;

					} else if (vend_approved_todo) {
						// Vend approved
						vend_approved_todo = false;

						mdb_payload[0] = 0x05;
						mdb_payload[1] = itemPrice >> 8;
						mdb_payload[2] = itemPrice;
						available_tx = 3;

					} else if (vend_denied_todo) {
						// Vend denied
						vend_denied_todo = false;

						mdb_payload[0] = 0x06;
						available_tx = 1;
						machine_state = IDLE_STATE;

					} else if (session_end_todo) {
						// End session
						session_end_todo = false;

						mdb_payload[0] = 0x07;
						available_tx = 1;
						machine_state = ENABLED_STATE;

					} else if (out_of_sequence_todo) {
						// Command out of sequence
						out_of_sequence_todo = false;

						mdb_payload[0] = 0x0b;
						available_tx = 1;

					} else {

						time_t now = time(NULL);

						if (machine_state >= IDLE_STATE && (now - session_begin_time /*elapsed*/) > 60 /*60 sec*/) {
							session_cancel_todo = true;
						}
					}

					break;
				}
				case VEND: {
					switch (read_9(&checksum)) {
					case VEND_REQUEST: {

						itemPrice = (read_9(&checksum) << 8) | read_9(&checksum);
						itemNumber = (read_9(&checksum) << 8) | read_9(&checksum);

                        if (read_9(NULL) != checksum) { mdb_checksum_errors++; continue; }

						machine_state = VEND_STATE;
						mdb_last_cmd = "VEND_REQUEST";

                        if(fundsAvailable && (fundsAvailable != 0xffff)){

                            if (itemPrice <= fundsAvailable) {
                                vend_approved_todo = true;
                            } else {
                                vend_denied_todo = true;
                            }
                        }

						/* PIPE_BLE */
						uint8_t payload[19];
						xorEncodeWithPasskey(0x0a, itemPrice, itemNumber, 0, (uint8_t*) &payload);

                        ble_notify_send((char*) &payload, sizeof(payload));

						ESP_LOGI( TAG, "VEND_REQUEST");
						break;
					}
					case VEND_CANCEL: {
                        if (read_9(NULL) != checksum) { mdb_checksum_errors++; continue; }

						mdb_last_cmd = "VEND_CANCEL";
						vend_denied_todo = true;
						break;
					}
					case VEND_SUCCESS: {

						itemNumber = (read_9(&checksum) << 8) | read_9(&checksum);

                        if (read_9(NULL) != checksum) { mdb_checksum_errors++; continue; }

						machine_state = IDLE_STATE;
						mdb_last_cmd = "VEND_SUCCESS";

						/* PIPE_BLE */
						uint8_t payload[19];
						xorEncodeWithPasskey(0x0b, itemPrice, itemNumber, 0, (uint8_t*) &payload);

                        ble_notify_send((char*) &payload, sizeof(payload));

						ESP_LOGI( TAG, "VEND_SUCCESS");
						break;
					}
					case VEND_FAILURE: {
                        if (read_9(NULL) != checksum) { mdb_checksum_errors++; continue; }

						machine_state = IDLE_STATE;
						mdb_last_cmd = "VEND_FAILURE";

					    /* PIPE_BLE */
						uint8_t payload[19];
						xorEncodeWithPasskey(0x0c, itemPrice, itemNumber, 0, (uint8_t*) &payload);

                        ble_notify_send((char*) &payload, sizeof(payload));
						break;
					}
					case SESSION_COMPLETE: {
                        if (read_9(NULL) != checksum) { mdb_checksum_errors++; continue; }

						mdb_last_cmd = "SESSION_COMPLETE";
						session_end_todo = true;

			            /* PIPE_BLE */
						uint8_t payload[19];
						xorEncodeWithPasskey(0x0d, itemPrice, itemNumber, 0, (uint8_t*) &payload);

                        ble_notify_send((char*) &payload, sizeof(payload));

						ESP_LOGI( TAG, "SESSION_COMPLETE");
						break;
					}
					case CASH_SALE: {

						uint16_t itemPrice = (read_9(&checksum) << 8) | read_9(&checksum);
						uint16_t itemNumber = (read_9(&checksum) << 8) | read_9(&checksum);

						if (read_9(NULL) != checksum) { mdb_checksum_errors++; continue; }

						mdb_last_cmd = "CASH_SALE";

                        uint8_t payload[19];
                        xorEncodeWithPasskey(0x21, itemPrice, itemNumber, 0, (uint8_t*) &payload);

                        char topic[128];
                        snprintf(topic, sizeof(topic), "/%s/%s/sale", my_company_id, my_device_id);

                        esp_mqtt_client_publish(mqttClient, topic, (char*) &payload, sizeof(payload), 1, 0);

                        ESP_LOGI( TAG, "CASH_SALE");
						break;
					}
					}

					break;
				}
				case READER: {
					switch (read_9(&checksum)) {
					case READER_DISABLE: {
                        if (read_9(NULL) != checksum) { mdb_checksum_errors++; continue; }

						machine_state = DISABLED_STATE;
						mdb_last_cmd = "READER_DISABLE";

                        xEventGroupClearBits(xLedEventGroup, BIT_EVT_MDB);
                        xEventGroupSetBits(xLedEventGroup, BIT_EVT_TRIGGER);

						break;
					}
					case READER_ENABLE: {
                        if (read_9(NULL) != checksum) { mdb_checksum_errors++; continue; }

						machine_state = ENABLED_STATE;
						mdb_last_cmd = "READER_ENABLE";

                        xEventGroupSetBits(xLedEventGroup, BIT_EVT_MDB | BIT_EVT_TRIGGER);

						break;
					}
					case READER_CANCEL: {
                        if (read_9(NULL) != checksum) { mdb_checksum_errors++; continue; }

						mdb_last_cmd = "READER_CANCEL";
						mdb_payload[ 0 ] = 0x08; // Canceled
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

                        /*char manufacturerCode[3];
                        char serialNumber[12];
                        char modelNumber[12];
                        char softwareVersion[2];*/

					    for(uint8_t x= 0; x < 29; x++) read_9(&checksum); // ...drop

				        if (read_9(NULL) != checksum) { mdb_checksum_errors++; continue; }

						mdb_last_cmd = "EXPANSION:REQUEST_ID";

                        mdb_payload[ 0 ] = 0x09;                        // Peripheral ID

                        memcpy( &mdb_payload[1], "VMF", 3);             // Manufacture code
                        memcpy( &mdb_payload[4], "            ", 12);   // Serial number
                        memcpy( &mdb_payload[16], "            ", 12);  // Model number
                        memcpy( &mdb_payload[28], "03", 2);             // Software version

                        available_tx = 30;

                        ESP_LOGI( TAG, "REQUEST_ID");
						break;
					}
					}

					break;
				}
				}

				// Transmit the prepared payload via bit-banging
				write_payload_9((uint8_t*) &mdb_payload, available_tx);

			}
#ifdef CONFIG_MDB_SNIFF_OTHER_CASHLESS
			else if ((coming_read & BIT_ADD_SET) == mdb_sniff_address) {

				// Passively sniff Cashless Device #2 traffic (read-only, never transmit)
				switch (coming_read & BIT_CMD_SET) {
				case VEND: {
					uint8_t checksum_sniff = 0x00;
					checksum_sniff += coming_read;
					switch (read_9(&checksum_sniff)) {
					case VEND_REQUEST: {
						sniff_itemPrice = (read_9(&checksum_sniff) << 8) | read_9(&checksum_sniff);
						sniff_itemNumber = (read_9(&checksum_sniff) << 8) | read_9(&checksum_sniff);
						if (read_9(NULL) != checksum_sniff) continue;
						ESP_LOGI(TAG, "SNIFF VEND_REQUEST price=%u item=%u", sniff_itemPrice, sniff_itemNumber);
						break;
					}
					case VEND_SUCCESS: {
						uint16_t sn = (read_9(&checksum_sniff) << 8) | read_9(&checksum_sniff);
						if (read_9(NULL) != checksum_sniff) continue;
						if (sn != 0xFFFF) sniff_itemNumber = sn;

						ESP_LOGI(TAG, "SNIFF CARD_SALE price=%u item=%u", sniff_itemPrice, sniff_itemNumber);

						uint8_t payload[19];
						xorEncodeWithPasskey(0x23, sniff_itemPrice, sniff_itemNumber, 0, (uint8_t*) &payload);

						char topic[128];
						snprintf(topic, sizeof(topic), "/%s/%s/sale", my_company_id, my_device_id);
						esp_mqtt_client_publish(mqttClient, topic, (char*) &payload, sizeof(payload), 1, 0);
						break;
					}
					default:
						break;
					}
					break;
				}
				default:
					break;
				}
			}
#endif
			else {
				// Not the intended address...
			}
		}
	}
}

/*
 * 19-byte payload:
 * - Initially filled with random data (esp_fill_random)
 * - Fixed fields overwritten according to CMD semantics
 * - Payload XOR-obfuscated with provisioning/session key before TX
 *
 * Byte index →
 * +----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+
 * |CMD |VER |    ITEM_PRICE     |ITEM_NUMB|       TIME_SEC    |PAX_COUNT|     NOT_USED           |
 * +----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+
 *   0    1    2    3    4    5    6    7    8    9   10   11   12   13   14   15   16   17   18
 *
 * CMD (byte 0) – vmflow protocol opcode:
 *  BLE target <- client:
 *   0x00 SUBDOMAIN (device_id) | 0x01 PASSKEY (cipher_code) | 0x02 START_SESSION
 *   0x03 APPROVE_SESSION       | 0x04 CLOSE_SESSION
 *   0x06 WIFI_SSID             | 0x07 WIFI_PASSWORD
 *
 *  BLE target -> client:
 *   0x0A VEND_REQUEST | 0x0B VEND_SUCCESS | 0x0C VEND_FAILURE | 0x0D SESSION_COMPLETE
 *
 *  MQTT target <- server:
 *   0x20 CREDIT
 *
 *  MQTT target -> server:
 *   0x21 CASH_SALE | 0x22 PAX_COUNTER | 0x23 CARD_SALE
 *
 * [ random filler ] + [ structured fields per CMD ] → XOR → obfuscated payload
 */

// Decode payload from communication between BLE and MQTT
uint8_t xorDecodeWithPasskey(uint16_t *itemPrice, uint16_t *itemNumber, uint8_t *payload) {

	for(int x = 0; x < sizeof(my_passkey); x++){
		payload[x + 1] ^= my_passkey[x];
	}

	int p_len = sizeof(my_passkey) + 1;

	uint8_t chk = 0x00;
	for(int x= 0; x < p_len - 1; x++){
		chk += payload[x];
	}

    if(chk != payload[p_len - 1]){
        return 0;
    }

    int32_t timestamp = ((uint32_t) payload[8] << 24) |
						((uint32_t) payload[9] << 16) |
						((uint32_t) payload[10] << 8)  |
						((uint32_t) payload[11] << 0);

    time_t now = time(NULL);

    if( abs((int32_t) now - timestamp) > 8 /*sec*/){
        return 0;
    }

    int32_t itemPrice32 =   ((uint32_t) payload[2] << 24) |
                            ((uint32_t) payload[3] << 16) |
                            ((uint32_t) payload[4] << 8)  |
                            ((uint32_t) payload[5] << 0);

    if(itemPrice)
        *itemPrice = TO_SCALE_FACTOR( FROM_SCALE_FACTOR(itemPrice32, 1, 2), CONFIG_MDB_SCALE_FACTOR, CONFIG_MDB_DECIMAL_PLACES);

    if(itemNumber)
        *itemNumber = ((uint16_t) payload[6] << 8) | ((uint16_t) payload[7] << 0);

    return 1;
}

// Encode payload to communication between BLE and MQTT
void xorEncodeWithPasskey(uint8_t cmd, uint16_t itemPrice, uint16_t itemNumber, uint16_t paxCounter, uint8_t *payload) {

    uint32_t itemPrice32 = TO_SCALE_FACTOR( FROM_SCALE_FACTOR(itemPrice, CONFIG_MDB_SCALE_FACTOR, CONFIG_MDB_DECIMAL_PLACES), 1, 2);

	esp_fill_random(payload + 1, sizeof(my_passkey));

	time_t now = time(NULL);

    payload[0] = cmd;

	payload[1] = 0x01; 				// version v1
	payload[2] = itemPrice32 >> 24;	// itemPrice
    payload[3] = itemPrice32 >> 16;
	payload[4] = itemPrice32 >> 8;
	payload[5] = itemPrice32;
	payload[6] = itemNumber >> 8;	// itemNumber
	payload[7] = itemNumber;
	payload[8]  = now >> 24;		// time (sec)
	payload[9]  = now >> 16;
	payload[10] = now >> 8;
	payload[11] = now;
    payload[12] = paxCounter >> 8;	// paxCounter
	payload[13] = paxCounter;

	int p_len = sizeof(my_passkey) + 1;

	uint8_t chk = 0x00;
	for(int x= 0; x < p_len - 1; x++){
		chk += payload[x];
	}
	payload[p_len - 1] = chk;

	for(int x = 0; x < sizeof(my_passkey); x++){
		payload[x + 1] ^= my_passkey[x];
	}
}

char* calc_crc_16(uint16_t *pCrc, char *uData) {

	uint8_t data = *uData;

	for (uint8_t iBit = 0; iBit < 8; iBit++, data >>= 1) {

		if ((data ^ *pCrc) & 0x01) {

			*pCrc >>= 1;
			*pCrc ^= 0xA001;

		} else
			*pCrc >>= 1;
	}

	return uData;
}

void readTelemetryDEX() {

	uart_set_baudrate(UART_NUM_1, 9600);
    // uart_set_line_inverse(UART_NUM_1, UART_SIGNAL_RXD_INV | UART_SIGNAL_TXD_INV);

	// -------------------------------------- First Handshake --------------------------------------
	uint8_t data[32];

	// ENQ ->
	uart_write_bytes(UART_NUM_1, "\x05", 1);

	// DLE 0 <-
	uart_read_bytes(UART_NUM_1, &data, 2, pdMS_TO_TICKS(100));
	if( data[0] != 0x10 || data[1] != '0' ) return;

	// DLE SOH ->
	uart_write_bytes(UART_NUM_1, "\x10\x01", 2);

	uint16_t crc = 0x0000;

	// Communication ID
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "1"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "2"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "3"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "4"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "5"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "6"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "7"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "8"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "9"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "0"), 1 );
	// Operation Request
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "R"), 1 );
	// Revision & Level
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "R"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "0"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "0"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "L"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "0"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "6"), 1 );

	uart_write_bytes( UART_NUM_1, "\x10", 1 );						// DLE
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x03"), 1 ); 	// ETX

	data[0] = crc % 256;
	data[1] = crc / 256;
	uart_write_bytes( UART_NUM_1, &data, 2 );

	// DLE 1 <-
	uart_read_bytes(UART_NUM_1, &data, 2, pdMS_TO_TICKS(100));
	if( data[0] != 0x10 || data[1] != '1' ) return;

	// EOT ->
	uart_write_bytes(UART_NUM_1, "\x04", 1);

	// -------------------------------------- Second Handshake --------------------------------------

	// ENQ <-
	uart_read_bytes(UART_NUM_1, &data, 1, pdMS_TO_TICKS(100));
	if( data[0] != 0x05 ) return;

	// DLE 0 ->
	uart_write_bytes(UART_NUM_1, "\x10\x30", 2);

	// DLE SOH <-
	uart_read_bytes(UART_NUM_1, &data, 2, pdMS_TO_TICKS(100));
	if( data[0] != 0x10 || data[1] != 0x01 ) return;

	// Response Code <-
	uart_read_bytes(UART_NUM_1, &data, 2, pdMS_TO_TICKS(100));
	// Communication ID <-
	uart_read_bytes(UART_NUM_1, &data, 10, pdMS_TO_TICKS(100));
	// Revision & Level <-
	uart_read_bytes(UART_NUM_1, &data, 6, pdMS_TO_TICKS(100));

	// DLE ETX <-
	uart_read_bytes(UART_NUM_1, &data, 2, pdMS_TO_TICKS(100));
	if( data[0] != 0x10 || data[1] != 0x03 ) return;

	// CRC <-
	uart_read_bytes(UART_NUM_1, &data, 2, pdMS_TO_TICKS(100));

	// DLE 1 ->
	uart_write_bytes(UART_NUM_1, "\x10\x31", 2);

	// EOT <-
	uart_read_bytes(UART_NUM_1, &data, 1, pdMS_TO_TICKS(100));
	if( data[0] != 0x04 ) return;

	// -------------------------------------- Data transfer --------------------------------------

	// ENQ <-
	uart_read_bytes(UART_NUM_1, &data, 1, pdMS_TO_TICKS(100));
	if (data[0] != 0x05) return;

	uint8_t block = 0x00;
	for (;;) {

		data[0] = 0x10; 				// DLE
		data[1] = ('0' + (block++ & 1)); 	// '0'|'1' ->
		uart_write_bytes(UART_NUM_1, &data, 2);

		// DLE STX <-
		uart_read_bytes(UART_NUM_1, &data, 2, pdMS_TO_TICKS(200));
		if (data[0] != 0x10 || data[1] != 0x02) return;

		for (;;) {

			uart_read_bytes(UART_NUM_1, &data, 1, pdMS_TO_TICKS(200));
			if (data[0] == 0x10) { // DLE

				uart_read_bytes(UART_NUM_1, &data, 1, pdMS_TO_TICKS(200));

				if (data[0] == 0x17) { // ETB

					// <- CRC
					uart_read_bytes(UART_NUM_1, &data, 2, pdMS_TO_TICKS(200));

					break;

				} else if (data[0] == 0x03) { // ETX

					// CRC <-
					uart_read_bytes(UART_NUM_1, &data, 2, pdMS_TO_TICKS(200));

					data[0] = 0x10; 					// DLE
					data[1] = ('0' + (block++ & 0x01)); 	// '0'|'1' ->
					uart_write_bytes(UART_NUM_1, &data, 2);

					// EOT <-
					uart_read_bytes(UART_NUM_1, &data, 1, pdMS_TO_TICKS(200));

					return;
				}
			}

			xRingbufferSend(dexRingbuf, &data[0], 1, 0);
		}
	}
}

void readTelemetryDDCMP() {

	uart_set_baudrate(UART_NUM_1, 2400);
    // uart_set_line_inverse(UART_NUM_1, UART_SIGNAL_RXD_INV | UART_SIGNAL_TXD_INV);

	//-------------------------------------------------------
	uint8_t buffer_rx[1024];
	uint8_t seq_rr_ddcmp;
	uint8_t seq_xx_ddcmp = 0;
	uint32_t n_bytes_message;
	uint16_t crc = 0x0000;
	uint8_t last_package;

	uint8_t crc_[2];

	// start...
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x05"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x06"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x40"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x00"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x00"), 1 ); // mbd
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x01"), 1 ); // sadd
	crc_[0] = crc % 256;
	crc_[1] = crc / 256;
	uart_write_bytes( UART_NUM_1, &crc_, 2 );

	if( uart_read_bytes(UART_NUM_1, &buffer_rx, 8, pdMS_TO_TICKS(200)) != 8)
		return;

	if ((buffer_rx[0] != 0x05) || (buffer_rx[1] != 0x07)) {
		return;
	} // ...stack

	crc = 0x0000;

	// data message header...
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x81"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x10"), 1 ); // nn
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x40"), 1 ); // mm
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x00"), 1 ); // rr
	++seq_xx_ddcmp;
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, (char*) &seq_xx_ddcmp), 1 ); // xx
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x01"), 1 ); // sadd
	crc_[0] = crc % 256;
	crc_[1] = crc / 256;
	uart_write_bytes( UART_NUM_1, &crc_, 2 );

	crc = 0x0000;
	// who are you...
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x77"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\xe0"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x00"), 1 );

	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x00"), 1 ); // security code
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x00"), 1 );

	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x00"), 1 ); // pass code
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x00"), 1 );

	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x01"), 1 ); // date dd mm yy
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x01"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x70"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x00"), 1 ); // time hh mm ss
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x00"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x00"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x00"), 1 ); // u2
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x00"), 1 ); // u1
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x0c"), 1 ); // 0b-Maintenance 0c-Route Person

	crc_[0] = crc % 256;
	crc_[1] = crc / 256;
	uart_write_bytes( UART_NUM_1, &crc_, 2 );

	if( uart_read_bytes(UART_NUM_1, &buffer_rx, 8, pdMS_TO_TICKS(200)) != 8)
		return;

	if ((buffer_rx[0] != 0x05) || (buffer_rx[1] != 0x01)) {
		return;
	} // ...ack

	if( uart_read_bytes(UART_NUM_1, &buffer_rx, 8, pdMS_TO_TICKS(200)) != 8)
		return;

	if (buffer_rx[0] != 0x81) {
		return;
	} // ...data message header
//
	seq_rr_ddcmp = buffer_rx[4];
//
	n_bytes_message = ((buffer_rx[2] & 0x3f) * 256) + buffer_rx[1];
	n_bytes_message += 2; // crc16

	if( uart_read_bytes(UART_NUM_1, &buffer_rx, n_bytes_message, pdMS_TO_TICKS(200)) != n_bytes_message)
		return;

//  if (buffer_rx[2] != 0x01) {
//    return;
//  } ...not rejected

	crc = 0x0000;

	// ack...
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x05"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x01"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x40"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, (char*) &seq_rr_ddcmp), 1 ); 	// rr
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x00"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x01"), 1 ); 					// sadd
	crc_[0] = crc % 256;
	crc_[1] = crc / 256;
	uart_write_bytes( UART_NUM_1, &crc_, 2 ); // Transmitiu ACK (05 01 40 01 00 01 B8 55)

	crc = 0x0000;

	// data message header...
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x81"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x09"), 1 ); 					// nn
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x40"), 1 ); 					// mm
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, (char*) &seq_rr_ddcmp), 1 ); 	// rr
	++seq_xx_ddcmp;
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, (char*) &seq_xx_ddcmp), 1 ); 	// xx
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x01"), 1 ); 					// sadd
	crc_[0] = crc % 256;
	crc_[1] = crc / 256;
	uart_write_bytes( UART_NUM_1, &crc_, 2 ); // Transmitiu DATA_HEADER (81 09 40 01 02 01 46 B0)

	crc = 0x0000;

	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x77"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\xE2"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x00"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x02"), 1 ); // security read list (Standard audit data is read without resetting the interim data. (Read only) )
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x01"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x00"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x00"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x00"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x00"), 1 );
	crc_[0] = crc % 256;
	crc_[1] = crc / 256;
	uart_write_bytes( UART_NUM_1, &crc_, 2 ); // Transmitiu READ_DATA/Audit Collection List (77 E2 00 01 01 00 00 00 00 F0 72)

	if( uart_read_bytes(UART_NUM_1, &buffer_rx, 8, pdMS_TO_TICKS(200)) != 8)
		return;

	if ((buffer_rx[0] != 0x05) || (buffer_rx[1] != 0x01)) {
		return;
	} // ...ack

	if( uart_read_bytes(UART_NUM_1, &buffer_rx, 8, pdMS_TO_TICKS(200)) != 8)
		return;

	if (buffer_rx[0] != 0x81) {
		return;
	} // DATA HEADER

	seq_rr_ddcmp = buffer_rx[4];

	n_bytes_message = ((buffer_rx[2] & 0x3f) * 256) + buffer_rx[1];
	n_bytes_message += 2; // crc16

	if( uart_read_bytes(UART_NUM_1, &buffer_rx, n_bytes_message, pdMS_TO_TICKS(200)) != n_bytes_message)
		return;

	if (buffer_rx[2] != 0x01) {
		return;
	}

	crc = 0x0000;

	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x05"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x01"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x40"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, (char*) &seq_rr_ddcmp), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x00"), 1 );
	uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x01"), 1 );
	crc_[0] = crc % 256;
	crc_[1] = crc / 256;
	uart_write_bytes( UART_NUM_1, &crc_, 2 ); // Transmitiu ACK

	do {

		if( uart_read_bytes(UART_NUM_1, &buffer_rx, 8, pdMS_TO_TICKS(200)) != 8)
			break;

		if (buffer_rx[0] != 0x81) {
			break;
		} // ...data header

		seq_rr_ddcmp = buffer_rx[4];
		last_package = buffer_rx[2] & 0x80;

		n_bytes_message = ((buffer_rx[2] & 0x3f) * 256) + buffer_rx[1];
		n_bytes_message += 2; // crc16

		if( uart_read_bytes(UART_NUM_1, &buffer_rx, n_bytes_message, pdMS_TO_TICKS(200)) != n_bytes_message)
			break;
		// ...data

		// Os dados recebidos são: 99 nn "audit dada" crc crc, ou seja, as informaões de audit estão da posição 2 do buffer_rx à posição n_bytes-3
		for (int x = 2; x < n_bytes_message - 2; x++)
			xRingbufferSend(dexRingbuf, &buffer_rx[x], 1, 0);

		crc = 0x0000;

		uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x05"), 1 );
		uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x01"), 1 );
		uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x40"), 1 );
		uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, (char*) &seq_rr_ddcmp), 1 );
		uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x00"), 1 );
		uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x01"), 1 );
		crc_[0] = crc % 256;
		crc_[1] = crc / 256;
		uart_write_bytes( UART_NUM_1, &crc_, 2 ); // Transmitiu ACK

		if (last_package) {

			crc = 0x0000;

			uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x81"), 1 );
			uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x02"), 1 );					// nn
			uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x40"), 1 ); 					// mm
			uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, (char*) &seq_rr_ddcmp), 1 ); 	// rr
			uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x03"), 1 ); 					// xx
			uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x01"), 1 ); 					// sadd
			crc_[0] = crc % 256;
			crc_[1] = crc / 256;
			uart_write_bytes( UART_NUM_1, &crc_, 2 ); // Transmitiu DATA HEADER

			uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x77"), 1 );
			uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\xFF"), 1 );
			uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\x67"), 1 );
			uart_write_bytes( UART_NUM_1, calc_crc_16(&crc, "\xB0"), 1 ); // Transmitiu FINIS

			if( uart_read_bytes(UART_NUM_1, &buffer_rx, 8, pdMS_TO_TICKS(200)) != 8)
				break;

			if ((buffer_rx[0] != 0x05) || (buffer_rx[1] != 0x01)) {
				break;
			} // ACK

			break;
		}

		vTaskDelay(pdMS_TO_TICKS(10)); // ticks para ms

	} while(1);
}

void requestTelemetryData(void *arg) {

	readTelemetryDDCMP();
	readTelemetryDEX();

	size_t dex_size;
	uint8_t *dex = (uint8_t*) xRingbufferReceive(dexRingbuf, &dex_size, 0);

  	char topic[128];
	snprintf(topic, sizeof(topic), "/%s/%s/dex", my_company_id, my_device_id);

    esp_mqtt_client_publish(mqttClient, topic, (char*) dex, dex_size, 0, 0);
    printf("%.*s", dex_size, (char*) dex);

    vRingbufferReturnItem(dexRingbuf, (void*) dex);
}

void vTaskBitEvent(void *pvParameters) {

    while(1){
        EventBits_t uxBits = xEventGroupWaitBits(xLedEventGroup, BIT_EVT_TRIGGER, pdTRUE, pdFALSE, portMAX_DELAY );

        if ((uxBits & MASK_EVT_INSTALLED) != MASK_EVT_INSTALLED) {
            led_strip_set_pixel(led_strip, 0, 80, 60, 0);   // Not installed := YELLOW
        } else if ((uxBits & BIT_EVT_MDB) && (uxBits & BIT_EVT_INTERNET)) {
            led_strip_set_pixel(led_strip, 0, 10, 80, 10);  // MDB & Internet := GREEN
        } else if (uxBits & BIT_EVT_MDB) {
            led_strip_set_pixel(led_strip, 0, 5, 15, 80);   // Only MDB := BLUE
        } else {
            led_strip_set_pixel(led_strip, 0, 80, 5, 5);    // Inactive/Disabled := RED
        }
        led_strip_refresh(led_strip);

        if(uxBits & BIT_EVT_BUZZER){

            gpio_set_level(PIN_BUZZER_PWR, 1);
            vTaskDelay(pdMS_TO_TICKS(1000));
            gpio_set_level(PIN_BUZZER_PWR, 0);

            xEventGroupClearBits(xLedEventGroup, BIT_EVT_BUZZER);
        }
    }
}

void ble_pax_event_handler(uint16_t devices_count){

    uint8_t payload[19];
    xorEncodeWithPasskey(0x22, 0, 0, devices_count, (uint8_t*) &payload);

    char topic[128];
    snprintf(topic, sizeof(topic), "/%s/%s/paxcounter", my_company_id, my_device_id);

    esp_mqtt_client_publish(mqttClient, topic, (char*) &payload, sizeof(payload), 1, 0);
}

void ble_event_handler(char *ble_payload) {

    printf("ble_event_handler %x\n", (uint8_t) ble_payload[0]);

	switch ( (uint8_t) ble_payload[0] ) {
    case 0x00: {
        nvs_handle_t handle;
		ESP_ERROR_CHECK( nvs_open("vmflow", NVS_READWRITE, &handle) );

		size_t s_len;
		if (nvs_get_str(handle, "device_id", NULL, &s_len) != ESP_OK) {

			strcpy((char*) &my_device_id, ble_payload + 1);

			ESP_ERROR_CHECK( nvs_set_str(handle, "device_id", (char*) &my_device_id) );
			ESP_ERROR_CHECK( nvs_commit(handle) );

			char myhost[64];
			snprintf(myhost, sizeof(myhost), "%s.vmflow.xyz", my_device_id);

			ble_set_device_name((char*) &myhost);

            xEventGroupSetBits(xLedEventGroup, BIT_EVT_DOMAIN | BIT_EVT_TRIGGER);

			ESP_LOGI( TAG, "HOST= %s", myhost);
		}
		nvs_close(handle);
		break;
    }
    case 0x01: {
        nvs_handle_t handle;
		ESP_ERROR_CHECK( nvs_open("vmflow", NVS_READWRITE, &handle) );

		size_t s_len;
		if (nvs_get_str(handle, "passkey", NULL, &s_len) != ESP_OK) {

			strcpy((char*) &my_passkey, ble_payload + 1);

			ESP_ERROR_CHECK( nvs_set_str(handle, "passkey", (char*) &my_passkey) );
			ESP_ERROR_CHECK( nvs_commit(handle) );

            xEventGroupSetBits(xLedEventGroup, BIT_EVT_PSSKEY | BIT_EVT_TRIGGER);

			ESP_LOGI( TAG, "PASSKEY= %s", my_passkey);
		}
		nvs_close(handle);

        break;
    }
    case 0x02: /*Starting a vending session*/
        uint16_t fundsAvailable = 0xffff;
		xQueueSend(mdbSessionQueue, &fundsAvailable, 0 /*if full, do not wait*/);
		break;
	case 0x03: /*Approve the vending session*/

        if(xorDecodeWithPasskey(NULL, NULL, (uint8_t*) ble_payload)){
            vend_approved_todo = (machine_state == VEND_STATE) ? true : false;
        }
        break;
    case 0x04: /*Close the vending session*/
    	session_cancel_todo = (machine_state >= IDLE_STATE) ? true : false;
        break;
    case 0x05:
        // Not implemented
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

/* ---------- OTA firmware update ----------
 * Spawned as a FreeRTOS task when an OTA command is received via MQTT.
 * Downloads a firmware binary from the given URL using esp_https_ota,
 * then reboots into the new firmware.
 */
static void ota_update_task(void *arg) {
    char *url = (char *)arg;
    ESP_LOGW(TAG, "OTA: starting firmware download from %s", url);

    /* Publish OTA-in-progress status */
    char topic[128];
    snprintf(topic, sizeof(topic), "/%s/%s/status", my_company_id, my_device_id);
    esp_mqtt_client_publish(mqttClient, topic, "ota_updating", 0, 1, 0);

    bool use_tls = (strncmp(url, "https://", 8) == 0);

    esp_http_client_config_t http_cfg = {
        .url               = url,
        .crt_bundle_attach = use_tls ? esp_crt_bundle_attach : NULL,
        .timeout_ms        = 60000,
        .buffer_size       = 1024,
        .buffer_size_tx    = 1024,
    };

    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };

    esp_err_t ret = esp_https_ota(&ota_cfg);

    if (ret == ESP_OK) {
        ESP_LOGW(TAG, "OTA: firmware update successful — restarting");
        esp_mqtt_client_publish(mqttClient, topic, "ota_success", 0, 1, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));
        free(url);
        esp_restart();
        /* not reached */
    } else {
        ESP_LOGE(TAG, "OTA: firmware update failed: %s", esp_err_to_name(ret));
        esp_mqtt_client_publish(mqttClient, topic, "ota_failed", 0, 1, 0);
    }

    ota_in_progress = false;
    free(url);
    vTaskDelete(NULL);
}

// Periodic MDB diagnostics publishing via MQTT (called from esp_timer)
static void mdb_diag_timer_cb(void *arg) {
    if (!mqttClient) return;

    char topic[128];
    snprintf(topic, sizeof(topic), "/%s/%s/mdb-log", my_company_id, my_device_id);

    char msg[256];
    snprintf(msg, sizeof(msg),
        "{\"state\":\"%s\",\"addr\":\"0x%02X\",\"polls\":%lu,\"chkErr\":%lu,\"lastCmd\":\"%s\"}",
        machine_state_name(machine_state),
        cashless_device_address,
        mdb_poll_count,
        mdb_checksum_errors,
        mdb_last_cmd);

    esp_mqtt_client_publish(mqttClient, topic, msg, 0, 0, 0);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {

	esp_mqtt_event_handle_t event = event_data;
	esp_mqtt_client_handle_t mqttClient = event->client;

	switch ((esp_mqtt_event_id_t) event_id) {
	case MQTT_EVENT_CONNECTED:
		ESP_LOGW(TAG, "MQTT: connected to broker");

    	char topic[128];
    	snprintf(topic, sizeof(topic), "/%s/%s/credit", my_company_id, my_device_id);
		ESP_LOGI(TAG, "MQTT: subscribing to '%s'", topic);
    	esp_mqtt_client_subscribe(mqttClient, topic, 0);

    	char topic_ota[128];
    	snprintf(topic_ota, sizeof(topic_ota), "/%s/%s/ota", my_company_id, my_device_id);
		ESP_LOGI(TAG, "MQTT: subscribing to '%s'", topic_ota);
    	esp_mqtt_client_subscribe(mqttClient, topic_ota, 1);

    	char topic_config[128];
    	snprintf(topic_config, sizeof(topic_config), "/%s/%s/config", my_company_id, my_device_id);
		ESP_LOGI(TAG, "MQTT: subscribing to '%s'", topic_config);
    	esp_mqtt_client_subscribe(mqttClient, topic_config, 1);

    	char topic_[128];
    	snprintf(topic_, sizeof(topic_), "/%s/%s/status", my_company_id, my_device_id);

		const esp_app_desc_t *app_desc = esp_app_get_description();
		char status_msg[128];
		snprintf(status_msg, sizeof(status_msg), "online|v:%s|b:%s %s %s", app_desc->version, app_desc->date, app_desc->time, BUILD_TIMEZONE);
		ESP_LOGI(TAG, "MQTT: publishing '%s' to '%s'", status_msg, topic_);
		esp_mqtt_client_publish(mqttClient, topic_, status_msg, 0, 1, 1);

        xEventGroupSetBits(xLedEventGroup, BIT_EVT_INTERNET | BIT_EVT_TRIGGER);

        // Start MDB diagnostics timer (30s interval)
        {
            static esp_timer_handle_t mdb_diag_timer = NULL;
            if (!mdb_diag_timer) {
                const esp_timer_create_args_t args = {
                    .callback = mdb_diag_timer_cb,
                    .name = "mdb_diag"
                };
                if (esp_timer_create(&args, &mdb_diag_timer) == ESP_OK) {
                    esp_timer_start_periodic(mdb_diag_timer, 30 * 1000000ULL); // 30s
                    ESP_LOGI(TAG, "MDB DIAG: timer started (30s interval)");
                }
            }
        }

		break;
	case MQTT_EVENT_DISCONNECTED:
		ESP_LOGW(TAG, "MQTT: disconnected from broker");
        xEventGroupClearBits(xLedEventGroup, BIT_EVT_INTERNET);
        xEventGroupSetBits(xLedEventGroup, BIT_EVT_TRIGGER);

		break;
	case MQTT_EVENT_SUBSCRIBED:
		ESP_LOGI(TAG, "MQTT: subscribed, msg_id=%d", event->msg_id);
		break;
	case MQTT_EVENT_UNSUBSCRIBED:
		ESP_LOGI(TAG, "MQTT: unsubscribed, msg_id=%d", event->msg_id);
		break;
	case MQTT_EVENT_PUBLISHED:
		ESP_LOGI(TAG, "MQTT: published, msg_id=%d", event->msg_id);
		break;
	case MQTT_EVENT_DATA:

	    ESP_LOGI( TAG, "TOPIC= %.*s", event->topic_len, event->topic);
	    ESP_LOGI( TAG, "DATA_LEN= %d", event->data_len);
	    ESP_LOGI( TAG, "DATA= %.*s", event->data_len, event->data);

		size_t topic_len = event->topic_len;

		if (topic_len > 7 && strncmp(event->topic + event->topic_len - 7, "/credit", 7) == 0) {

			uint16_t fundsAvailable;
			if(xorDecodeWithPasskey(&fundsAvailable, NULL, (uint8_t*) event->data)){
			    xQueueSend(mdbSessionQueue, &fundsAvailable, 0 /*if full, do not wait*/);

                xEventGroupSetBits(xLedEventGroup, BIT_EVT_BUZZER | BIT_EVT_TRIGGER);

                ESP_LOGI( TAG, "Amount= %f", FROM_SCALE_FACTOR(fundsAvailable, CONFIG_MDB_SCALE_FACTOR, CONFIG_MDB_DECIMAL_PLACES) );
			}
		}

		/* OTA firmware update — expects JSON: {"url":"https://server/path/to/firmware.bin"} */
		if (topic_len > 4 && strncmp(event->topic + event->topic_len - 4, "/ota", 4) == 0) {

		    if (ota_in_progress) {
		        ESP_LOGW(TAG, "OTA: update already in progress, ignoring");
		        break;
		    }

		    /* Null-terminate the incoming data so we can parse it as a string */
		    char *json_buf = malloc(event->data_len + 1);
		    if (!json_buf) {
		        ESP_LOGE(TAG, "OTA: malloc failed");
		        break;
		    }
		    memcpy(json_buf, event->data, event->data_len);
		    json_buf[event->data_len] = '\0';

		    cJSON *root = cJSON_Parse(json_buf);
		    free(json_buf);

		    if (!root) {
		        ESP_LOGE(TAG, "OTA: failed to parse JSON");
		        break;
		    }

		    cJSON *j_url = cJSON_GetObjectItem(root, "url");
		    if (!j_url || !cJSON_IsString(j_url) || strlen(j_url->valuestring) == 0) {
		        ESP_LOGE(TAG, "OTA: missing or empty 'url' field");
		        cJSON_Delete(root);
		        break;
		    }

		    /* Validate URL starts with the trusted server URL (prevents arbitrary downloads) */
		    if (strlen(my_srv_url) > 0 && strncmp(j_url->valuestring, my_srv_url, strlen(my_srv_url)) != 0) {
		        ESP_LOGE(TAG, "OTA: URL '%s' does not match trusted server '%s'", j_url->valuestring, my_srv_url);
		        cJSON_Delete(root);
		        break;
		    }

		    /* Copy URL for the OTA task (freed inside the task) */
		    char *ota_url = strdup(j_url->valuestring);
		    cJSON_Delete(root);

		    if (!ota_url) {
		        ESP_LOGE(TAG, "OTA: strdup failed");
		        break;
		    }

		    ESP_LOGW(TAG, "OTA: received update command, URL=%s", ota_url);
		    ota_in_progress = true;
		    xTaskCreate(ota_update_task, "ota_update", 8192, ota_url, 5, NULL);
		}

		/* Device config — expects JSON: {"mdb_address": 1} or {"mdb_address": 2} */
		if (topic_len > 7 && strncmp(event->topic + event->topic_len - 7, "/config", 7) == 0) {

		    char *json_buf = malloc(event->data_len + 1);
		    if (!json_buf) {
		        ESP_LOGE(TAG, "CONFIG: malloc failed");
		        break;
		    }
		    memcpy(json_buf, event->data, event->data_len);
		    json_buf[event->data_len] = '\0';

		    cJSON *root = cJSON_Parse(json_buf);
		    free(json_buf);

		    if (!root) {
		        ESP_LOGE(TAG, "CONFIG: failed to parse JSON");
		        break;
		    }

		    bool needs_restart = false;

		    cJSON *j_mdb = cJSON_GetObjectItem(root, "mdb_address");
		    if (j_mdb && cJSON_IsNumber(j_mdb)) {
		        int addr = j_mdb->valueint;
		        if (addr == 1 || addr == 2) {
		            nvs_handle_t h;
		            if (nvs_open("vmflow", NVS_READWRITE, &h) == ESP_OK) {
		                nvs_set_u8(h, "mdb_addr", (uint8_t) addr);
		                nvs_commit(h);
		                nvs_close(h);
		                ESP_LOGW(TAG, "CONFIG: mdb_address set to %d, restart required", addr);
		                needs_restart = true;
		            }
		        } else {
		            ESP_LOGE(TAG, "CONFIG: invalid mdb_address %d (must be 1 or 2)", addr);
		        }
		    }

		    cJSON_Delete(root);

		    if (needs_restart) {
		        ESP_LOGW(TAG, "CONFIG: restarting to apply new configuration...");
		        vTaskDelay(pdMS_TO_TICKS(500));
		        esp_restart();
		    }
		}

		break;
	case MQTT_EVENT_ERROR:
		ESP_LOGE(TAG, "MQTT: error event");
		if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
		    ESP_LOGE(TAG, "MQTT: transport error: esp_tls_last_esp_err=0x%x, tls_stack_err=0x%x, sock_errno=%d (%s)",
		             event->error_handle->esp_tls_last_esp_err,
		             event->error_handle->esp_tls_stack_err,
		             event->error_handle->esp_transport_sock_errno,
		             strerror(event->error_handle->esp_transport_sock_errno));
		} else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
		    ESP_LOGE(TAG, "MQTT: connection refused, reason=0x%x", event->error_handle->connect_return_code);
		}
		break;
	default:
	    ESP_LOGI( TAG, "Other event id: %d", event->event_id);
		break;
	}
}

/* ---------- Provisioning claim ----------
 * Called as a FreeRTOS task after the device obtains an IP address and a
 * prov_code is found in NVS.  POSTs to the claim-device edge function,
 * persists the returned subdomain + passkey, erases the one-time code, then
 * reboots so the regular startup path takes over.
 */
static void provision_claim_task(void *arg) {
    char prov_code[32] = {0};
    char srv_url[128]  = {0};

    nvs_handle_t handle;
    if (nvs_open("vmflow", NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "PROV: NVS open failed");
        vTaskDelete(NULL);
        return;
    }

    size_t len = sizeof(prov_code);
    if (nvs_get_str(handle, "prov_code", prov_code, &len) != ESP_OK || strlen(prov_code) == 0) {
        ESP_LOGI(TAG, "PROV: no provisioning code in NVS");
        nvs_close(handle);
        vTaskDelete(NULL);
        return;
    }

    len = sizeof(srv_url);
    nvs_get_str(handle, "srv_url", srv_url, &len);
    nvs_close(handle);

    if (strlen(srv_url) == 0) {
        ESP_LOGE(TAG, "PROV: no server URL in NVS");
        vTaskDelete(NULL);
        return;
    }

    /* MAC address */
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    /* Build request */
    char body[128];
    snprintf(body, sizeof(body),
             "{\"short_code\":\"%s\",\"mac_address\":\"%s\"}", prov_code, mac_str);

    char url[192];
    snprintf(url, sizeof(url), "%s/functions/v1/claim-device", srv_url);

    ESP_LOGI(TAG, "PROV: claiming device at %s body=%s", url, body);

    char resp_buf[512] = {0};

    bool use_tls = (strncmp(url, "https://", 8) == 0);

    esp_http_client_config_t http_cfg = {
        .url               = url,
        .method            = HTTP_METHOD_POST,
        .crt_bundle_attach = use_tls ? esp_crt_bundle_attach : NULL,
        .timeout_ms        = 15000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    esp_err_t http_err = esp_http_client_open(client, strlen(body));
    if (http_err == ESP_OK) {
        esp_http_client_write(client, body, strlen(body));
        int content_length = esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);

        int read_len = esp_http_client_read_response(client, resp_buf, sizeof(resp_buf) - 1);
        if (read_len >= 0) resp_buf[read_len] = '\0';

        ESP_LOGW(TAG, "PROV: HTTP %d  body=%s", status, resp_buf);

        if (status == 200) {
            cJSON *root = cJSON_Parse(resp_buf);
            if (root) {
                cJSON *j_company = cJSON_GetObjectItem(root, "company_id");
                cJSON *j_device  = cJSON_GetObjectItem(root, "device_id");
                cJSON *j_pass    = cJSON_GetObjectItem(root, "passkey");

                if (j_company && cJSON_IsString(j_company) &&
                    j_device  && cJSON_IsString(j_device) &&
                    j_pass    && cJSON_IsString(j_pass)) {

                    nvs_handle_t h;
                    if (nvs_open("vmflow", NVS_READWRITE, &h) == ESP_OK) {
                        nvs_set_str(h, "company_id", j_company->valuestring);
                        nvs_set_str(h, "device_id",  j_device->valuestring);
                        nvs_set_str(h, "passkey",    j_pass->valuestring);
                        cJSON *j_mqtt = cJSON_GetObjectItem(root, "mqtt_host");
                        if (j_mqtt && cJSON_IsString(j_mqtt) && strlen(j_mqtt->valuestring) > 0) {
                            nvs_set_str(h, "mqtt_host", j_mqtt->valuestring);
                        }
                        cJSON *j_mqtt_port = cJSON_GetObjectItem(root, "mqtt_port");
                        if (j_mqtt_port && cJSON_IsString(j_mqtt_port) && strlen(j_mqtt_port->valuestring) > 0) {
                            nvs_set_str(h, "mqtt_port", j_mqtt_port->valuestring);
                        }
                        nvs_erase_key(h, "prov_code");
                        nvs_commit(h);
                        nvs_close(h);
                    }

                    ESP_LOGI(TAG, "PROV: claimed, company=%s device=%s — restarting",
                             j_company->valuestring, j_device->valuestring);
                    cJSON_Delete(root);
                    esp_http_client_cleanup(client);
                    vTaskDelay(pdMS_TO_TICKS(500));
                    esp_restart();
                    /* not reached */
                }
                cJSON_Delete(root);
            }
        }
    } else {
        ESP_LOGE(TAG, "PROV: HTTP error: %s", esp_err_to_name(http_err));
    }

    esp_http_client_cleanup(client);
    vTaskDelete(NULL);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {

	if (event_base == WIFI_EVENT)
		switch (event_id) {
		case WIFI_EVENT_STA_START: {

		    wifi_retry_num = 0;
		    ESP_LOGI(TAG, "WIFI STA started");

		    /* Check whether there is a saved SSID before attempting to connect.
		     * esp_wifi_connect() with an empty SSID may return ESP_OK on some
		     * IDF versions without ever emitting STA_DISCONNECTED, leaving the
		     * SoftAP fallback unreachable. */
		    wifi_config_t sta_cfg = {0};
		    esp_wifi_get_config(WIFI_IF_STA, &sta_cfg);
		    ESP_LOGI(TAG, "Saved SSID: \"%s\"", (char *)sta_cfg.sta.ssid);

		    if (strlen((char *)sta_cfg.sta.ssid) == 0) {
		        ESP_LOGI(TAG, "No saved WiFi credentials — starting SoftAP");
		        start_softap();
		        start_dns_server();
		        start_rest_server();
		    } else {
		        esp_err_t conn_err = esp_wifi_connect();
		        ESP_LOGI(TAG, "esp_wifi_connect() → %s", esp_err_to_name(conn_err));
		        if (conn_err != ESP_OK) {
		            ESP_LOGW(TAG, "Connect failed immediately — starting SoftAP");
		            start_softap();
		            start_dns_server();
		            start_rest_server();
		        }
		    }
			break;
		}
		case WIFI_EVENT_AP_START:
		    ESP_LOGI(TAG, "WIFI AP started — SSID \"" AP_SSID "\" should now be visible");
		    break;
		case WIFI_EVENT_AP_STOP:
		    ESP_LOGI(TAG, "WIFI AP stopped");
		    break;
		case WIFI_EVENT_AP_STACONNECTED:
		    ESP_LOGI(TAG, "Client connected to SoftAP");
		    break;
		case WIFI_EVENT_STA_CONNECTED:
			break;
		case WIFI_EVENT_STA_DISCONNECTED:

		    if(wifi_retry_num++ < WIFI_MAX_RETRY) {

		        if (mqtt_started) {
                    esp_mqtt_client_disconnect(mqttClient);

                    mqtt_started = false;
                }

                esp_wifi_connect();

		    } else {

		        start_softap();
                start_dns_server();
                start_rest_server();
		    }

			break;
		}

	if (event_base == IP_EVENT)
		switch (event_id) {
		case IP_EVENT_STA_GOT_IP: {

		    ip_event_got_ip_t *event_ip = (ip_event_got_ip_t *)event_data;
		    ESP_LOGW(TAG, "GOT IP: " IPSTR, IP2STR(&event_ip->ip_info.ip));

		    wifi_retry_num = 0;

            stop_rest_server();
            stop_dns_server();

            // Switch to STA-only mode now that we have a connection
            esp_wifi_set_mode(WIFI_MODE_STA);

            /* If a provisioning code is waiting, claim the device first.
             * The claim task will call esp_restart() on success, so normal
             * MQTT startup is intentionally skipped here. */
            {
                nvs_handle_t pnvs;
                size_t pc_len = 0;
                bool needs_prov = false;
                if (nvs_open("vmflow", NVS_READONLY, &pnvs) == ESP_OK) {
                    needs_prov = (nvs_get_str(pnvs, "prov_code", NULL, &pc_len) == ESP_OK && pc_len > 1);
                    nvs_close(pnvs);
                }
                if (needs_prov) {
                    ESP_LOGW(TAG, "Provisioning pending — spawning claim task");
                    xTaskCreate(provision_claim_task, "prov_claim", 8192, NULL, 5, NULL);
                    break;
                }
            }

		    if (!mqtt_started) {
		        ESP_LOGW(TAG, "MQTT: starting client (company='%s' device='%s', client=%p)", my_company_id, my_device_id, mqttClient);
                esp_mqtt_client_start(mqttClient);
                mqtt_started = true;
            } else {
                ESP_LOGW(TAG, "MQTT: already started, skipping");
            }

            if (!sntp_started) {
                esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
                esp_sntp_setservername(0, "pool.ntp.org");
                esp_sntp_init();

                sntp_started = true;
            }

			break;
		}
		}
}

void request_pax_counter(void *arg) {
    ble_scan_start(PAX_SCAN_DURATION_SEC);
}

#define PIN_BOOT_BTN        GPIO_NUM_0
#define FACTORY_RESET_MS    5000  // hold for 5 seconds to trigger reset

static void factory_reset_task(void *arg) {
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << PIN_BOOT_BTN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn_cfg);

    while (1) {
        // Wait for button press (active low)
        if (gpio_get_level(PIN_BOOT_BTN) == 0) {
            TickType_t press_start = xTaskGetTickCount();

            // Wait while held
            while (gpio_get_level(PIN_BOOT_BTN) == 0) {
                TickType_t held_ms = (xTaskGetTickCount() - press_start) * portTICK_PERIOD_MS;
                if (held_ms >= FACTORY_RESET_MS) {
                    ESP_LOGW(TAG, "FACTORY RESET: button held %lu ms — erasing NVS and restarting", held_ms);
                    nvs_flash_erase();
                    esp_wifi_restore();
                    vTaskDelay(pdMS_TO_TICKS(200));
                    esp_restart();
                }
                vTaskDelay(pdMS_TO_TICKS(50));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void app_main(void) {

    gpio_set_direction(PIN_MDB_RX, GPIO_MODE_INPUT);
	gpio_set_direction(PIN_MDB_TX, GPIO_MODE_INPUT);  // idle: high-Z (tri-state)

	gpio_set_direction(PIN_BUZZER_PWR, GPIO_MODE_OUTPUT);
	gpio_set_level(PIN_BUZZER_PWR, 0);

	//---------------- Strip LED configuration -----------------//
	//----------------------------------------------------------//
    xLedEventGroup = xEventGroupCreate();

    led_strip_config_t strip_config = {
        .strip_gpio_num = PIN_MDB_LED,
        .max_leds = 1,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, // 10 MHz → good precision
        .mem_block_symbols = 64,
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));

	//--------------- ADC Init (NTC thermistor) ----------------//
	//----------------------------------------------------------//
    adc_oneshot_unit_handle_t adc_handle;
    adc_oneshot_unit_init_cfg_t init_config = { .unit_id = ADC_UNIT_THERMISTOR, };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));

    adc_oneshot_chan_cfg_t config = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT, };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL_THERMISTOR, &config));

    int adc_raw_value;

    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_CHANNEL_THERMISTOR, &adc_raw_value));
    ESP_LOGI(TAG, "ADC Raw Data: %d", adc_raw_value);

	//---------------- UART1 - EVA DTS DEX/DDCMP ---------------//
	//----------------------------------------------------------//
	uart_config_t uart_config_1 = {
			.baud_rate = 9600,
			.data_bits = UART_DATA_8_BITS,
			.parity = UART_PARITY_DISABLE,
			.stop_bits = UART_STOP_BITS_1,
			.flow_ctrl = UART_HW_FLOWCTRL_DISABLE };

	uart_param_config(UART_NUM_1, &uart_config_1);
	uart_set_pin( UART_NUM_1, PIN_DEX_TX, PIN_DEX_RX, -1, -1);
	uart_driver_install(UART_NUM_1, 256, 256, 0, NULL, 0);

    // ---
    dexRingbuf = xRingbufferCreate(8 * 1024 /*8Kb*/, RINGBUF_TYPE_BYTEBUF);

    const double INTERVAL_12H_US = 12ULL * 60 * 60 * 1000000; // 12h in microseconds

	const esp_timer_create_args_t periodic_timer_args = {
		.callback = &requestTelemetryData,
		.name = "task_dex_12h"
	};

	esp_timer_handle_t periodic_timer;
	esp_timer_create(&periodic_timer_args, &periodic_timer);
	esp_timer_start_periodic(periodic_timer, INTERVAL_12H_US);

	//-------------------- NETWORK STACK -----------------------//
	//----------------------------------------------------------//
	esp_err_t nvs_err = nvs_flash_init();
	if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
	    ESP_LOGW(TAG, "NVS partition invalid (%s), erasing and re-initialising", esp_err_to_name(nvs_err));
	    nvs_flash_erase();
	    nvs_err = nvs_flash_init();
	}
	if (nvs_err != ESP_OK) {
	    ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(nvs_err));
	} else {
	    ESP_LOGI(TAG, "NVS initialised OK");
	}
	//
	esp_netif_init();
	esp_event_loop_create_default();

	esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	esp_wifi_init(&cfg);

	esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL);
	esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL);

	//---------- NVS device config (before WiFi starts) -------//
	//----------------------------------------------------------//
	char myhost[96];
	strcpy(myhost, "0.vmflow.xyz"); // Default value

    nvs_handle_t handle;
	if (nvs_open("vmflow", NVS_READONLY, &handle) == ESP_OK) {

	    size_t s_len = 0;
	    if (nvs_get_str(handle, "passkey", NULL, &s_len) == ESP_OK) {
            nvs_get_str(handle, "passkey", my_passkey, &s_len);

            s_len = 0;
            if (nvs_get_str(handle, "company_id", NULL, &s_len) == ESP_OK) {
                nvs_get_str(handle, "company_id", my_company_id, &s_len);

                s_len = 0;
                if (nvs_get_str(handle, "device_id", NULL, &s_len) == ESP_OK) {
                    nvs_get_str(handle, "device_id", my_device_id, &s_len);

                    snprintf(myhost, sizeof(myhost), "%s.vmflow.xyz", my_device_id);

                    xEventGroupSetBits(xLedEventGroup, BIT_EVT_PSSKEY | BIT_EVT_DOMAIN | BIT_EVT_TRIGGER);
                }
            }
        }

        /* Read srv_url for OTA download validation */
        s_len = sizeof(my_srv_url);
        if (nvs_get_str(handle, "srv_url", my_srv_url, &s_len) == ESP_OK) {
            ESP_LOGI(TAG, "NVS: srv_url = %s", my_srv_url);
        }

        /* Read MDB device address from NVS (1 = 0x10, 2 = 0x60); fall back to Kconfig default */
        {
            uint8_t nvs_mdb_addr = 0;
            if (nvs_get_u8(handle, "mdb_addr", &nvs_mdb_addr) == ESP_OK) {
                if (nvs_mdb_addr == 1) {
                    cashless_device_address = 16;   /* 0x10 */
#ifdef CONFIG_MDB_SNIFF_OTHER_CASHLESS
                    mdb_sniff_address = 96;         /* 0x60 */
#endif
                } else if (nvs_mdb_addr == 2) {
                    cashless_device_address = 96;   /* 0x60 */
#ifdef CONFIG_MDB_SNIFF_OTHER_CASHLESS
                    mdb_sniff_address = 16;         /* 0x10 */
#endif
                }
                ESP_LOGI(TAG, "NVS: mdb_addr = %u (cashless=0x%02X, sniff=0x%02X)",
                         nvs_mdb_addr, cashless_device_address,
#ifdef CONFIG_MDB_SNIFF_OTHER_CASHLESS
                         mdb_sniff_address
#else
                         0
#endif
                );
            }
        }

		nvs_close(handle);
	}

	/* Log firmware version at boot */
	{
	    const esp_app_desc_t *app_desc = esp_app_get_description();
	    ESP_LOGW(TAG, "Firmware version: %s (compiled %s %s)", app_desc->version, app_desc->date, app_desc->time);
	}

    //-------------------------- MQTT --------------------------//
	//----------------------------------------------------------//
	char lwt_topic[128];
	snprintf(lwt_topic, sizeof(lwt_topic), "/%s/%s/status", my_company_id, my_device_id);

	// Read MQTT settings from NVS (set via webui or claim-device), fall back to defaults
	char mqtt_uri[160] = "mqtt://mqtt.vmflow.xyz";
	char mqtt_user[64] = "vmflow";
	char mqtt_pass[64] = "vmflow";
	{
	    nvs_handle_t h;
	    if (nvs_open("vmflow", NVS_READONLY, &h) == ESP_OK) {
	        char mqtt_host[128] = {0};
	        size_t mlen = sizeof(mqtt_host);
	        if (nvs_get_str(h, "mqtt_host", mqtt_host, &mlen) == ESP_OK && strlen(mqtt_host) > 0) {
	            char mqtt_port_str[8] = {0};
	            size_t plen = sizeof(mqtt_port_str);
	            if (nvs_get_str(h, "mqtt_port", mqtt_port_str, &plen) == ESP_OK && strlen(mqtt_port_str) > 0) {
	                snprintf(mqtt_uri, sizeof(mqtt_uri), "mqtt://%s:%s", mqtt_host, mqtt_port_str);
	            } else {
	                snprintf(mqtt_uri, sizeof(mqtt_uri), "mqtt://%s", mqtt_host);
	            }
	        } else {
	            // No mqtt_host set — check if port override applies to default host
	            char mqtt_port_str[8] = {0};
	            size_t plen = sizeof(mqtt_port_str);
	            if (nvs_get_str(h, "mqtt_port", mqtt_port_str, &plen) == ESP_OK && strlen(mqtt_port_str) > 0) {
	                snprintf(mqtt_uri, sizeof(mqtt_uri), "mqtt://mqtt.vmflow.xyz:%s", mqtt_port_str);
	            }
	        }

	        size_t ulen = sizeof(mqtt_user);
	        nvs_get_str(h, "mqtt_user", mqtt_user, &ulen);

	        size_t pwlen = sizeof(mqtt_pass);
	        nvs_get_str(h, "mqtt_pass", mqtt_pass, &pwlen);

	        nvs_close(h);
	    }
	}
	ESP_LOGI(TAG, "MQTT broker: %s (user=%s)", mqtt_uri, mqtt_user);

	const esp_mqtt_client_config_t mqttCfg = {
		.broker.address.uri = mqtt_uri,
        .credentials = {
            /* MQTT connection uses username/password authentication ONLY for broker ACL control.
             * Transport is intentionally non-TLS to reduce overhead on constrained devices.
             *
             * Security model:
             * - MQTT credentials are considered public / non-secret.
             * - Broker acts as a routing layer, not a security boundary.
             * - Broker ACLs limit publish/subscribe scope, reducing traffic amplification
             *   and preserving essential network operation in case of credential exposure.
             * - All application payloads are protected using a per-device XOR-based cipher,
             *   with a secret provisioned at installation time.
             * - Payload signing/obfuscation ensures basic integrity validation and
             *   prevents trivial message injection without knowledge of the device secret.
             */
            .username = mqtt_user,
            .authentication.password = mqtt_pass
        },
		.session.last_will.topic = lwt_topic, // LWT (Last Will and Testament)...
		.session.last_will.msg = "offline",
		.session.last_will.qos = 1,
		.session.last_will.retain = 1,
	};

	mqttClient = esp_mqtt_client_init(&mqttCfg);
	esp_mqtt_client_register_event(mqttClient, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

	//-- Start WiFi now that MQTT client is ready for events ---//
	//----------------------------------------------------------//
	esp_wifi_set_mode(WIFI_MODE_APSTA);
	esp_wifi_start();

	//--------------- Factory reset (BOOT button) --------------//
	//----------------------------------------------------------//
	xTaskCreate(factory_reset_task, "factory_rst", 4096, NULL, 5, NULL);

	//------------------------ BLUETOOTH -----------------------//
	//----------------------------------------------------------//
	ble_init(myhost, ble_event_handler, ble_pax_event_handler);

	const esp_timer_create_args_t periodic_pax_timer_args = {
		.callback = &request_pax_counter,
		.name = "task_paxcounter"
	};

	esp_timer_handle_t periodic_pax_timer;
	esp_timer_create(&periodic_pax_timer_args, &periodic_pax_timer);
	esp_timer_start_periodic(periodic_pax_timer, PAX_SCAN_INTERVAL_US);

    //------------------------ MAIN TASKS ----------------------//
	//----------------------------------------------------------//
	mdbSessionQueue = xQueueCreate(1 /*queue-length*/, sizeof(uint16_t));
	xTaskCreatePinnedToCore(vTaskMdbEvent, "TaskMdbEvent", 4096, NULL, 1, NULL, 1);

    xTaskCreatePinnedToCore(vTaskBitEvent, "TaskBitEvent", 2048, NULL, 1, NULL, 0);
    xEventGroupSetBits(xLedEventGroup, BIT_EVT_TRIGGER);
}