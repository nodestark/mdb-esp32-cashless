/*
 * VMflow.xyz
 *
 * mdb-slave-esp32s3.c - Vending machine peripheral
 *
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <freertos/ringbuf.h>
#include <sdkconfig.h>
#include <esp_log.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <esp_random.h>
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

#define TAG "mdb_cashless"

#define STRINGIFY_IMPL(x) #x
#define STRINGIFY(x)      STRINGIFY_IMPL(x)

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

esp_timer_handle_t periodic_pax_timer;

static bool mqtt_started = false;
static bool sntp_started = false;

static bool is_wifi_on = false;
static bool is_ppp_on = false;

#define WIFI_MAX_RETRY 5
static uint8_t wifi_retry_count = 0;

char my_subdomain[32];
char my_passkey[18];

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

RingbufHandle_t dexRingbuf;

// MQTT client handle
esp_mqtt_client_handle_t mqttClient = NULL;

// Message queues for communication
static QueueHandle_t mdbSessionQueue = NULL;

void xorEncodeWithPasskey(uint8_t cmd, uint16_t itemPrice, uint16_t itemNumber, uint8_t *payload);
uint8_t xorDecodeWithPasskey(uint16_t *itemPrice, uint16_t *itemNumber, uint8_t *payload);

uint16_t read_9(uint8_t *checksum) {

	uint16_t coming_read = 0;

	// Wait start bit
	while (gpio_get_level(PIN_MDB_RX))
		;

	ets_delay_us(156);
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
    ets_delay_us(104);              // 9600bps

	for (uint8_t x = 0; x < 9; x++) {
		gpio_set_level(PIN_MDB_TX, (nth9 >> x) & 1);
		ets_delay_us(104);          // 9600bps
	}

    gpio_set_level(PIN_MDB_TX, 1);  // Stop bit
    ets_delay_us(104);              // 9600bps
}

// Function to transmit the payload via bit-banging (using MDB protocol)
void write_payload_9(uint8_t *mdb_payload, uint8_t length) {

	uint8_t checksum = 0x00;

	// Calculate checksum
	for (int x = 0; x < length; x++) {
		checksum += mdb_payload[x];
		write_9(mdb_payload[x]);
	}

	// CHK* ACK*
	write_9(BIT_MODE_SET | checksum);
}
void mdb_cashless_task(void *pvParameters) {

	time_t session_begin_time = 0;

	uint16_t fundsAvailable = 0;
	uint16_t itemPrice = 0;
	uint16_t itemNumber = 0;

	// Payload buffer and available transmission flag
	uint8_t mdb_payload[36];
	uint8_t available_tx = 0;

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
			} else if ((coming_read & BIT_ADD_SET) == CONFIG_CASHLESS_DEVICE_ADDRESS) {

				// Reset transmission availability
				available_tx = 0;

				// Command decoding based on incoming data
				switch (coming_read & BIT_CMD_SET) {

				case RESET: {

                    if (read_9(NULL) != checksum) continue;

                    // Reset during VEND_STATE is interpreted as VEND_SUCCESS

					cashless_reset_todo = true;
					machine_state = INACTIVE_STATE;

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

                        if (read_9(NULL) != checksum) continue;

						machine_state = DISABLED_STATE;

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

                        if (read_9(NULL) != checksum) continue;

						machine_state = VEND_STATE;

                        if(fundsAvailable && (fundsAvailable != 0xffff)){

                            if (itemPrice <= fundsAvailable) {
                                vend_approved_todo = true;
                            } else {
                                vend_denied_todo = true;
                            }
                        }

						/* PIPE_BLE */
						uint8_t payload[19];
						xorEncodeWithPasskey(0x0a, itemPrice, itemNumber, (uint8_t*) &payload);

                        ble_notify_send((char*) &payload, sizeof(payload));

						ESP_LOGI( TAG, "VEND_REQUEST");
						break;
					}
					case VEND_CANCEL: {
                        if (read_9(NULL) != checksum) continue;

						vend_denied_todo = true;
						break;
					}
					case VEND_SUCCESS: {

						itemNumber = (read_9(&checksum) << 8) | read_9(&checksum);

                        if (read_9(NULL) != checksum) continue;

						machine_state = IDLE_STATE;

						/* PIPE_BLE */
						uint8_t payload[19];
						xorEncodeWithPasskey(0x0b, itemPrice, itemNumber, (uint8_t*) &payload);

                        ble_notify_send((char*) &payload, sizeof(payload));

						ESP_LOGI( TAG, "VEND_SUCCESS");
						break;
					}
					case VEND_FAILURE: {
                        if (read_9(NULL) != checksum) continue;

						machine_state = IDLE_STATE;

					    /* PIPE_BLE */
						uint8_t payload[19];
						xorEncodeWithPasskey(0x0c, itemPrice, itemNumber, (uint8_t*) &payload);

                        ble_notify_send((char*) &payload, sizeof(payload));
						break;
					}
					case SESSION_COMPLETE: {
                        if (read_9(NULL) != checksum) continue;

						session_end_todo = true;

			            /* PIPE_BLE */
						uint8_t payload[19];
						xorEncodeWithPasskey(0x0d, itemPrice, itemNumber, (uint8_t*) &payload);

                        ble_notify_send((char*) &payload, sizeof(payload));

						ESP_LOGI( TAG, "SESSION_COMPLETE");
						break;
					}
					case CASH_SALE: {

						uint16_t itemPrice = (read_9(&checksum) << 8) | read_9(&checksum);
						uint16_t itemNumber = (read_9(&checksum) << 8) | read_9(&checksum);

						if (read_9(NULL) != checksum) continue;

                        uint8_t payload[19];
                        xorEncodeWithPasskey(0x21, itemPrice, itemNumber, (uint8_t*) &payload);

                        char topic[64];
                        snprintf(topic, sizeof(topic), "domain.vmflow.xyz/%s/sale", my_subdomain);

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
                        if (read_9(NULL) != checksum) continue;

						machine_state = DISABLED_STATE;

                        xEventGroupClearBits(xLedEventGroup, BIT_EVT_MDB);
                        xEventGroupSetBits(xLedEventGroup, BIT_EVT_TRIGGER);

						// ESP_LOGI( TAG, "READER_DISABLE");
						break;
					}
					case READER_ENABLE: {
                        if (read_9(NULL) != checksum) continue;

						machine_state = ENABLED_STATE;

                        xEventGroupSetBits(xLedEventGroup, BIT_EVT_MDB | BIT_EVT_TRIGGER);

						// ESP_LOGI( TAG, "READER_ENABLE");
						break;
					}
					case READER_CANCEL: {
                        if (read_9(NULL) != checksum) continue;

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

				        if (read_9(NULL) != checksum) continue;

                        mdb_payload[ 0 ] = 0x09;                        // Peripheral ID

                        memcpy( &mdb_payload[1], "VMF", 3);             // Manufacture code
                        memcpy( &mdb_payload[4], "            ", 12);   // Serial number
                        memcpy( &mdb_payload[16], "            ", 12);  // Model number
                        mdb_payload[28] = 0x00;                         // Software version v3
                        mdb_payload[29] = 0x03;

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

			} else {

				// Not the intended address...
			}
		}
	}
}

/*
 * VMflow wire payload — 19 bytes, XOR-obfuscated with the device passkey.
 *
 *   0        CMD    — opcode (see below)
 *   1– 4     PRICE  — uint32, item price (scale factor applied)
 *   5– 6     ITEM   — uint16, item number
 *   7–10     TIME   — uint32, Unix timestamp (seconds)
 *   11–17    —      — random filler (esp_fill_random)
 *   18       CHK    — sum of bytes 0–17
 *
 * Opcodes:
 *   BLE ← app   0x00 SUBDOMAIN     0x01 PASSKEY  0x02 START_SESSION
 *               0x03 APPROVE       0x04 CLOSE_SESSION
 *               0x06 WIFI_SSID     0x07 WIFI_PASSWORD
 *   BLE → app   0x0A VEND_REQUEST  0x0B VEND_SUCCESS
 *               0x0C VEND_FAILURE  0x0D SESSION_COMPLETE
 *   MQTT ← srv  0x20 CREDIT
 *   MQTT → srv  0x21 CASH_SALE     0x22 PAX_COUNTER (ITEM = device count)
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

    int32_t timestamp = ((uint32_t) payload[7] << 24) |
						((uint32_t) payload[8] << 16) |
						((uint32_t) payload[9] << 8)  |
						((uint32_t) payload[10] << 0);

    time_t now = time(NULL);

    if( abs((int32_t) now - timestamp) > 8 /*sec*/){
        return 0;
    }

    int32_t itemPrice32 =   ((uint32_t) payload[1] << 24) |
                            ((uint32_t) payload[2] << 16) |
                            ((uint32_t) payload[3] << 8)  |
                            ((uint32_t) payload[4] << 0);

    if(itemPrice)
        *itemPrice = TO_SCALE_FACTOR( FROM_SCALE_FACTOR(itemPrice32, 1, 2), CONFIG_MDB_SCALE_FACTOR, CONFIG_MDB_DECIMAL_PLACES);

    if(itemNumber)
        *itemNumber = ((uint16_t) payload[5] << 8) | ((uint16_t) payload[6] << 0);

    return 1;
}

// Encode payload to communication between BLE and MQTT
void xorEncodeWithPasskey(uint8_t cmd, uint16_t itemPrice, uint16_t itemNumber, uint8_t *payload) {

    uint32_t itemPrice32 = TO_SCALE_FACTOR( FROM_SCALE_FACTOR(itemPrice, CONFIG_MDB_SCALE_FACTOR, CONFIG_MDB_DECIMAL_PLACES), 1, 2);

	esp_fill_random(payload, 19);

	time_t now = time(NULL);

    payload[0] = cmd;

	payload[1] = itemPrice32 >> 24;     // itemPrice
    payload[2] = itemPrice32 >> 16;
	payload[3] = itemPrice32 >> 8;
	payload[4] = itemPrice32;
	payload[5] = itemNumber >> 8;	    // itemNumber
	payload[6] = itemNumber;
	payload[7] = now >> 24;		        // time (sec)
	payload[8] = now >> 16;
	payload[9] = now >> 8;
	payload[10] = now;
	// ...18

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

  	char topic[64];
	snprintf(topic, sizeof(topic), "domain.vmflow.xyz/%s/dex", my_subdomain);

    esp_mqtt_client_publish(mqttClient, topic, (char*) dex, dex_size, 0, 0);
    printf("%.*s", dex_size, (char*) dex);

    vRingbufferReturnItem(dexRingbuf, (void*) dex);
}

void led_status_task(void *pvParameters) {

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
    xorEncodeWithPasskey(0x22, 0, devices_count, (uint8_t*) &payload);

    char topic[64];
    snprintf(topic, sizeof(topic), "domain.vmflow.xyz/%s/paxcounter", my_subdomain);

    esp_mqtt_client_publish(mqttClient, topic, (char*) &payload, sizeof(payload), 1, 0);
}

void ble_event_handler(char *ble_payload) {

    printf("ble_event_handler %x\n", (uint8_t) ble_payload[0]);

	switch ( (uint8_t) ble_payload[0] ) {
    case 0x00: {
        nvs_handle_t handle;
		nvs_open("vmflow", NVS_READWRITE, &handle);

		size_t s_len;
		if (nvs_get_str(handle, "domain", NULL, &s_len) != ESP_OK) {

			strcpy((char*) &my_subdomain, ble_payload + 1);

			nvs_set_str(handle, "domain", (char*) &my_subdomain);
			nvs_commit(handle);

			char myhost[64];
			snprintf(myhost, sizeof(myhost), "%s.vmflow.xyz", my_subdomain);

			ble_set_device_name((char*) &myhost);

            xEventGroupSetBits(xLedEventGroup, BIT_EVT_DOMAIN | BIT_EVT_TRIGGER);

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

			strcpy((char*) &my_passkey, ble_payload + 1);

			nvs_set_str(handle, "passkey", (char*) &my_passkey);
			nvs_commit(handle);

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

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {

	esp_mqtt_event_handle_t event = event_data;
	esp_mqtt_client_handle_t mqttClient = event->client;

	switch ((esp_mqtt_event_id_t) event_id) {
	case MQTT_EVENT_CONNECTED:

    	char topic[64];
    	snprintf(topic, sizeof(topic), "%s.vmflow.xyz/#", my_subdomain);

    	esp_mqtt_client_subscribe(mqttClient, topic, 0);
    	ESP_LOGI(TAG, "subscribed to: %s", topic);

    	char topic_[64];
    	snprintf(topic_, sizeof(topic_), "domain.vmflow.xyz/%s/status", my_subdomain);

		esp_mqtt_client_publish(mqttClient, topic_, "online", 0, 1, 1);

        xEventGroupSetBits(xLedEventGroup, BIT_EVT_INTERNET | BIT_EVT_TRIGGER);

        esp_timer_stop(periodic_pax_timer);
        esp_timer_start_periodic(periodic_pax_timer, PAX_SCAN_INTERVAL_US);

		break;
	case MQTT_EVENT_DISCONNECTED:

        xEventGroupClearBits(xLedEventGroup, BIT_EVT_INTERNET);
        xEventGroupSetBits(xLedEventGroup, BIT_EVT_TRIGGER);

        esp_timer_stop(periodic_pax_timer);

		break;
	case MQTT_EVENT_SUBSCRIBED:
		break;
	case MQTT_EVENT_UNSUBSCRIBED:
		break;
	case MQTT_EVENT_PUBLISHED:
		break;
	case MQTT_EVENT_DATA:

	    ESP_LOGI( TAG, "TOPIC= %.*s", event->topic_len, event->topic);
	    ESP_LOGI( TAG, "DATA_LEN= %d", event->data_len);
	    ESP_LOGI( TAG, "DATA= %.*s", event->data_len, event->data);

		if (event->topic_len > 7 && strncmp(event->topic + event->topic_len - 7, "/credit", 7) == 0) {

			uint16_t fundsAvailable;
			if(xorDecodeWithPasskey(&fundsAvailable, NULL, (uint8_t*) event->data)){
			    xQueueSend(mdbSessionQueue, &fundsAvailable, 0 /*if full, do not wait*/);

                xEventGroupSetBits(xLedEventGroup, BIT_EVT_BUZZER | BIT_EVT_TRIGGER);

                ESP_LOGI( TAG, "Amount= %f", FROM_SCALE_FACTOR(fundsAvailable, CONFIG_MDB_SCALE_FACTOR, CONFIG_MDB_DECIMAL_PLACES) );
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

void on_internet_status(bool is_ppp_on, bool is_wifi_on){

    if(is_ppp_on || is_wifi_on){
        if (!mqtt_started) {
            esp_mqtt_client_start(mqttClient);
            mqtt_started = true;
        }

        if (!sntp_started) {
            esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
            esp_sntp_setservername(0, "pool.ntp.org");
            esp_sntp_init();

            sntp_started = true;
        }
    }

    if(!is_ppp_on && !is_wifi_on){
        if (mqtt_started) {
            esp_mqtt_client_stop(mqttClient);
            mqtt_started = false;
        }
    }

}

static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    switch (event_id) {
        case IP_EVENT_PPP_GOT_IP: {
            is_ppp_on = true;
            ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
            ESP_LOGI(TAG, "ppp got IP: " IPSTR, IP2STR(&event->ip_info.ip));
            break;
        }
        case IP_EVENT_PPP_LOST_IP:
            ESP_LOGW(TAG, "ppp lost IP, rebooting...");
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_restart();
            break;
        case IP_EVENT_STA_GOT_IP: {
            is_wifi_on = true;
            ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
            ESP_LOGI(TAG, "wifi got IP: " IPSTR, IP2STR(&event->ip_info.ip));
            break;
        }
    }

    on_internet_status(is_ppp_on, is_wifi_on);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {

    switch (event_id) {
    case WIFI_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case WIFI_EVENT_STA_CONNECTED:
        wifi_retry_count = 0;
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
		is_wifi_on = false;

        if (wifi_retry_count < WIFI_MAX_RETRY) {
            wifi_retry_count++;
            ESP_LOGI(TAG, "WiFi reconnect attempt %d/%d", wifi_retry_count, WIFI_MAX_RETRY);
            esp_wifi_connect();
        } else {
            ESP_LOGW(TAG, "WiFi max retries reached (%d), stopping reconnect", WIFI_MAX_RETRY);
        }

        break;
    }

    on_internet_status(is_ppp_on, is_wifi_on);
}

static void sim7080g_pulse_power(void) {

    /* transistor inverts: GPIO high → PWRKEY low on SIM7080G (active pulse) */
    gpio_set_level(PIN_SIM7080G_PWR, 1);
    vTaskDelay(pdMS_TO_TICKS(1200));
    gpio_set_level(PIN_SIM7080G_PWR, 0);

    ESP_LOGI(TAG, "waiting for SIM7080G boot...");
    vTaskDelay(pdMS_TO_TICKS(5000));
}

/* Wait for EPS network registration (AT+CEREG: stat=1 home, stat=5 roaming) */
static void sim7080g_wait_registered(esp_modem_dce_t *dce) {

    char resp[64];
    for (int i = 0; i < 30; i++) {
        memset(resp, 0, sizeof(resp));
        esp_modem_at(dce, "AT+CEREG?", resp, 3000);
        /* response: +CEREG: <n>,<stat> — stat 1=home, 5=roaming */
        if (strstr(resp, ",1") || strstr(resp, ",5")) {
            ESP_LOGI(TAG, "network registered (%s)", resp);
            return;
        }
        ESP_LOGW(TAG, "not registered yet: %s", resp);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    ESP_LOGE(TAG, "network registration timeout");
}

void app_main(void) {

    gpio_set_direction(PIN_MDB_RX, GPIO_MODE_INPUT);
    gpio_set_direction(PIN_MDB_TX, GPIO_MODE_OUTPUT);
	//

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
        .resolution_hz = 10000000, // 10 MHz (1 tick = 0.1 µs)
        .mem_block_symbols = 64,
    };
    led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);

    xTaskCreatePinnedToCore(led_status_task, "led_status", 2048, NULL, 1, NULL, 0);
    xEventGroupSetBits(xLedEventGroup, BIT_EVT_TRIGGER);

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

    const uint64_t INTERVAL_12H_US = 12ULL * 60ULL * 60ULL * 1000000ULL; // 12h in microseconds

	const esp_timer_create_args_t periodic_dex_timer_args = {
		.callback   = &requestTelemetryData,
		.name       = "task_dex_12h"
	};
    esp_timer_handle_t periodic_dex_timer;

	esp_timer_create(&periodic_dex_timer_args, &periodic_dex_timer);
	esp_timer_start_periodic(periodic_dex_timer, INTERVAL_12H_US);

	//-------------------- NETWORK STACK -----------------------//
	//----------------------------------------------------------//
	nvs_flash_init();
	//
	esp_netif_init();
	esp_event_loop_create_default();

	esp_netif_t *wifi_netif = esp_netif_create_default_wifi_sta();
    esp_netif_set_route_prio(wifi_netif, 100);

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	esp_wifi_init(&cfg);

	esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL);
	esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, ip_event_handler, NULL, NULL);

	esp_wifi_set_mode(WIFI_MODE_STA);
	esp_wifi_start();

	//------------------------ BLUETOOTH -----------------------//
	//----------------------------------------------------------//
	char myhost[64];
	strcpy(myhost, "0.vmflow.xyz"); // Default value

    nvs_handle_t handle;
	if (nvs_open("vmflow", NVS_READONLY, &handle) == ESP_OK) {

	    size_t s_len = 0;
	    if (nvs_get_str(handle, "passkey", NULL, &s_len) == ESP_OK) {
            nvs_get_str(handle, "passkey", my_passkey, &s_len);

            if (nvs_get_str(handle, "domain", NULL, &s_len) == ESP_OK) {
                nvs_get_str(handle, "domain", my_subdomain, &s_len);

                snprintf(myhost, sizeof(myhost), "%s.vmflow.xyz", my_subdomain);

                xEventGroupSetBits(xLedEventGroup, BIT_EVT_PSSKEY | BIT_EVT_DOMAIN | BIT_EVT_TRIGGER);
            }
        }

		nvs_close(handle);
	}
	ble_init(myhost, ble_event_handler, ble_pax_event_handler);

	const esp_timer_create_args_t periodic_pax_timer_args = {
		.callback   = &ble_scan_start,
        .arg        = (void*) (uintptr_t) PAX_SCAN_DURATION_SEC,
		.name       = "task_paxcounter"
	};
    esp_timer_create(&periodic_pax_timer_args, &periodic_pax_timer);

    //-------------------------- MQTT --------------------------//
	//----------------------------------------------------------//
	char lwt_topic[64];
	snprintf(lwt_topic, sizeof(lwt_topic), "domain.vmflow.xyz/%s/status", my_subdomain);

	const esp_mqtt_client_config_t mqttCfg = {
		.broker.address.uri = "mqtt://mqtt.vmflow.xyz",
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
             .username = "vmflow",
            .authentication.password = "vmflow"
        },
		.session.last_will.topic = lwt_topic,
		.session.last_will.msg = "offline",
		.session.last_will.qos = 1,
		.session.last_will.retain = 1,
		.session.keepalive = 120,          /* PING interval (s), default 120 */
		.network.timeout_ms = 20000,       /* TCP read timeout (ms) */
		.network.reconnect_timeout_ms = 10000,
	};

	mqttClient = esp_mqtt_client_init(&mqttCfg);
	esp_mqtt_client_register_event(mqttClient, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    //------------------------ MAIN TASKS ----------------------//
	//----------------------------------------------------------//
	mdbSessionQueue = xQueueCreate(1 /*queue-length*/, sizeof(uint16_t));
    xTaskCreatePinnedToCore(mdb_cashless_task, "mdb_cashless_task", 8192, NULL, 1, NULL, 1);

    //------------------- SIM7080g STACK -----------------------//
	//----------------------------------------------------------//
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
    esp_netif_set_route_prio(ppp_netif, 200);

    esp_modem_dce_t *dce = esp_modem_new_dev(ESP_MODEM_DCE_SIM7000, &dte_config, &dce_config, ppp_netif);
    assert(dce);

    /* try sync — modem may already be on (flash/soft-reset) */
    esp_err_t ret = esp_modem_sync(dce);
    if (ret != ESP_OK) {
        /* may be stuck in PPP mode from previous session — escape first */
        esp_modem_set_mode(dce, ESP_MODEM_MODE_COMMAND);
        vTaskDelay(pdMS_TO_TICKS(1500));
        ret = esp_modem_sync(dce);
    }

    if (ret != ESP_OK) {
        /* truly off — pulse to power on */
        sim7080g_pulse_power();
        ret = esp_modem_sync(dce);
        if (ret != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            ret = esp_modem_sync(dce);
        }
    } else {
        ESP_LOGI(TAG, "modem already on");
    }

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "no modem detected, skipping PPP");
    } else {
        ESP_LOGI(TAG, "modem detected");
        esp_modem_at(dce, "AT+CNMP=38", NULL, 3000);
        esp_modem_at(dce, "AT+CMNB=" STRINGIFY(CONFIG_SIM7080G_CMNB), NULL, 3000);
        esp_modem_at(dce, "AT+CEREG=1", NULL, 3000);
        sim7080g_wait_registered(dce);

        int rssi = 0, ber = 0;
        if (esp_modem_get_signal_quality(dce, &rssi, &ber) == ESP_OK) {
            ESP_LOGI(TAG, "RSSI: %d dBm, BER: %d", (rssi == 99 ? 0 : -113 + 2 * rssi), ber);
        }

        ESP_LOGI(TAG, "setting data mode...");
        esp_modem_set_mode(dce, ESP_MODEM_MODE_DATA);
    }
}