/*
 * mdb-slave-esp32s3.c - Vending machine peripheral
 *
 */

#include "esp_log.h"

#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_wifi.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <math.h>
#include <mqtt_client.h>
#include <nvs_flash.h>
#include <rom/ets_sys.h>
#include <stdio.h>
#include <string.h>
#include <esp_sntp.h>

#include "bleprph.h"
#include "nimble.h"

#define TAG "mdb-target"

#define pin_mdb_rx  	GPIO_NUM_4  // Pin to receive data from MDB
#define pin_mdb_tx  	GPIO_NUM_5  // Pin to transmit data to MDB
#define pin_mdb_led 	GPIO_NUM_21 // LED to indicate MDB state

// Functions for scale factor conversion
#define to_scale_factor(p, x, y) (p / x / pow(10, -(y) ))  // Converts to scale factor
#define from_scale_factor(p, x, y) (p * x * pow(10, -(y) )) // Converts from scale factor

#define ACK 	0x00  // Acknowledgment / Checksum correct
#define RET 	0xAA  // Retransmit previously sent data. Only VMC can send this
#define NAK 	0xFF  // Negative acknowledgment

// Bit masks for MDB operations
#define BIT_MODE_SET 	0b100000000
#define BIT_ADD_SET   	0b011111000
#define BIT_CMD_SET   	0b000000111

char my_subdomain[32];
char my_passkey[18];

// Defining MDB commands as an enum
enum MDB_COMMAND {
	RESET = 0x00,
	SETUP = 0x01,
	POLL = 0x02,
	VEND = 0x03,
	READER = 0x04,
	EXPANSION = 0x07
};

// Defining MDB setup flow
enum MDB_SETUP_FLOW {
	CONFIG_DATA = 0x00, MAX_MIN_PRICES = 0x01
};

// Defining MDB vending flow
enum MDB_VEND_FLOW {
	VEND_REQUEST = 0x00,
	VEND_CANCEL = 0x01,
	VEND_SUCCESS = 0x02,
	VEND_FAILURE = 0x03,
	SESSION_COMPLETE = 0x04,
	CASH_SALE = 0x05
};

// Defining MDB reader flow
enum MDB_READER_FLOW {
	READER_DISABLE = 0x00, READER_ENABLE = 0x01, READER_CANCEL = 0x02
};

// Defining MDB expansion flow
enum MDB_EXPANSION_FLOW {
	REQUEST_ID = 0x00
};

// Defining machine states
typedef enum MACHINE_STATE {
	INACTIVE_STATE, DISABLED_STATE, ENABLED_STATE, IDLE_STATE, VEND_STATE
} machine_state_t;

machine_state_t machine_state = INACTIVE_STATE, machine_state_; // Initial machine state

// Control flags for MDB flows
uint8_t session_begin_todo = false;
uint8_t session_cancel_todo = false;
uint8_t session_end_todo = false;
//
uint8_t vend_approved_todo = false;
uint8_t vend_denied_todo = false;
//
uint8_t cashless_reset_todo = false;
uint8_t outsequence_todo = false;

// MQTT client handle
esp_mqtt_client_handle_t mqttClient = (void*) 0;

// Message queues for communication
static QueueHandle_t mdbSessionQueue = (void*) 0;

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

    int32_t timestamp = ((uint32_t) payload[6] << 24) |
						((uint32_t) payload[7] << 16) |
						((uint32_t) payload[8] << 8)  |
						((uint32_t) payload[9] << 0);

    time_t now;
    time( &now);

    if( abs((int32_t) now - timestamp) > 8 /*sec*/){
        return 0;
    }

    if(itemPrice)
        *itemPrice = ((uint16_t) payload[2] << 8) | ((uint16_t) payload[3] << 0);

    if(itemNumber)
        *itemNumber = ((uint16_t) payload[4] << 8) | ((uint16_t) payload[5] << 0);

    return 1;
}

void xorEncodeWithPasskey(uint16_t *itemPrice, uint16_t *itemNumber, uint8_t *payload) {

	esp_fill_random(payload + 1, sizeof(my_passkey));

	time_t now;
	time( &now);

	// payload[0] = cmd;
	payload[1] = 0x01; 				// version v1
	payload[2] = *itemPrice >> 8;	// itemPrice
	payload[3] = *itemPrice;
	payload[4] = *itemNumber >> 8;	// itemNumber
	payload[5] = *itemNumber;
	payload[6] = (now >> 24);		// time (sec)
	payload[7] = (now >> 16);
	payload[8] = (now >> 8);
	payload[9] = (now >> 0);

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


uint16_t IRAM_ATTR read_9(uint8_t *checksum) {

	uint16_t coming_read = 0;

	// Wait start bit
	while (gpio_get_level(pin_mdb_rx))
		;

	ets_delay_us(104);
	ets_delay_us(78);

	for(int x = 0; x < 9; x++){
		coming_read |= (gpio_get_level(pin_mdb_rx) << x);
		ets_delay_us(104); // 9600bps
	}

	if (checksum)
		*checksum += coming_read;

	return coming_read;
}

void IRAM_ATTR write_9(uint16_t nth9) {

	nth9 <<= 1;
	nth9 |= 0b10000000000; // Start bit | nth9 | Stop bit

	for (uint8_t x = 0; x < 11; x++) {

		gpio_set_level(pin_mdb_tx, (nth9 >> x) & 1);
		ets_delay_us(104); // 9600bps
	}
}

// Function to transmit the payload via UART9 (using MDB protocol)
void write_payload_9(uint8_t *mdb_payload, uint8_t length) {

	uint8_t checksum = 0;

	// Calculate checksum
	for (int x = 0; x < length; x++) {

		checksum += mdb_payload[x];
		write_9(mdb_payload[x]);
	}

	// CHK* ACK*
	write_9(BIT_MODE_SET | checksum);
}

// Main MDB loop function
void mdb_cashless_loop(void *pvParameters) {

	time_t session_begin_time = 0;

	uint16_t fundsAvailable = 0;
	uint16_t itemPrice = 0;
	uint16_t itemNumber = 0;

	// Payload buffer and available transmission flag
	uint8_t mdb_payload[256];
	uint8_t available_tx = 0;

	for (;;) {

		// Checksum calculation
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

			} else if ((coming_read & BIT_ADD_SET) == 0x10) {

				// Reset transmission availability
				available_tx = 0;

				// Command decoding based on incoming data
				switch (coming_read & BIT_CMD_SET) {

				case RESET: {
					uint8_t checksum_ = read_9((uint8_t*) 0);

					if (machine_state == VEND_STATE) {
						// Reset during VEND_STATE is interpreted as VEND_SUCCESS
					}

					machine_state = INACTIVE_STATE;
					cashless_reset_todo = true;

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

						uint8_t checksum_ = read_9((uint8_t*) 0);

						machine_state = DISABLED_STATE;

						mdb_payload[0] = 0x01;        	// Reader Config Data
						mdb_payload[1] = 1;           	// Reader Feature Level
						mdb_payload[2] = 0xff;        	// Country Code High
						mdb_payload[3] = 0xff;        	// Country Code Low
						mdb_payload[4] = 1;           	// Scale Factor
						mdb_payload[5] = 2;           	// Decimal Places
						mdb_payload[6] = 3; 			// Maximum Response Time (5s)
						mdb_payload[7] = 0b00001001;  	// Miscellaneous Options
						available_tx = 8;

						break;
					}
					case MAX_MIN_PRICES: {

						uint16_t maxPrice = (read_9(&checksum) << 8) | read_9(&checksum);
						uint16_t minPrice = (read_9(&checksum) << 8) | read_9(&checksum);

						uint8_t checksum_ = read_9((uint8_t*) 0);

						break;
					}
					}

					break;
				}
				case POLL: {

					uint8_t checksum_ = read_9((uint8_t*) 0);

					if (outsequence_todo) {
						// Command out of sequence
						outsequence_todo = false;

						mdb_payload[0] = 0x0b;
						available_tx = 1;

					} else if (cashless_reset_todo) {
						// Just reset
						cashless_reset_todo = false;
						mdb_payload[0] = 0x00;
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

					} else if (machine_state == ENABLED_STATE && xQueueReceive(mdbSessionQueue, &fundsAvailable, 0)) {
						// Begin session
						session_begin_todo = false;

						machine_state = IDLE_STATE;

						uint16_t fundsAvailable_ = (fundsAvailable == 0x0000 ? 0xffff : fundsAvailable);

						mdb_payload[0] = 0x03;
						mdb_payload[1] = fundsAvailable_ >> 8;
						mdb_payload[2] = fundsAvailable_;
						available_tx = 3;
						
						time( &session_begin_time);

					} else if (session_cancel_todo) {
						// Cancel session
						session_cancel_todo = false;

						mdb_payload[0] = 0x04;
						available_tx = 1;

					} else {

						time_t now;
						time( &now);
						
						if (machine_state >= IDLE_STATE && (now - session_begin_time /*elapsed*/) > 90 /*sec*/) {
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

						uint8_t checksum_ = read_9((uint8_t*) 0);

						machine_state = VEND_STATE;

                        if(fundsAvailable){
                            if (itemPrice <= fundsAvailable) {
                                vend_approved_todo = true;
                            } else {
                                vend_denied_todo = true;
                            }
                        }

						/* PIPE_BLE */
						uint8_t payload[19];
						payload[0] = 0x0a;

						xorEncodeWithPasskey(&itemPrice, &itemNumber, (uint8_t*) &payload);

                        sendBleNotification((char*) &payload, sizeof(payload));

						ESP_LOGI( TAG, "VEND_REQUEST");
						break;
					}
					case VEND_CANCEL: {

						uint8_t checksum_ = read_9((uint8_t*) 0);

						vend_denied_todo = true;

						break;
					}
					case VEND_SUCCESS: {

						itemNumber = (read_9(&checksum) << 8) | read_9(&checksum);

						uint8_t checksum_ = read_9((uint8_t*) 0);

						machine_state = IDLE_STATE;

						/* PIPE_BLE */
						uint8_t payload[19];
						payload[0] = 0x0b;

						xorEncodeWithPasskey(&itemPrice, &itemNumber, (uint8_t*) &payload);

                        sendBleNotification((char*) &payload, sizeof(payload));

						ESP_LOGI( TAG, "VEND_SUCCESS");
						break;
					}
					case VEND_FAILURE: {

						uint8_t checksum_ = read_9((uint8_t*) 0);

						machine_state = IDLE_STATE;

					    /* PIPE_BLE */
						uint8_t payload[19];
						payload[0] = 0x0c;

						xorEncodeWithPasskey(&itemPrice, &itemNumber, (uint8_t*) &payload);

                        sendBleNotification((char*) &payload, sizeof(payload));
						break;
					}
					case SESSION_COMPLETE: {

						uint8_t checksum_ = read_9((uint8_t*) 0);

						session_end_todo = true;

			            /* PIPE_BLE */
						uint8_t payload[19];
						payload[0] = 0x0d;

						xorEncodeWithPasskey(&itemPrice, &itemNumber, (uint8_t*) &payload);

                        sendBleNotification((char*) &payload, sizeof(payload));

						ESP_LOGI( TAG, "SESSION_COMPLETE");
						break;
					}
					case CASH_SALE: {

						uint16_t itemPrice = (read_9(&checksum) << 8) | read_9(&checksum);
						uint16_t itemNumber = (read_9(&checksum) << 8) | read_9(&checksum);

						uint8_t checksum_ = read_9((uint8_t*) 0);

						if (checksum_ == checksum) {

                            uint8_t payload[19];
                            payload[0] = 0x21;

						    xorEncodeWithPasskey(&itemPrice, &itemNumber, (uint8_t*) &payload);

						  	char topic[64];
						  	snprintf(topic, sizeof(topic), "/domain/%s/sale", my_subdomain);

							esp_mqtt_client_publish(mqttClient, topic, (char*) &payload, sizeof(payload), 1, 0);

							ESP_LOGI( TAG, "CASH_SALE");
						}

						break;
					}
					}

					break;
				}
				case READER: {
					switch (read_9(&checksum)) {
					case READER_DISABLE: {

						uint8_t checksum_ = read_9((uint8_t*) 0);
						machine_state = DISABLED_STATE;

						break;
					}
					case READER_ENABLE: {

						uint8_t checksum_ = read_9((uint8_t*) 0);
						machine_state = ENABLED_STATE;

						break;
					}
					case READER_CANCEL: {

						uint8_t checksum_ = read_9((uint8_t*) 0);

						mdb_payload[ 0 ] = 0x08; // Canceled
						available_tx = 1;

						break;
					}
					}

					break;
				}
				case EXPANSION: {

					switch (read_9(&checksum)) {
					case REQUEST_ID: {

						mdb_payload[ 0 ] = 0x09; 	// Peripheral ID
						mdb_payload[ 1 ] = ' '; 	// Manufacture code
						mdb_payload[ 2 ] = ' ';
						mdb_payload[ 3 ] = ' ';

						mdb_payload[ 4 ] = ' '; 	// Serial number
						mdb_payload[ 5 ] = ' ';
						mdb_payload[ 6 ] = ' ';
						mdb_payload[ 7 ] = ' ';
						mdb_payload[ 8 ] = ' ';
						mdb_payload[ 9 ] = ' ';
						mdb_payload[ 10 ] = ' ';
						mdb_payload[ 11 ] = ' ';
						mdb_payload[ 12 ] = ' ';
						mdb_payload[ 13 ] = ' ';
						mdb_payload[ 14 ] = ' ';
						mdb_payload[ 15 ] = ' ';

						mdb_payload[ 16 ] = ' '; 	// Model number
						mdb_payload[ 17 ] = ' ';
						mdb_payload[ 18 ] = ' ';
						mdb_payload[ 19 ] = ' ';
						mdb_payload[ 20 ] = ' ';
						mdb_payload[ 21 ] = ' ';
						mdb_payload[ 22 ] = ' ';
						mdb_payload[ 23 ] = ' ';
						mdb_payload[ 24 ] = ' ';
						mdb_payload[ 25 ] = ' ';
						mdb_payload[ 26 ] = ' ';
						mdb_payload[ 27 ] = ' ';

						mdb_payload[ 28 ] = ' '; 	// Software version
						mdb_payload[ 29 ] = ' ';

						available_tx = 30;
						break;
					}
					}

					break;
				}
				}

				// Transmit the prepared payload via UART
				write_payload_9((uint8_t*) &mdb_payload, available_tx);

				// Intended address
				gpio_set_level(pin_mdb_led, 1);

			} else {

				// Not the intended address
				gpio_set_level(pin_mdb_led, 0);
			}

		}
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

			printf("%c", data[0]);
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
			printf("%c", (char) buffer_rx[x]);

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

SemaphoreHandle_t xOneShotReqTelemetry;

void requestTelemetryData(void *arg) {

	if (xSemaphoreTake(xOneShotReqTelemetry, (TickType_t) 0 ) == pdTRUE) {

		readTelemetryDDCMP();
		readTelemetryDEX();

        xSemaphoreGive(xOneShotReqTelemetry);
	}

    vTaskDelete((void*) 0);
}

void ble_event_handler(char *event_data) {

	switch ( (uint8_t) event_data[0] ) {
    case 0x00: {
        nvs_handle_t handle;
		ESP_ERROR_CHECK( nvs_open("vmflow", NVS_READWRITE, &handle) );

		size_t s_len;
		if (nvs_get_str(handle, "domain", NULL, &s_len) != ESP_OK) {

			strcpy((char*) &my_subdomain, event_data + 1);

			ESP_ERROR_CHECK( nvs_set_str(handle, "domain", (char*) &my_subdomain) );
			ESP_ERROR_CHECK( nvs_commit(handle) );

			char myhost[64];
			snprintf(myhost, sizeof(myhost), "%s.vmflow.xyz", my_subdomain);

			renameBleDevice((char*) &myhost);

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

			strcpy((char*) &my_passkey, event_data + 1);

			ESP_ERROR_CHECK( nvs_set_str(handle, "passkey", (char*) &my_passkey) );
			ESP_ERROR_CHECK( nvs_commit(handle) );

			ESP_LOGI( TAG, "PASSKEY= %s", my_passkey);
		}
		nvs_close(handle);

        break;
    }
    case 0x02: /*Starting a vending session*/
        uint16_t fundsAvailable = 0x0000;
		xQueueSend(mdbSessionQueue, &fundsAvailable, 0 /*if full, do not wait*/);
		break;
	case 0x03: /*Approve the vending session*/

        if(xorDecodeWithPasskey((void*) 0, (void*) 0, (uint8_t*) event_data)){
            vend_approved_todo = (machine_state == VEND_STATE) ? true : false;
        }
        break;
    case 0x04: /*Close the vending session*/
    	session_cancel_todo = (machine_state >= IDLE_STATE) ? true : false;
        break;
    case 0x05:
        xTaskCreate(requestTelemetryData, "requestTelemetry", 2048, NULL, 1, NULL);
        break;
    case 0x06: {
        esp_wifi_disconnect();

		wifi_config_t wifi_config = {0};
		esp_wifi_get_config(WIFI_IF_STA, &wifi_config);

		strcpy((char*) wifi_config.sta.ssid, event_data + 1);
		esp_wifi_set_config(WIFI_IF_STA, &wifi_config);

	    ESP_LOGI( TAG, "SSID= %s", wifi_config.sta.ssid);
        break;
    }
    case 0x07: {
        wifi_config_t wifi_config = {0};
		esp_wifi_get_config(WIFI_IF_STA, &wifi_config);

		strcpy((char*) wifi_config.sta.password, event_data + 1);
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

    	char topic_[64];
    	snprintf(topic_, sizeof(topic_), "/domain/%s/status", my_subdomain);

		esp_mqtt_client_publish(mqttClient, topic_, "online", 0, 1, 1);

		break;
	case MQTT_EVENT_DISCONNECTED:
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

		size_t topic_len = strlen(event->topic);

		if (topic_len > 10 && strncmp(event->topic + event->topic_len - 10, "/telemetry", 10) == 0) {
			xTaskCreate(requestTelemetryData, "OneShotTelemetry", 2048, NULL, 1, NULL);
		}

		if (topic_len > 7 && strncmp(event->topic + event->topic_len - 7, "/credit", 7) == 0) {

			uint16_t fundsAvailable;
			if(xorDecodeWithPasskey(&fundsAvailable, (void*) 0, (uint8_t*) event->data)){
			    xQueueSend(mdbSessionQueue, &fundsAvailable, 0 /*if full, do not wait*/);

			    ESP_LOGI( TAG, "Amount= %d", fundsAvailable);
			}
		}

		break;
	case MQTT_EVENT_ERROR:
		break;
	default:
	    ESP_LOGI( TAG, "Other event id: %d", event->event_id);
		break;
	}
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {

	if (event_base == WIFI_EVENT)
		switch (event_id) {
		case WIFI_EVENT_STA_START:
			esp_wifi_connect();
			break;
		case WIFI_EVENT_STA_CONNECTED:
			break;
		case WIFI_EVENT_STA_DISCONNECTED:
			esp_mqtt_client_disconnect(mqttClient);
			esp_wifi_connect();
			break;
		}

	if (event_base == IP_EVENT)
		switch (event_id) {
		case IP_EVENT_STA_GOT_IP:

			esp_mqtt_client_start(mqttClient);

			// Configuration and initialization of the SNTP client for time synchronization via the internet
			esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
			esp_sntp_setservername(0, "pool.ntp.org");
			esp_sntp_init();

			break;
		}
}

void app_main(void) {

	gpio_set_direction(pin_mdb_rx, GPIO_MODE_INPUT);

	gpio_set_direction(pin_mdb_tx, GPIO_MODE_OUTPUT);
	gpio_set_direction(pin_mdb_led, GPIO_MODE_OUTPUT);

	// Initialize UART1 driver and configure TX/RX pins
	uart_config_t uart_config_1 = {
			.baud_rate = 9600,
			.data_bits = UART_DATA_8_BITS,
			.parity = UART_PARITY_DISABLE,
			.stop_bits = UART_STOP_BITS_1,
			.flow_ctrl = UART_HW_FLOWCTRL_DISABLE };

	uart_param_config(UART_NUM_1, &uart_config_1);
	uart_set_pin( UART_NUM_1, GPIO_NUM_17, GPIO_NUM_18, -1, -1);
	uart_driver_install(UART_NUM_1, 256, 256, 0, (void*) 0, 0);

	// Initialization of the network stack and event loop
	nvs_flash_init();
	//
	esp_netif_init();
	esp_event_loop_create_default();
	esp_netif_create_default_wifi_sta();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	esp_wifi_init(&cfg);

	esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, (void*) 0, (void*) 0);
	esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, (void*) 0, (void*) 0);

	esp_wifi_set_mode(WIFI_MODE_STA);
	esp_wifi_start();

	// Initialization of Bluetooth Low Energy (BLE) with the alias
	char myhost[64];
	strcpy(myhost, "0.vmflow.xyz"); // Default value
	//
	nvs_handle_t handle;
	if (nvs_open("vmflow", NVS_READONLY, &handle) == ESP_OK) {

	    size_t s_len = 0;
        if (nvs_get_str(handle, "domain", (void*) 0, &s_len) == ESP_OK) {
        	nvs_get_str(handle, "domain", my_subdomain, &s_len);

			snprintf(myhost, sizeof(myhost), "%s.vmflow.xyz", my_subdomain);
		}

        if (nvs_get_str(handle, "passkey", (void*) 0, &s_len) == ESP_OK) {
            nvs_get_str(handle, "passkey", my_passkey, &s_len);
		}

		nvs_close(handle);
	}

	startBleDevice(myhost, ble_event_handler);
	//
	char lwt_topic[64];
	snprintf(lwt_topic, sizeof(lwt_topic), "/domain/%s/status", my_subdomain);

	// MQTT client configuration
	const esp_mqtt_client_config_t mqttCfg = {
		.broker.address.uri = "mqtt://mqtt.vmflow.xyz",
		// LWT (Last Will and Testament)
		.session.last_will.topic = lwt_topic,
		.session.last_will.msg = "offline",
		.session.last_will.qos = 1,
		.session.last_will.retain = 1,
	};

	mqttClient = esp_mqtt_client_init(&mqttCfg);
	esp_mqtt_client_register_event(mqttClient, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

	// ---
	xSemaphoreGive(xOneShotReqTelemetry= xSemaphoreCreateBinary());

	// Creation of the queue for MDB sessions and the main MDB task
	mdbSessionQueue = xQueueCreate(3 /*queue-length*/, sizeof(uint16_t));
	xTaskCreate(mdb_cashless_loop, "cashless_loop", 4096, (void*) 0, 1, (void*) 0);
}
