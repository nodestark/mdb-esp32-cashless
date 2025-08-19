/*
 * mdb-slave-esp32.c - Vending machine peripheral
 *
 * Written by Leonardo Soares <leonardobsi@gmail.com>
 *
 */

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

#define TIMEOUT_IDLE_US 	(90 * 1000000ULL)  // 90 segundos em microssegundos

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

machine_state_t machine_state = INACTIVE_STATE; // Initial machine state

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

static int64_t state_start_time_us = 0;

// MQTT client handle
esp_mqtt_client_handle_t mqttClient = (void*) 0;

// Defining session pipes for different communication types
enum MDB_SESSION_PIPE {
	PIPE_BLE = 0x00, PIPE_MQTT = 0x01
};

// Structure for MDB flow (reading data, controlling machine state)
struct flow_mdb_session_msg_t {
	uint8_t pipe;
	uint16_t sequential;
	uint16_t fundsAvailable;
	uint16_t itemPrice;
	uint16_t itemNumber;
};

// Message queues for communication
static QueueHandle_t mdbSessionQueue = (void*) 0;

void transmitPayloadByBLE(char cmd, uint16_t itemPrice, uint16_t itemNumber, uint16_t sequential) {

	char ble_payload[10];

	uint8_t chk = 0x00;

	chk ^= ble_payload[0] = cmd;
	chk ^= ble_payload[1] = itemPrice >> 8;
	chk ^= ble_payload[2] = itemPrice;
	chk ^= ble_payload[3] = itemNumber >> 8;
	chk ^= ble_payload[4] = itemNumber;
	chk ^= ble_payload[5] = sequential >> 8;
	chk ^= ble_payload[6] = sequential;
	chk ^= ble_payload[7] = 0x00;
	chk ^= ble_payload[8] = 0x00;

	ble_payload[9] = chk;

	sendBleNotification((char*) &ble_payload, sizeof(ble_payload));
}

// Function to read 9 bits from MDB (one byte at a time)
uint16_t read_9(uint8_t *checksum) {

	uint16_t coming_read = 0;

	// Wait until the RX signal is 0
	while (gpio_get_level(pin_mdb_rx))
		;

	ets_delay_us(156); // Delay between bits

	for (uint8_t x = 0; x < 9 /*9bits*/; x++) {

		coming_read |= (gpio_get_level(pin_mdb_rx) << x);
		ets_delay_us(104); // 9600bps timing
	}

	if (checksum)
		*checksum += coming_read;

	return coming_read;
}

// Function to write 9 bits to MDB (one byte at a time)
void write_9(uint16_t nth9) {

	gpio_set_level(pin_mdb_tx, 0); // Start transmission
	ets_delay_us(104);

	for (uint8_t x = 0; x < 9 /*9bits*/; x++) {

		gpio_set_level(pin_mdb_tx, (nth9 >> x) & 1);
		ets_delay_us(104); // 9600bps timing
	}

	gpio_set_level(pin_mdb_tx, 1); // End transmission
	ets_delay_us(104);
}

// Function to transmit the payload via UART9 (using MDB protocol)
void writePayload_ttl9(uint8_t *mdb_payload, uint8_t length) {

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

	// Declaration of the current MDB session structure
	struct flow_mdb_session_msg_t mdbCurrentSession;

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

						uint16_t vendAmount = mdbCurrentSession.itemPrice;
						mdb_payload[0] = 0x05;
						mdb_payload[1] = vendAmount >> 8;
						mdb_payload[2] = vendAmount;
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

					} else if (machine_state == ENABLED_STATE && xQueueReceive(mdbSessionQueue, &mdbCurrentSession, 0)) {
						// Begin session
						session_begin_todo = false;

						machine_state = IDLE_STATE;

						state_start_time_us = esp_timer_get_time();

						uint16_t fundsAvailable = mdbCurrentSession.fundsAvailable;
						mdb_payload[0] = 0x03;
						mdb_payload[1] = fundsAvailable >> 8;
						mdb_payload[2] = fundsAvailable;
						available_tx = 3;

					} else if (session_cancel_todo) {
						// Cancel session
						session_cancel_todo = false;

						mdb_payload[0] = 0x04;
						available_tx = 1;

					} else {

						int64_t now = esp_timer_get_time();
						int64_t elapsed = now - state_start_time_us;

						if (machine_state >= IDLE_STATE && elapsed > TIMEOUT_IDLE_US) {
							session_cancel_todo = true;
						}
					}

					break;
				}
				case VEND: {
					switch (read_9(&checksum)) {
					case VEND_REQUEST: {

						uint16_t itemPrice = (read_9(&checksum) << 8) | read_9(&checksum);
						uint16_t itemNumber = (read_9(&checksum) << 8) | read_9(&checksum);

						uint8_t checksum_ = read_9((uint8_t*) 0);

						machine_state = VEND_STATE;

						mdbCurrentSession.itemNumber = itemNumber;
						mdbCurrentSession.itemPrice = itemPrice;

						if (mdbCurrentSession.pipe == PIPE_MQTT) {

							if (itemPrice < mdbCurrentSession.fundsAvailable) {
								vend_approved_todo = true;
							} else {
								vend_denied_todo = true;
							}
						}

						/* PIPE_BLE */

						transmitPayloadByBLE('a', mdbCurrentSession.itemPrice, mdbCurrentSession.itemNumber, mdbCurrentSession.sequential);

						break;
					}
					case VEND_CANCEL: {

						uint8_t checksum_ = read_9((uint8_t*) 0);

						vend_denied_todo = true;

						break;
					}
					case VEND_SUCCESS: {

						uint16_t itemNumber = (read_9(&checksum) << 8) | read_9(&checksum);

						uint8_t checksum_ = read_9((uint8_t*) 0);

						machine_state = IDLE_STATE;

						mdbCurrentSession.itemNumber = itemNumber;

						/* PIPE_BLE */

						transmitPayloadByBLE('b', mdbCurrentSession.itemPrice, mdbCurrentSession.itemNumber, mdbCurrentSession.sequential);

						break;
					}
					case VEND_FAILURE: {

						uint8_t checksum_ = read_9((uint8_t*) 0);

						machine_state = IDLE_STATE;

						/* PIPE_BLE */

						transmitPayloadByBLE('c', mdbCurrentSession.itemPrice, mdbCurrentSession.itemNumber, mdbCurrentSession.sequential - 1);

						break;
					}
					case SESSION_COMPLETE: {

						uint8_t checksum_ = read_9((uint8_t*) 0);

						session_end_todo = true;

						/* PIPE_BLE */

						transmitPayloadByBLE('d', mdbCurrentSession.itemPrice, mdbCurrentSession.itemNumber, mdbCurrentSession.sequential);

						break;
					}
					case CASH_SALE: {

						uint16_t itemPrice = (read_9(&checksum) << 8) | read_9(&checksum);
						uint16_t itemNumber = (read_9(&checksum) << 8) | read_9(&checksum);

						uint8_t checksum_ = read_9((uint8_t*) 0);

						if (checksum_ == checksum) {

							char payload[100];
							sprintf(payload, "item_number=%d,item_price=%d", itemNumber, itemPrice);
							esp_mqtt_client_publish(mqttClient, "/app/machine00000/sale", (char*) &payload, 0, 1, 0);
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
				}

				// Transmit the prepared payload via UART
				writePayload_ttl9((uint8_t*) &mdb_payload, available_tx);

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

//	uart_set_line_inverse(UART_NUM_1, UART_SIGNAL_RXD_INV | UART_SIGNAL_TXD_INV);
	uart_set_line_inverse(UART_NUM_1, 0);

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

	uart_set_baudrate(UART_NUM_1, 9600);

//	uart_set_line_inverse(UART_NUM_1, UART_SIGNAL_RXD_INV | UART_SIGNAL_TXD_INV);
	uart_set_line_inverse(UART_NUM_1, 0);

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

void ping_callback(void *arg) {
	esp_mqtt_client_publish(mqttClient, "/app/machine00000/ping", "1", 0, 1, 0);
}

void ble_event_handler(char *event_data) {
	printf(">_ %s\n", event_data);

	if (strncmp(event_data, "e", 1) == 0) {
//    	Starting a vending session...

		struct flow_mdb_session_msg_t msg = { .pipe = PIPE_BLE, .sequential = esp_random(), .fundsAvailable = 0xffff };

		xQueueSend(mdbSessionQueue, &msg, 0 /*if full, do not wait*/);

	} else if (strncmp(event_data, "f", 1) == 0) {
//    	Approve the vending session...

		char ble_payload[10];
		memcpy(ble_payload, event_data, sizeof(ble_payload));

		uint8_t chk = 0x00;

		chk ^= ble_payload[0];
		chk ^= ble_payload[1];
		chk ^= ble_payload[2];
		chk ^= ble_payload[3];
		chk ^= ble_payload[4];
		chk ^= ble_payload[5];
		chk ^= ble_payload[6];
		chk ^= ble_payload[7];
		chk ^= ble_payload[8];
//		assert(ble_payload[9] == chk);

		uint16_t sequential = (ble_payload[5] << 8) | ble_payload[6];
//		assert(sequential == (mdbSessionOwner->sequential + 1) );

		vend_approved_todo = (machine_state == VEND_STATE) ? true : false;

	} else if (strncmp(event_data, "g", 1) == 0) {
//    	Close the vending session...

		session_cancel_todo = (machine_state >= IDLE_STATE) ? true : false;

	} else if (strncmp(event_data, "h", 1) == 0) {
	    xTaskCreate(requestTelemetryData, "requestTelemetry", 2048, NULL, 1, NULL);
	}
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {

	esp_mqtt_event_handle_t event = event_data;
	esp_mqtt_client_handle_t client = event->client;

	switch ((esp_mqtt_event_id_t) event_id) {
	case MQTT_EVENT_CONNECTED:

		esp_mqtt_client_subscribe(client, "/iot/machine00000/#", 0);

		esp_mqtt_client_publish(client, "/app/machine00000/poweron", "1", 0, 1, 0);

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

		printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
		printf("DATA=%.*s\r\n", event->data_len, event->data);

		if (strncmp(event->topic, "/iot/machine00000/restart", 25) == 0) {
			esp_restart();
		}

		if (strncmp(event->topic, "/iot/machine00000/telemetry", 25) == 0) {
		    xTaskCreate(requestTelemetryData, "OneShotTelemetry", 2048, NULL, 1, NULL);
		}

		if (strncmp(event->topic, "/iot/machine00000/session/e", 27) == 0) {

			struct flow_mdb_session_msg_t msg = { .pipe = PIPE_MQTT };

			// 150,<sequential>
			sscanf(event->data, "%hu,%hu", (uint16_t*) &msg.fundsAvailable, (uint16_t*) &msg.sequential);

			xQueueSend(mdbSessionQueue, &msg, 0 /*if full, do not wait*/);
		}

		break;
	case MQTT_EVENT_ERROR:
		break;
	default:
		printf("Other event id:%d\n", event->event_id);
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
			sntp_setoperatingmode(SNTP_OPMODE_POLL);
			sntp_setservername(0, "pool.ntp.org");
			sntp_init();

			//
			esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

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

	esp_netif_init();
	esp_event_loop_create_default();
	esp_netif_create_default_wifi_sta();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	esp_wifi_init(&cfg);

	esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, (void*) 0, (void*) 0);
	esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, (void*) 0, (void*) 0);

	wifi_config_t wifi_config = {
			.sta = {
					.ssid = "ALLREDE-SOARES-2G", 	// Wi-Fi network name (SSID)
					.password = "julia2012",        // Network password
					.threshold.authmode = WIFI_AUTH_WPA2_PSK, // WPA2-PSK authentication mode
			}, };

	esp_wifi_set_mode(WIFI_MODE_STA);
	esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
	esp_wifi_start();

	// MQTT client configuration (Eclipse MQTT Broker)
	const esp_mqtt_client_config_t mqttCfg = { .broker.address.uri = "mqtt://mqtt.eclipseprojects.io", /*MQTT broker URI*/};

	mqttClient = esp_mqtt_client_init(&mqttCfg);
	esp_mqtt_client_register_event(mqttClient, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

	// Configuration and creation of a periodic timer
	const esp_timer_create_args_t timer_args = {
			.callback = &ping_callback, /*Timer callback function*/
			.arg = (void*) 0, /*Callback function argument*/
			.name = "ping_timer" /*Timer name*/ };

	esp_timer_handle_t periodic_timer;
	esp_timer_create(&timer_args, &periodic_timer);

	esp_timer_start_periodic(periodic_timer, 300000000);

	// Initialization of Bluetooth Low Energy (BLE) with the machine name
	startBle("0.vmflow.xyz", ble_event_handler);

	xSemaphoreGive(xOneShotReqTelemetry= xSemaphoreCreateBinary());

	// Creation of the queue for MDB sessions and the main MDB task
	mdbSessionQueue = xQueueCreate(16 /*queue-length*/, sizeof(struct flow_mdb_session_msg_t));
	xTaskCreate(mdb_cashless_loop, "cashless_loop", 4096, (void*) 0, 1, (void*) 0);
}
