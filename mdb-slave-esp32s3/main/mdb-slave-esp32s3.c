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

#include "bleprph.h"
#include "nimble.h"

esp_mqtt_client_handle_t mqttClient = (void*) 0;

struct flow_cash_sale_msg_t {
	uint16_t itemPrice;
	uint16_t itemNumber;
};
static QueueHandle_t cash_sale_queue = (void*) 0;

//	################################################################
#define pin_mdb_rx  GPIO_NUM_14
#define pin_mdb_tx  GPIO_NUM_27

#define pin_mdb_led  GPIO_NUM_25

#define to_scale_factor(p, x, y) (p / x / pow(10, -(y) ))
#define from_scale_factor(p, x, y) (p * x * pow(10, -(y) ))

#define ACK 0x00  // Acknowledgment / Checksum correct;
#define RET 0xAA  // Retransmit the previously sent data. Only the VMC can transmit this byte;
#define NAK 0xFF  // Negative acknowledge.

#define BIT_MODE_SET  0b100000000
#define BIT_ADD_SET   0b011111000
#define BIT_CMD_SET   0b000000111

enum MDB_COMMAND {
	RESET = 0x00,
	SETUP = 0x01,
	POLL = 0x02,
	VEND = 0x03,
	READER = 0x04,
	EXPANSION = 0x07
};

enum MDB_SETUP_DATA {
	CONFIG_DATA = 0x00, MAX_MIN_PRICES = 0x01
};

enum MDB_VEND_DATA {
	VEND_REQUEST = 0x00,
	VEND_CANCEL = 0x01,
	VEND_SUCCESS = 0x02,
	VEND_FAILURE = 0x03,
	SESSION_COMPLETE = 0x04,
	CASH_SALE = 0x05
};

enum MDB_READER_DATA {
	READER_DISABLE = 0x00, READER_ENABLE = 0x01, READER_CANCEL = 0x02
};

enum MDB_EXPANSION_DATA {
	REQUEST_ID = 0x00
};

typedef enum MACHINE_STATE {
	INACTIVE_STATE, DISABLED_STATE, ENABLED_STATE, IDLE_STATE, VEND_STATE
} machine_state_t;

machine_state_t machine_state = INACTIVE_STATE;

uint8_t session_begin_todo = false;
uint8_t session_cancel_todo = false;
uint8_t session_end_todo = false;
//
uint8_t vend_approved_todo = false;
uint8_t vend_denied_todo = false;
//
uint8_t cashless_reset_todo = false;
uint8_t outsequence_todo = false;

uint16_t read_9(uint8_t *checksum) {

	uint16_t coming_read = 0;

	while (gpio_get_level(pin_mdb_rx))
		;

	ets_delay_us(156);
	for (uint8_t x = 0; x < 9 /*9bits*/; x++) {

		coming_read |= (gpio_get_level(pin_mdb_rx) << x);
		ets_delay_us(104); // 9600bps
	}

	if(checksum) *checksum += coming_read;

	return coming_read;
}

void write_9(uint16_t nth9) {

	gpio_set_level(pin_mdb_tx, 0); // start
	ets_delay_us(104);

	for (uint8_t x = 0; x < 9 /*9bits*/; x++) {

		gpio_set_level(pin_mdb_tx, (nth9 >> x) & 1);
		ets_delay_us(104); // 9600bps
	}

	gpio_set_level(pin_mdb_tx, 1); // stop
	ets_delay_us(104);
}

void transmitPayloadByUART9(uint8_t *mdb_payload, uint8_t length) {

	uint8_t checksum = 0;
	for (int x = 0; x < length; x++) {

		checksum += mdb_payload[x];
		write_9(mdb_payload[x]);
	}

	// CHK* ACK*
	write_9(BIT_MODE_SET | checksum);
}

void mdb_loop(void *pvParameters) {

	gpio_set_direction(pin_mdb_rx, GPIO_MODE_INPUT);
	gpio_set_direction(pin_mdb_tx, GPIO_MODE_OUTPUT);

	uint8_t mdb_payload[256];
	uint8_t available_tx = 0;

	for (;;) {

		uint8_t checksum = 0x00;

		uint16_t coming_read = read_9(&checksum);

		if( coming_read & BIT_MODE_SET ){

			if( (uint8_t) coming_read == ACK ){
				// ACK
			} else if( (uint8_t) coming_read == RET ){
				// RET
			} else if( (uint8_t) coming_read == NAK ){
				// NAK
			} else if((coming_read & BIT_ADD_SET) == 0x10){

				gpio_set_level(pin_mdb_led, 1);

				available_tx = 0;

				switch (coming_read & BIT_CMD_SET) {
				case RESET: {

					read_9((uint8_t*) 0);

					if (machine_state == VEND_STATE) {
						// A reset between VEND APPROVED and VEND SUCCESS shall be interbreted as a VEND SUCCESS...
					}

					machine_state = INACTIVE_STATE;
					cashless_reset_todo = true;
					break;
				}
				case SETUP: {

					switch (read_9(&checksum)) {
					case CONFIG_DATA: {

						uint8_t vmcFeatureLevel = read_9(&checksum);
						uint8_t vmcColumnsOnDisplay= read_9(&checksum);
						uint8_t vmcRowsOnDisplay= read_9(&checksum);
						uint8_t vmcDisplayInfo= read_9(&checksum);

						read_9((uint8_t*) 0);

						machine_state = DISABLED_STATE;

						mdb_payload[0] = 0x01;       	// Reader Config Data
						mdb_payload[1] = 1; 			// Reader Feature Level. 1, 2, 3
						mdb_payload[2] = 0xff;       	// Country Code High;
						mdb_payload[3] = 0xff;       	// Country Code Low;
						mdb_payload[4] = 1;       		// Scale Factor
						mdb_payload[5] = 2;       		// Decimal Places
						mdb_payload[6] = 5; 			// Application Maximum Response Time (5s)
						mdb_payload[7] = 0b00001001; 	// Miscellaneous Options

						available_tx = 8;

						break;
					}
					case MAX_MIN_PRICES: {

						uint16_t maxPrice = (read_9(&checksum) /*2*/ << 8) | read_9(&checksum) /*3*/;
						uint16_t minPrice = (read_9(&checksum) /*4*/ << 8) | read_9(&checksum) /*5*/;

						read_9((uint8_t*) 0);

						break;
					}
					}

					break;
				}
				case POLL: {

					read_9((uint8_t*) 0);

					if (outsequence_todo) {
						outsequence_todo = false;

						mdb_payload[0] = 0x0b; // Command Out of Sequence
						available_tx = 1;

					} else if (cashless_reset_todo) {
						cashless_reset_todo = false;

						mdb_payload[0] = 0x00; // Just Reset
						available_tx = 1;

					} else if (vend_approved_todo) {
						vend_approved_todo = false;

						uint16_t vendAmount= to_scale_factor(0.00 /*vend request itemPrice*/, 1, 2);

						mdb_payload[0] = 0x05; 				// Vend Approved
						mdb_payload[1] = vendAmount >> 8;  	// Vend Amount
						mdb_payload[2] = vendAmount;
						available_tx = 3;

					} else if (vend_denied_todo) {
						vend_denied_todo = false;

						mdb_payload[0] = 0x06; // Vend Denied
						available_tx = 1;

						machine_state = IDLE_STATE;

					} else if (session_end_todo) {
						session_end_todo = false;

						mdb_payload[0] = 0x07; // End Session
						available_tx = 1;

						machine_state = ENABLED_STATE;

					} else if (session_begin_todo) {
						session_begin_todo = false;

						machine_state = IDLE_STATE;

						uint16_t fundsAvailable = to_scale_factor(0.00, 1, 2);

						mdb_payload[0] = 0x03; // Begin Session
						mdb_payload[1] = fundsAvailable >> 8;
						mdb_payload[2] = fundsAvailable;
						available_tx = 3;

					} else if (session_cancel_todo) {
						session_cancel_todo = false;

						mdb_payload[0] = 0x04; // Session Cancel Request
						available_tx = 1;
					}

					break;
				}
				case VEND: {

					switch(read_9(&checksum)){
					case VEND_REQUEST: {

						uint16_t itemPrice = (read_9(&checksum) /*2*/ << 8) | read_9(&checksum) /*3*/;
						uint16_t itemNumber = (read_9(&checksum) /*4*/ << 8) | read_9(&checksum) /*5*/;

						read_9((uint8_t*) 0);

						machine_state = VEND_STATE;

						break;
					}
					case VEND_CANCEL: {

						read_9((uint8_t*) 0);

						vend_denied_todo = true;

						break;
					}
					case VEND_SUCCESS: {

						uint16_t itemNumber = (read_9(&checksum) /*1*/ << 8) | read_9(&checksum) /*2*/;

						read_9((uint8_t*) 0);

						machine_state = IDLE_STATE;

						break;
					}
					case VEND_FAILURE: {

						read_9((uint8_t*) 0);

						machine_state = IDLE_STATE;

						break;
					}
					case SESSION_COMPLETE: {

						read_9((uint8_t*) 0);

						session_end_todo = true;

						break;
					}
					case CASH_SALE: {

						uint16_t itemPrice = (read_9(&checksum) /*2*/ << 8) | read_9(&checksum) /*3*/;
						uint16_t itemNumber = (read_9(&checksum) /*4*/ << 8) | read_9(&checksum) /*5*/;

						read_9((uint8_t*) 0); // read_9((uint8_t*) 0) == checksum

						//
						struct flow_cash_sale_msg_t msg= { .itemPrice= itemPrice, .itemNumber= itemNumber};
						xQueueSend(cash_sale_queue, &msg, 0 /*if full, do not wait*/ );

						break;
					}
					}

					break;
				}
				case READER: {
					switch(read_9(&checksum)){
					case READER_DISABLE: {

						read_9((uint8_t*) 0);

						machine_state = DISABLED_STATE;
						break;
					}
					case READER_ENABLE: {

						read_9((uint8_t*) 0);

						machine_state = ENABLED_STATE;
						break;
					}
					case READER_CANCEL: {

						read_9((uint8_t*) 0);

						mdb_payload[0] = 0x08; 	//Canceled
						available_tx = 1;
						break;
					}
					}
					break;
				}
				case EXPANSION: {
					switch(read_9(&checksum)){
					case REQUEST_ID: {

						char vmcManufacturerCode[3];
						vmcManufacturerCode[0]= read_9(&checksum);
						vmcManufacturerCode[1]= read_9(&checksum);
						vmcManufacturerCode[2]= read_9(&checksum);

						char vmcSerialNumber[12];
						vmcSerialNumber[0]= read_9(&checksum);
						vmcSerialNumber[1]= read_9(&checksum);
						vmcSerialNumber[2]= read_9(&checksum);
						vmcSerialNumber[3]= read_9(&checksum);
						vmcSerialNumber[4]= read_9(&checksum);
						vmcSerialNumber[5]= read_9(&checksum);
						vmcSerialNumber[6]= read_9(&checksum);
						vmcSerialNumber[7]= read_9(&checksum);
						vmcSerialNumber[8]= read_9(&checksum);
						vmcSerialNumber[9]= read_9(&checksum);
						vmcSerialNumber[10]= read_9(&checksum);
						vmcSerialNumber[11]= read_9(&checksum);

						char vmcModelNumber[12];
						vmcModelNumber[0]= read_9(&checksum);
						vmcModelNumber[1]= read_9(&checksum);
						vmcModelNumber[2]= read_9(&checksum);
						vmcModelNumber[3]= read_9(&checksum);
						vmcModelNumber[4]= read_9(&checksum);
						vmcModelNumber[5]= read_9(&checksum);
						vmcModelNumber[6]= read_9(&checksum);
						vmcModelNumber[7]= read_9(&checksum);
						vmcModelNumber[8]= read_9(&checksum);
						vmcModelNumber[9]= read_9(&checksum);
						vmcModelNumber[10]= read_9(&checksum);
						vmcModelNumber[11]= read_9(&checksum);

						char vmcSoftwareVersion[2];
						vmcSoftwareVersion[0]= read_9(&checksum);
						vmcSoftwareVersion[1]= read_9(&checksum);

						read_9((uint8_t*) 0);
//
						mdb_payload[0] = 0x09; // Peripheral ID

						mdb_payload[1] = ' '; // Peripheral Manufacturer code
						mdb_payload[2] = ' ';
						mdb_payload[3] = ' ';

						mdb_payload[4] = ' '; // Peripheral Serial number
						mdb_payload[5] = ' ';
						mdb_payload[6] = ' ';
						mdb_payload[7] = ' ';
						mdb_payload[8] = ' ';
						mdb_payload[9] = ' ';
						mdb_payload[10] = ' ';
						mdb_payload[11] = ' ';
						mdb_payload[12] = ' ';
						mdb_payload[13] = ' ';
						mdb_payload[14] = ' ';
						mdb_payload[15] = ' ';

						mdb_payload[16] = ' '; // Peripheral Model number
						mdb_payload[17] = ' ';
						mdb_payload[18] = ' ';
						mdb_payload[19] = ' ';
						mdb_payload[20] = ' ';
						mdb_payload[21] = ' ';
						mdb_payload[22] = ' ';
						mdb_payload[23] = ' ';
						mdb_payload[24] = ' ';
						mdb_payload[25] = ' ';
						mdb_payload[26] = ' ';
						mdb_payload[27] = ' ';

						mdb_payload[28] = ' '; // Peripheral Software version
						mdb_payload[29] = ' ';

						available_tx = 30;
						break;
					}
					}
					break;
				}
				}

				transmitPayloadByUART9((uint8_t*) &mdb_payload, available_tx);

			} else {
				// If not my address

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

void cash_sale_loop(void *pvParameters) {

	struct flow_cash_sale_msg_t msg;

	while(1){

		if (xQueueReceive(cash_sale_queue, &msg, portMAX_DELAY) ){

			char payload[100];
			sprintf(payload, "item_number=%d,item_price=%d", msg.itemNumber, msg.itemPrice);

			esp_mqtt_client_publish(mqttClient, "/application/sale/machine00000", (char*) &payload, 0, 1, 0);
		}
	}
}

void ping_callback(void *arg) {

	esp_mqtt_client_publish(mqttClient, "/application/ping/machine00000", "ping", 0, 1, 0);
}

void ble_event_handler(char *event_data){
	printf(">_ %s\n", event_data);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data){

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
        	if( strncmp(event->data, "restart", 7) == 0 ) {
        		esp_restart();
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

	gpio_set_direction(pin_mdb_led, GPIO_MODE_OUTPUT);
	gpio_set_level(pin_mdb_led, 1);

//	################################################################
	nvs_flash_init();

	esp_netif_init();

	esp_event_loop_create_default();
	esp_netif_create_default_wifi_sta();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	esp_wifi_init(&cfg);

	esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, (void*) 0, (void*) 0);

	wifi_config_t wifi_config = {
			.sta = {
					.ssid = "ALLREDE-SOARES-2G",
					.password = "julia2012",
					.threshold.authmode = WIFI_AUTH_WPA2_PSK,
			},
	};

	esp_wifi_set_mode(WIFI_MODE_STA);
	esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
	esp_wifi_start();

//	################################################################
	const esp_mqtt_client_config_t mqttCfg = {
			.broker.address.uri = "mqtt://mqtt.eclipseprojects.io",
	};

	mqttClient = esp_mqtt_client_init(&mqttCfg);

	esp_mqtt_client_register_event(mqttClient, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
	esp_mqtt_client_start(mqttClient);

//	################################################################
	const esp_timer_create_args_t timer_args = {
			.callback = &ping_callback,
			.arg = (void*) 0,
			.name = "ping_timer"
	};
	esp_timer_handle_t periodic_timer;
	esp_timer_create(&timer_args, &periodic_timer);

	esp_timer_start_periodic(periodic_timer, 300000000 /*useconds = 5min*/);

//	################################################################
	startBle("machine00000", ble_event_handler);

	xTaskCreate(mdb_loop, "mdb_loop", 1024, (void*) 0, 1, (void*) 0);

	cash_sale_queue= xQueueCreate(16 /*queue-length*/, sizeof(struct flow_cash_sale_msg_t));
	xTaskCreate(cash_sale_loop, "cash_sale_loop", 1024, (void*) 0, 1, (void*) 0);
}
