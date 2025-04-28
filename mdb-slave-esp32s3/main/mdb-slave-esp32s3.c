/*
 * mdb-slave-esp32.c - Vending machine peripheral
 *
 * Written by Leonardo Soares <leonardobsi@gmail.com>
 *
 */
#include <stdio.h>
#include <string.h>
#include <math.h>

#include <freertos/FreeRTOS.h>
#include <esp_private/wifi.h>
#include <nvs_flash.h>
#include <mqtt_client.h>
#include <esp_timer.h>

#include <driver/gpio.h>
#include <rom/ets_sys.h>
#include <esp_random.h>

#include "bleprph.h"
#include "nimble.h"

#include "rsacipher.h"

// Pin definitions for MDB communication
#define pin_mdb_rx  GPIO_NUM_14  // Pin to receive data from MDB
#define pin_mdb_tx  GPIO_NUM_27  // Pin to transmit data to MDB
#define pin_mdb_led GPIO_NUM_25  // LED to indicate MDB state

// Functions for scale factor conversion
#define to_scale_factor(p, x, y) (p / x / pow(10, -(y) ))  // Converts to scale factor
#define from_scale_factor(p, x, y) (p * x * pow(10, -(y) )) // Converts from scale factor

// MDB command definitions
#define ACK 0x00  // Acknowledgment / Checksum correct
#define RET 0xAA  // Retransmit previously sent data. Only VMC can send this
#define NAK 0xFF  // Negative acknowledgment

// Bit masks for MDB operations
#define BIT_MODE_SET  0b100000000
#define BIT_ADD_SET   0b011111000
#define BIT_CMD_SET   0b000000111

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

// MQTT client handle
esp_mqtt_client_handle_t mqttClient = (void*) 0;

#define BLE_PAYLOAD_SIZE 10

// Defining session pipes for different communication types
enum MDB_SESSION_PIPE {
	PIPE_BLE = 0x00, PIPE_MQTT = 0x01
};

// Structure for MDB flow (reading data, controlling machine state)
struct flow_mdb_session_msg_t {
	uint8_t pipe;
	char uuid[36];
	uint16_t sequential;
	uint16_t fundsAvailable;
	uint16_t itemPrice;
	uint16_t itemNumber;
};

// Message queues for communication
static QueueHandle_t mdbSessionQueue = (void*) 0;

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

	if (checksum) *checksum += coming_read;

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
void transmitPayloadByUART9(uint8_t *mdb_payload, uint8_t length) {

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
void mdb_main_loop(void *pvParameters) {

	// Declaration of the current MDB session structure
	struct flow_mdb_session_msg_t mdbCurrentSession;

	// Configure the MDB RX pin as input and the MDB TX pin as output
	gpio_set_direction(pin_mdb_rx, GPIO_MODE_INPUT);
	gpio_set_direction(pin_mdb_tx, GPIO_MODE_OUTPUT);

	// Payload buffer and available transmission flag
	uint8_t mdb_payload[256];
	uint8_t available_tx = 0;

	for (;;) {

		// Checksum calculation
		uint8_t checksum = 0x00;

		// Read from MDB and check if the mode bit is set
		uint16_t coming_read = read_9(&checksum);

		if (coming_read & BIT_MODE_SET) {

			// Check for common MDB responses (ACK, RET, NAK)
			if ((uint8_t) coming_read == ACK) {
				// ACK (Acknowledgement)
			} else if ((uint8_t) coming_read == RET) {
				// RET (Return)
			} else if ((uint8_t) coming_read == NAK) {
				// NAK (Negative Acknowledgement)
			} else if ((coming_read & BIT_ADD_SET) == 0x10) {

				// Reset transmission availability
				available_tx = 0;

				// Command decoding based on incoming data
				switch (coming_read & BIT_CMD_SET) {

				case RESET: {
					// Handle RESET command
					uint8_t checksum_ = read_9((uint8_t*) 0);

					// Reset during VEND_STATE is interpreted as VEND_SUCCESS
					if (machine_state == VEND_STATE) {
						// VEND_SUCCESS interpretation
					}

					machine_state = INACTIVE_STATE;
					cashless_reset_todo = true;
					break;
				}
				case SETUP: {
					// Handle SETUP command
					switch (read_9(&checksum)) {

					case CONFIG_DATA: {
						// Configuration Data handling
						uint8_t vmcFeatureLevel = read_9(&checksum);
						uint8_t vmcColumnsOnDisplay = read_9(&checksum);
						uint8_t vmcRowsOnDisplay = read_9(&checksum);
						uint8_t vmcDisplayInfo = read_9(&checksum);

						uint8_t checksum_ = read_9((uint8_t*) 0);

						machine_state = DISABLED_STATE;

						// Setting up the reader configuration payload
						mdb_payload[0] = 0x01;        	// Reader Config Data
						mdb_payload[1] = 1;           	// Reader Feature Level
						mdb_payload[2] = 0xff;        	// Country Code High
						mdb_payload[3] = 0xff;        	// Country Code Low
						mdb_payload[4] = 1;           	// Scale Factor
						mdb_payload[5] = 2;           	// Decimal Places
						mdb_payload[6] = 5; 			// Maximum Response Time (5s)
						mdb_payload[7] = 0b00001001;  	// Miscellaneous Options
						available_tx = 8;

						break;
					}

					case MAX_MIN_PRICES: {
						// Handling maximum and minimum price settings
						uint16_t maxPrice = (read_9(&checksum) << 8) | read_9(&checksum);
						uint16_t minPrice = (read_9(&checksum) << 8) | read_9(&checksum);

						uint8_t checksum_ = read_9((uint8_t*) 0);
						break;
					}
					}
					break;
				}
				case POLL: {
					// Handle POLL command
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

						uint16_t vendAmount = to_scale_factor(mdbCurrentSession.itemPrice, 1, 2);
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

						uint16_t fundsAvailable = to_scale_factor(mdbCurrentSession.fundsAvailable, 1, 2);
						mdb_payload[0] = 0x03;
						mdb_payload[1] = fundsAvailable >> 8;
						mdb_payload[2] = fundsAvailable;
						available_tx = 3;
					}
					break;
				}
				case VEND: {
					// Handle VEND command
					switch (read_9(&checksum)) {
					case VEND_REQUEST: {
						// Handling a vend request
						uint16_t itemPrice = (read_9(&checksum) << 8) | read_9(&checksum);
						uint16_t itemNumber = (read_9(&checksum) << 8) | read_9(&checksum);

						uint8_t checksum_ = read_9((uint8_t*) 0);

						machine_state = VEND_STATE;

						mdbCurrentSession.itemNumber= itemNumber;
						mdbCurrentSession.itemPrice= itemPrice;

						if (mdbCurrentSession.pipe == PIPE_MQTT) {

							if (itemPrice < mdbCurrentSession.fundsAvailable) {
								vend_approved_todo = true;
							} else {
								vend_denied_todo = true;
							}
						}

						/* PIPE_BLE */
						uint16_t currentSequential = mdbCurrentSession.sequential;

						char ble_payload[BLE_PAYLOAD_SIZE];

						uint8_t chk= 0x00;

						chk ^= ble_payload[0] = 'a';
						chk ^= ble_payload[1] = itemPrice >> 8;
						chk ^= ble_payload[2] = itemPrice;
						chk ^= ble_payload[3] = itemNumber >> 8;
						chk ^= ble_payload[4] = itemNumber;
						chk ^= ble_payload[5] = currentSequential >> 8;
						chk ^= ble_payload[6] = currentSequential;
						chk ^= ble_payload[7] = 0x00;
						chk ^= ble_payload[8] = 0x00;
						ble_payload[9] = chk;

						sendBleNotification((char*) &ble_payload, sizeof(ble_payload));
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

						/* PIPE_BLE */
						char ble_payload[BLE_PAYLOAD_SIZE];

						uint8_t chk= 0x00;

						chk ^= ble_payload[0] = 'b';
						chk ^= ble_payload[1] = 0x00;
						chk ^= ble_payload[2] = 0x00;
						chk ^= ble_payload[3] = itemNumber >> 8;
						chk ^= ble_payload[4] = itemNumber;
						chk ^= ble_payload[5] = 0x00;
						chk ^= ble_payload[6] = 0x00;
						chk ^= ble_payload[7] = 0x00;
						chk ^= ble_payload[8] = 0x00;
						ble_payload[9] = chk;

						sendBleNotification((char*) &ble_payload, sizeof(ble_payload));

						break;
					}
					case VEND_FAILURE: {

						uint8_t checksum_ = read_9((uint8_t*) 0);

						machine_state = IDLE_STATE;

						/* PIPE_BLE */
						uint16_t currentSequential = mdbCurrentSession.sequential - 1;

						char ble_payload[BLE_PAYLOAD_SIZE];

						uint8_t chk= 0x00;

						chk ^= ble_payload[0] = 'c';
						chk ^= ble_payload[1] = mdbCurrentSession.itemPrice >> 8;
						chk ^= ble_payload[2] = mdbCurrentSession.itemPrice;
						chk ^= ble_payload[3] = mdbCurrentSession.itemNumber >> 8;
						chk ^= ble_payload[4] = mdbCurrentSession.itemNumber;
						chk ^= ble_payload[5] = currentSequential >> 8;
						chk ^= ble_payload[6] = currentSequential;
						chk ^= ble_payload[7] = 0x00;
						chk ^= ble_payload[8] = 0x00;
						ble_payload[9] = chk;

						sendBleNotification((char*) &ble_payload, sizeof(ble_payload));

						break;
					}
					case SESSION_COMPLETE: {

						uint8_t checksum_ = read_9((uint8_t*) 0);

						session_end_todo = true;

						/* PIPE_BLE */
						char ble_payload[BLE_PAYLOAD_SIZE];

						uint8_t chk= 0x00;

						chk ^= ble_payload[0] = 'd';
						chk ^= ble_payload[1] = 0x00;
						chk ^= ble_payload[2] = 0x00;
						chk ^= ble_payload[3] = 0x00;
						chk ^= ble_payload[4] = 0x00;
						chk ^= ble_payload[5] = 0x00;
						chk ^= ble_payload[6] = 0x00;
						chk ^= ble_payload[7] = 0x00;
						chk ^= ble_payload[8] = 0x00;
						ble_payload[9] = chk;

						sendBleNotification((char*) &ble_payload, sizeof(ble_payload));

						break;
					}
					case CASH_SALE: {

						uint16_t itemPrice = (read_9(&checksum) << 8) | read_9(&checksum);
						uint16_t itemNumber = (read_9(&checksum) << 8) | read_9(&checksum);

						uint8_t checksum_ = read_9((uint8_t*) 0);

						if(checksum_ == checksum){

							char payload[100];
							sprintf(payload, "item_number=%d,item_price=%d", itemNumber, itemPrice);
							esp_mqtt_client_publish(mqttClient, "/application/sale/machine00000", (char*) &payload, 0, 1, 0);
						}

						break;
					}
					}
					break;
				}
				case READER: {
					// Handle READER command
					switch (read_9(&checksum)) {
					case READER_DISABLE: {
						// Disable reader

						uint8_t checksum_ = read_9((uint8_t*) 0);
						machine_state = DISABLED_STATE;

						break;
					}
					case READER_ENABLE: {
						// Enable reader

						uint8_t checksum_ = read_9((uint8_t*) 0);
						machine_state = ENABLED_STATE;

						break;
					}
					}
					break;
				}
				}

				// Transmit the prepared payload via UART
				transmitPayloadByUART9((uint8_t*) &mdb_payload, available_tx);

				// Intended address
				gpio_set_level(pin_mdb_led, 1);

			} else {

				// Not the intended address
				gpio_set_level(pin_mdb_led, 0);
			}
		}
	}
}

/*
 char calc_crc_16(uint8_t *pCrc, uint8_t uData) {

 uint8_t oldData = uData;
 for (uint8_t iBit = 0; iBit < 8; iBit++, uData >>= 1) {

 if ((uData ^ *pCrc) & 0x01) {

 *pCrc >>= 1;
 *pCrc ^= 0xA001;

 } else *pCrc >>= 1;
 }

 return oldData;
 }
 */

/*
 void readTelemetryDex() {

 HardwareSerial serial1 = HardwareSerial(1);

 serial1.begin(9600, SERIAL_8N1, pin_dex_rx, pin_dex_tx, (settings.telemetry_selected != 2 ? true : false));
 serial1.setTimeout(3000);

 //----- First Handshake ---------------------------------
 serial1.write(0x05); // ENQ

 if (!serial1.find("\x10\x30")) {
 return;
 } // DLE '0'

 vTaskDelay(pdMS_TO_TICKS(10)); // ticks para ms

 unsigned int crc;

 crc = 0;
 serial1.write(0x10);                      	// DLE
 serial1.write(0x01);                      	// SOH
 serial1.write(calc_crc_16(&crc, '1'));
 serial1.write(calc_crc_16(&crc, '2'));
 serial1.write(calc_crc_16(&crc, '3'));
 serial1.write(calc_crc_16(&crc, '4'));
 serial1.write(calc_crc_16(&crc, '5'));
 serial1.write(calc_crc_16(&crc, '6'));
 serial1.write(calc_crc_16(&crc, '7'));
 serial1.write(calc_crc_16(&crc, '8'));
 serial1.write(calc_crc_16(&crc, '9'));
 serial1.write(calc_crc_16(&crc, '0'));
 serial1.write(calc_crc_16(&crc, 'R'));
 serial1.write(calc_crc_16(&crc, 'R'));
 serial1.write(calc_crc_16(&crc, '0'));
 serial1.write(calc_crc_16(&crc, '0'));
 serial1.write(calc_crc_16(&crc, 'L'));
 serial1.write(calc_crc_16(&crc, '0'));
 serial1.write(calc_crc_16(&crc, '6'));
 serial1.write(0x10);                      	// DLE
 serial1.write(calc_crc_16(&crc, 0x03)); 	// ETX
 serial1.write(crc % 256);
 serial1.write(crc / 256);

 if (!serial1.find("\x10\x31")) {
 return;
 } // DLE '1'

 vTaskDelay(pdMS_TO_TICKS(10)); // ticks para ms

 serial1.write(0x04); // EOT

 //--- Second Handshake -------------------------------
 if (!serial1.find("\x05")) {
 return;
 } // ENQ
 vTaskDelay(pdMS_TO_TICKS(10)); // ticks para ms

 serial1.write("\x10\x30"); // DLE '0'

 // <--- (DLE SOH) & Communication ID & Response Code & Revision&Level & (DLE ETX CRC-16)
 if (!serial1.find("\x10\x01")) {
 return;
 } // DLE SOH

 // ...

 if (!serial1.find("\x10\x03")) {
 return;
 } // DLE ETX

 vTaskDelay(pdMS_TO_TICKS(10)); // ticks para ms

 serial1.write("\x10\x31"); // DLE '1'

 if (!serial1.find("\x04")) {
 return;
 } // EOT

 //--- Data Transfer -------------------------------
 if (!serial1.find("\x05")) {
 return;
 } // ENQ

 File fileEva_dts = SPIFFS.open(TELEMETRY_FILE, FILE_WRITE);

 byte b = 0x00;
 do {

 vTaskDelay(pdMS_TO_TICKS(100)); // ticks para ms

 // clear buffer...
 while (serial1.available())
 serial1.read();

 serial1.write(0x10); // DLE
 serial1.write(0x30 | (b++ & 0x01)); // '0'|'1'

 // <--- (DLE STX) & (Audit Data (Block n)) & (DLE ETB|ETX CRC-16)

 // DLE STX
 if (serial1.find("\x10\x02")) {

 fileEva_dts.print(serial1.readStringUntil('\x10'));

 } else {

 // EOT
 break;
 }

 } while (true);

 fileEva_dts.close();
 }
 */

/*
 void readTelemetryDdcmp() {

 HardwareSerial serial1 = HardwareSerial(1);

 serial1.begin(2400, SERIAL_8N1, pin_dex_rx, pin_dex_tx, (settings.telemetry_selected != 2 ? true : false));
 serial1.setTimeout(3000);

 //-------------------------------------------------------
 byte buffer_rx[1024];
 byte seq_rr_ddcmp;
 byte seq_xx_ddcmp = 0;
 unsigned int n_bytes_message;
 unsigned int crc;
 byte last_package;

 crc = 0;
 // start...
 serial1.write(calc_crc_16(&crc, 0x05));
 serial1.write(calc_crc_16(&crc, 0x06));
 serial1.write(calc_crc_16(&crc, 0x40));
 serial1.write(calc_crc_16(&crc, 0x00));
 serial1.write(calc_crc_16(&crc, 0x00)); // mbd
 serial1.write(calc_crc_16(&crc, 0x01)); // sadd
 serial1.write(crc % 256);
 serial1.write(crc / 256);

 if (serial1.readBytes(buffer_rx, 8) != 8) {
 return;
 }
 if ((buffer_rx[0] != 0x05) || (buffer_rx[1] != 0x07)) {
 return;
 } // ...stack

 crc = 0;
 // data message header...
 serial1.write(calc_crc_16(&crc, 0x81));
 serial1.write(calc_crc_16(&crc, 0x10));            // nn
 serial1.write(calc_crc_16(&crc, 0x40));            // mm
 serial1.write(calc_crc_16(&crc, 0x00));            // rr
 serial1.write(calc_crc_16(&crc, ++seq_xx_ddcmp));  // xx
 serial1.write(calc_crc_16(&crc, 0x01));            // sadd
 serial1.write(crc % 256);
 serial1.write(crc / 256);

 crc = 0;
 // who are you...
 serial1.write(calc_crc_16(&crc, 0x77));
 serial1.write(calc_crc_16(&crc, 0xe0));
 serial1.write(calc_crc_16(&crc, 0x00));

 serial1.write(calc_crc_16(&crc, 0x00)); // security code
 serial1.write(calc_crc_16(&crc, 0x00));

 serial1.write(calc_crc_16(&crc, 0x00)); // pass code
 serial1.write(calc_crc_16(&crc, 0x00));

 serial1.write(calc_crc_16(&crc, 0x01)); // date dd mm yy
 serial1.write(calc_crc_16(&crc, 0x01));
 serial1.write(calc_crc_16(&crc, 0x70));
 serial1.write(calc_crc_16(&crc, 0x00)); // time hh mm ss
 serial1.write(calc_crc_16(&crc, 0x00));
 serial1.write(calc_crc_16(&crc, 0x00));
 serial1.write(calc_crc_16(&crc, 0x00)); // u2
 serial1.write(calc_crc_16(&crc, 0x00)); // u1
 serial1.write(calc_crc_16(&crc, 0x0c)); // 0b-Maintenance 0c-Route Person
 serial1.write(crc % 256);
 serial1.write(crc / 256);

 if (serial1.readBytes(buffer_rx, 8) != 8) {
 return;
 }
 if ((buffer_rx[0] != 0x05) || (buffer_rx[1] != 0x01)) {
 return;
 } // ...ack

 if (serial1.readBytes(buffer_rx, 8) != 8) {
 return;
 }
 if (buffer_rx[0] != 0x81) {
 return;
 } // ...data message header

 seq_rr_ddcmp = buffer_rx[4];

 n_bytes_message = ((buffer_rx[2] & 0x3f) * 256) + buffer_rx[1];
 n_bytes_message += 2; // crc16
 if (serial1.readBytes(buffer_rx, n_bytes_message) != n_bytes_message) {
 return;
 }
 //  if (buffer_rx[2] != 0x01) {
 //    return;
 //  }
 // ...not rejected

 crc = 0;
 // ack...
 serial1.write(calc_crc_16(&crc, 0x05));
 serial1.write(calc_crc_16(&crc, 0x01));
 serial1.write(calc_crc_16(&crc, 0x40));
 serial1.write(calc_crc_16(&crc, seq_rr_ddcmp)); // rr
 serial1.write(calc_crc_16(&crc, 0x00));
 serial1.write(calc_crc_16(&crc, 0x01));         // sadd
 serial1.write(crc % 256);
 serial1.write(crc / 256); // Transmitiu ACK (05 01 40 01 00 01 B8 55)

 crc = 0;
 // data message header...
 serial1.write(calc_crc_16(&crc, 0x81));
 serial1.write(calc_crc_16(&crc, 0x09));                   // nm
 serial1.write(calc_crc_16(&crc, 0x40));                   // mm
 serial1.write(calc_crc_16(&crc, seq_rr_ddcmp));           // rr
 serial1.write(calc_crc_16(&crc, ++seq_xx_ddcmp));         // xx
 serial1.write(calc_crc_16(&crc, 0x01));                   // sadd
 serial1.write(crc % 256);
 serial1.write(crc / 256); // Transmitiu DATA_HEADER (81 09 40 01 02 01 46 B0)

 crc = 0;
 serial1.write(calc_crc_16(&crc, 0x77));
 serial1.write(calc_crc_16(&crc, 0xE2));
 serial1.write(calc_crc_16(&crc, 0x00));
 serial1.write(calc_crc_16(&crc, 0x02)); // security read list (Standard audit data is read without resetting the interim data. (Read only) )
 serial1.write(calc_crc_16(&crc, 0x01));
 serial1.write(calc_crc_16(&crc, 0x00));
 serial1.write(calc_crc_16(&crc, 0x00));
 serial1.write(calc_crc_16(&crc, 0x00));
 serial1.write(calc_crc_16(&crc, 0x00));
 serial1.write(crc % 256);
 serial1.write(crc / 256); // Transmitiu READ_DATA/Audit Collection List (77 E2 00 01 01 00 00 00 00 F0 72)

 if (serial1.readBytes(buffer_rx, 8) != 8) {
 return;
 }
 if ((buffer_rx[0] != 0x05) || (buffer_rx[1] != 0x01)) {
 return;
 } // ...ack

 if (serial1.readBytes(buffer_rx, 8) != 8) {
 return;
 }
 if (buffer_rx[0] != 0x81) {
 return;
 } // DATA HEADER

 seq_rr_ddcmp = buffer_rx[4];

 n_bytes_message = ((buffer_rx[2] & 0x3f) * 256) + buffer_rx[1];
 n_bytes_message += 2; // crc16
 if (serial1.readBytes(buffer_rx, n_bytes_message) != n_bytes_message) {
 return;
 }
 if (buffer_rx[2] != 0x01) {
 return;
 }

 crc = 0;
 serial1.write(calc_crc_16(&crc, 0x05));
 serial1.write(calc_crc_16(&crc, 0x01));
 serial1.write(calc_crc_16(&crc, 0x40));
 serial1.write(calc_crc_16(&crc, seq_rr_ddcmp));
 serial1.write(calc_crc_16(&crc, 0x00));
 serial1.write(calc_crc_16(&crc, 0x01));
 serial1.write(crc % 256);
 serial1.write(crc / 256); // Transmitiu ACK

 File fileEva_dts = SPIFFS.open(TELEMETRY_FILE, FILE_WRITE);

 do {
 if (serial1.readBytes(buffer_rx, 8) != 8) {
 break;
 }
 if (buffer_rx[0] != 0x81) {
 break;
 } // ...data header

 seq_rr_ddcmp = buffer_rx[4];
 last_package = buffer_rx[2] & 0x80;

 n_bytes_message = ((buffer_rx[2] & 0x3f) * 256) + buffer_rx[1];
 n_bytes_message += 2; // crc16

 if (serial1.readBytes(buffer_rx, n_bytes_message) != n_bytes_message) {
 break;
 } // dados

 // Os dados recebidos são: 99 nn "audit dada" crc crc, ou seja, as informaões de audit estão da posição 2 do buffer_rx à posição n_bytes-3
 for (int x = 2; x < n_bytes_message - 2; x++)
 fileEva_dts.print((char) buffer_rx[x]);

 crc = 0;
 serial1.write(calc_crc_16(&crc, 0x05));
 serial1.write(calc_crc_16(&crc, 0x01));
 serial1.write(calc_crc_16(&crc, 0x40));
 serial1.write(calc_crc_16(&crc, seq_rr_ddcmp));
 serial1.write(calc_crc_16(&crc, 0x00));
 serial1.write(calc_crc_16(&crc, 0x01));
 serial1.write(crc % 256);
 serial1.write(crc / 256); // Transmitiu ACK

 if (last_package) {

 crc = 0;
 serial1.write(calc_crc_16(&crc, 0x81));
 serial1.write(calc_crc_16(&crc, 0x02));         // nn
 serial1.write(calc_crc_16(&crc, 0x40));         // mm
 serial1.write(calc_crc_16(&crc, seq_rr_ddcmp)); // rr
 serial1.write(calc_crc_16(&crc, 0x03));         // xx
 serial1.write(calc_crc_16(&crc, 0x01));         // sadd
 serial1.write(crc % 256);
 serial1.write(crc / 256); // Transmitiu DATA HEADER

 serial1.write(0x77);
 serial1.write(0xFF);
 serial1.write(0x67);
 serial1.write(0xB0);   // Transmitiu FINIS

 if (serial1.readBytes(buffer_rx, 8) != 8) {
 break;
 }
 if ((buffer_rx[0] != 0x05) || (buffer_rx[1] != 0x01)) {
 break;
 } // ACK

 break;
 }

 vTaskDelay(pdMS_TO_TICKS(10)); // ticks para ms

 } while (true);

 fileEva_dts.close();
 }*/

void ping_callback(void *arg) {
	esp_mqtt_client_publish(mqttClient, "/application/ping/machine00000", "ping", 0, 1, 0);
}

void ble_event_handler(char *event_data) {
	printf(">_ %s\n", event_data);

	if (strncmp(event_data, "e", 1) == 0) {
//    	Starting a vending session...

		struct flow_mdb_session_msg_t msg = { .pipe = PIPE_BLE, .fundsAvailable = 0xffff };

		xQueueSend(mdbSessionQueue, &msg, 0 /*if full, do not wait*/);

	} else if (strncmp(event_data, "f", 1) == 0) {
//    	Approve the vending session...

		char ble_payload[BLE_PAYLOAD_SIZE];
		memcpy(ble_payload, event_data, sizeof(ble_payload));

		uint8_t chk= 0x00;

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
//        assert(sequential == (mdbSessionOwner->sequential + 1) );

		vend_approved_todo = (machine_state == VEND_STATE) ? true : false;

	} else if (strncmp(event_data, "g", 1) == 0) {
//    	Close the vending session...

		session_cancel_todo = (machine_state == IDLE_STATE) ? true : false;
		vend_denied_todo = (machine_state == VEND_STATE) ? true : false;
	}
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {

	esp_mqtt_event_handle_t event = event_data;
	esp_mqtt_client_handle_t client = event->client;

	switch ((esp_mqtt_event_id_t) event_id) {
	case MQTT_EVENT_CONNECTED:

		esp_mqtt_client_subscribe(client, "/iot/machine00000/#", 0);

		esp_mqtt_client_publish(client, "/application/poweron/machine00000", "poweron", 0, 1, 0);

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
			if (strncmp(event->data, "restart", 7) == 0) {
				esp_restart();
			}
		}

		// Starting a new vending session...
		if (strncmp(event->topic, "/iot/machine00000/session", 25) == 0) {
			if (strncmp(event->data, "a", 1) == 0) {

				struct flow_mdb_session_msg_t msg = { .pipe = PIPE_MQTT };

				// a,150,87dfae4b-f021-40cd-a1dd-89c68d53ecb1
				sscanf(event->data, "%*1s,%hu,%36s", (uint16_t*) &msg.fundsAvailable, (char*) &msg.uuid);

				xQueueSend(mdbSessionQueue, &msg, 0 /*if full, do not wait*/);
			}
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

	switch (event_id) {
	case WIFI_EVENT_STA_START:
		esp_wifi_connect();
		break;
	case WIFI_EVENT_STA_CONNECTED:
		break;
	case WIFI_EVENT_STA_DISCONNECTED:
		esp_wifi_connect();
		break;
	}
}

void app_main(void) {

	// LED configuration as output and initial activation
	gpio_set_direction(pin_mdb_led, GPIO_MODE_OUTPUT);
	gpio_set_level(pin_mdb_led, 1);

	// Initialization of non-volatile flash memory (NVS)
	nvs_flash_init();

	// Initialization of the network stack and event loop
	esp_netif_init();
	esp_event_loop_create_default();
	esp_netif_create_default_wifi_sta();

	// Wi-Fi interface configuration and initialization
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	esp_wifi_init(&cfg);

	// Registering the Wi-Fi event handler
	esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, (void*) 0, (void*) 0);

	// Wi-Fi network configuration (SSID and password)
	wifi_config_t wifi_config = {
			.sta = {
					.ssid = "ALLREDE-SOARES-2G", 	// Wi-Fi network name (SSID)
					.password = "julia2012",        // Network password
					.threshold.authmode = WIFI_AUTH_WPA2_PSK, // WPA2-PSK authentication mode
			}, };

	// Setting Wi-Fi mode to station (STA) and starting Wi-Fi
	esp_wifi_set_mode(WIFI_MODE_STA);
	esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
	esp_wifi_start();

	// MQTT client configuration (Eclipse MQTT Broker)
	const esp_mqtt_client_config_t mqttCfg = { .broker.address.uri = "mqtt://mqtt.eclipseprojects.io", /*MQTT broker URI*/ };

	// Initialization and registration of MQTT client events
	mqttClient = esp_mqtt_client_init(&mqttCfg);
	esp_mqtt_client_register_event(mqttClient, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
	esp_mqtt_client_start(mqttClient);

	// Configuration and creation of a periodic timer
	const esp_timer_create_args_t timer_args = {
			.callback = &ping_callback, /*Timer callback function*/
			.arg = (void*) 0, /*Callback function argument*/
			.name = "ping_timer" /*Timer name*/ };

	esp_timer_handle_t periodic_timer;
	esp_timer_create(&timer_args, &periodic_timer);

	// Start the periodic timer with an interval of 5 minutes (300,000,000 microseconds)
	esp_timer_start_periodic(periodic_timer, 300000000);

	// Initialization of Bluetooth Low Energy (BLE) with the machine name
	startBle("machine00000", ble_event_handler);

	// Creation of the queue for MDB sessions and the main MDB task
	mdbSessionQueue = xQueueCreate(16 /*queue-length*/, sizeof(struct flow_mdb_session_msg_t));
	xTaskCreate(mdb_main_loop, "main_loop", 4096, (void*) 0, 1, (void*) 0);
}
