#include <stdio.h>
#include <string.h>

#include <driver/uart.h>
#include <driver/gpio.h>

#include "tinyusb.h"
#include "tusb_cdc_acm.h"
#include "tusb_console.h"

#include <rom/ets_sys.h>

#include "apps/your_app/your_app.h"

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
	/*MDB...*/
	ENABLED_STATE,
	INACTIVE_STATE,
	DISABLED_STATE,
	IDLE_STATE,
	VEND_STATE
	/*...MDB*/
} machine_state_t;

machine_state_t machine_state;

uint8_t reset_cashless_todo= false;

uint8_t session_begin_todo= false;
uint8_t session_end_todo= false;
uint8_t session_cancel_todo= false;

uint8_t vend_request_todo= false;
uint8_t vend_approved_todo= false;
uint8_t vend_denied_todo= false;
uint8_t vend_fail_todo= false;

uint8_t outsequence_todo= false;

// ---
uint8_t mMdb_payload[256];
uint8_t available_rx= 0;
uint8_t available_tx= 0;

uint16_t read_9() {

	uint16_t coming_read = 0;

	while (gpio_get_level(GPIO_NUM_4))
		;

	ets_delay_us(156);
	for(uint8_t x= 0; x < 9; x++){

		coming_read |= (gpio_get_level(GPIO_NUM_4) << x);
		ets_delay_us(104); // 9600bps
	}

	return coming_read;
}

void write_9( uint16_t nth9 ) {

	gpio_set_level(GPIO_NUM_5, 0); // start
	ets_delay_us(104);

	for(uint8_t x= 0; x < 9; x++){

		gpio_set_level(GPIO_NUM_5, (nth9 >> x) & 1);
		ets_delay_us(104); // 9600bps
	}

	gpio_set_level(GPIO_NUM_5, 1); // stop
	ets_delay_us(104);
}

void transmitPayloadByUART9() {

	uint8_t chk = 0;
	for (int x = 0; x < available_tx; x++) {

		chk += mMdb_payload[x];
		write_9(mMdb_payload[x]);
	}

	// CHK* ACK*
	write_9(0b100000000 | chk);
}

void mdb_loop(void *pvParameters) {

	uint8_t mIsPayloading= false;
	uint8_t checksum= 0x00;

	for (;;) {

		uint16_t coming_read = read_9();

		if (coming_read & BIT_MODE_SET) {

			// ADD*
			mIsPayloading = true;
			checksum = 0;

			available_rx = 0;
			available_tx = 0;
		}

		if (mIsPayloading) {

			mMdb_payload[available_rx++] = coming_read;

			if (checksum == (uint8_t) coming_read ) {
				// CHK

				mIsPayloading = false;

				if ((mMdb_payload[0] & BIT_ADD_SET) == 0x10) {

					// status led...
					gpio_set_level(GPIO_NUM_21, 1);

					uint8_t mdb_cmd = mMdb_payload[0] & BIT_CMD_SET;

					if (mdb_cmd == RESET) {

						if (machine_state == VEND_STATE) {
							// A reset between VEND APPROVED and VEND SUCCESS shall be interbreted as a VEND SUCCESS...
						}

						machine_state = INACTIVE_STATE;
						reset_cashless_todo = true;

					} else if (mdb_cmd == SETUP) {

						if (mMdb_payload[1] == CONFIG_DATA) {

							machine_state = DISABLED_STATE;

							mMdb_payload[0] = 0x01;       	// Reader Config Data
							mMdb_payload[1] = 0x01; 		// Reader Feature Level. 1, 2, 3
							mMdb_payload[2] = 0xff;       	// Country Code High;
							mMdb_payload[3] = 0xff;       	// Country Code Low;
							mMdb_payload[4] = 0x01;       	// Scale Factor
							mMdb_payload[5] = 0x02;       	// Decimal Places
							mMdb_payload[6] = 120; 			// Application Maximum Response Time (90s)
							mMdb_payload[7] = 0b00001001; 	// Miscellaneous Options

							available_tx = 8;

						} else if (mMdb_payload[1] == MAX_MIN_PRICES) {

							 uint16_t minPrice = (mMdb_payload[4] << 8) | mMdb_payload[5];
							 uint16_t maxPrice = (mMdb_payload[2] << 8) | mMdb_payload[3];

							// No Data *
						}
					} else if (mdb_cmd == POLL) {

						if (outsequence_todo) {
							outsequence_todo = false;

							mMdb_payload[0] = 0x0b; // Command Out of Sequence
							available_tx = 1;

						} else if (reset_cashless_todo) {
							reset_cashless_todo = false;

							mMdb_payload[0] = 0x00; // Just Reset
							available_tx = 1;

						} else if (vend_approved_todo) {
							vend_approved_todo = false;

							mMdb_payload[0] = 0x05;             // Vend Approved
//							mMdb_payload[1] = mMdbCredit >> 8;  // Vend Amount
//							mMdb_payload[2] = mMdbCredit;
							available_tx = 3;

						} else if (vend_denied_todo) {
							vend_denied_todo = false;

							mMdb_payload[0] = 0x06; // Vend Denied
							available_tx = 1;

							machine_state = IDLE_STATE;

						} else if (session_end_todo) {
							session_end_todo = false;

							mMdb_payload[0] = 0x07; // End Session
							available_tx = 1;

							machine_state = ENABLED_STATE;

						} else if (session_begin_todo) {
							session_begin_todo = false;

							machine_state = IDLE_STATE;

							uint8_t fundsAvailable = 0x00;

							//Set credits
							mMdb_payload[0] = 0x03; // Begin Session
							mMdb_payload[1] = fundsAvailable >> 8;
							mMdb_payload[2] = fundsAvailable;

							available_tx = 3;

						} else if (session_cancel_todo) {
							session_cancel_todo = false;

							mMdb_payload[0] = 0x04; // Session Cancel Request

							available_tx = 1;
						}

					} else if (mdb_cmd == VEND) {

						if (mMdb_payload[1] == VEND_REQUEST) {

							machine_state = VEND_STATE;

							uint16_t itemPrice = (mMdb_payload[2] << 8) | mMdb_payload[3];
							uint16_t itemNumber = (mMdb_payload[4] << 8) | mMdb_payload[5];

						} else if (mMdb_payload[1] == VEND_CANCEL) {

							vend_denied_todo = true;

						} else if (mMdb_payload[1] == VEND_SUCCESS) {

							machine_state = IDLE_STATE;

							//No Data *
						} else if (mMdb_payload[1] == VEND_FAILURE) {

							machine_state = IDLE_STATE;

							// No Data *
						} else if (mMdb_payload[1] == SESSION_COMPLETE) {

							session_end_todo = true;

						} else if (mMdb_payload[1] == CASH_SALE) {

							uint16_t itemPrice = (mMdb_payload[2] << 8) | mMdb_payload[3];
							uint16_t itemNumber = (mMdb_payload[4] << 8) | mMdb_payload[5];

							// No Data *
						}

					} else if (mdb_cmd == READER) {

						if (mMdb_payload[1] == READER_DISABLE) {

							machine_state = DISABLED_STATE;

						} else if (mMdb_payload[1] == READER_ENABLE) {

							machine_state = ENABLED_STATE;

						} else if (mMdb_payload[1] == READER_CANCEL) {

							mMdb_payload[0] = 0x08; //Canceled
							available_tx = 1;
						}
					} else if (mdb_cmd == EXPANSION) {

						if (mMdb_payload[1] == REQUEST_ID) {

							mMdb_payload[0] = 0x09; // Peripheral ID

							mMdb_payload[1] = ' '; // Manufacture code
							mMdb_payload[2] = ' ';
							mMdb_payload[3] = ' ';

							mMdb_payload[4] = ' '; // Serial number
							mMdb_payload[5] = ' ';
							mMdb_payload[6] = ' ';
							mMdb_payload[7] = ' ';
							mMdb_payload[8] = ' ';
							mMdb_payload[9] = ' ';
							mMdb_payload[10] = ' ';
							mMdb_payload[11] = ' ';
							mMdb_payload[12] = ' ';
							mMdb_payload[13] = ' ';
							mMdb_payload[14] = ' ';
							mMdb_payload[15] = ' ';

							mMdb_payload[16] = ' '; // Model number
							mMdb_payload[17] = ' ';
							mMdb_payload[18] = ' ';
							mMdb_payload[19] = ' ';
							mMdb_payload[20] = ' ';
							mMdb_payload[21] = ' ';
							mMdb_payload[22] = ' ';
							mMdb_payload[23] = ' ';
							mMdb_payload[24] = ' ';
							mMdb_payload[25] = ' ';
							mMdb_payload[26] = ' ';
							mMdb_payload[27] = ' ';

							mMdb_payload[28] = ' '; // Software version
							mMdb_payload[29] = ' ';

							available_tx = 30;
						}
					}

					transmitPayloadByUART9();

				} else {

					// If not my address

					// status led...
					gpio_set_level(GPIO_NUM_21, 0);
				}

			} else {

				checksum += coming_read;
			}
		} else if (coming_read == ACK) {

			// ACK
		} else if (coming_read == RET) {

			// RET
		} else if (coming_read == NAK) {
			// NAK
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

void app_main(void) {

	const tinyusb_config_t tusb_cfg = {
			.device_descriptor = (void*) 0,
			.string_descriptor = (void*) 0,
			.external_phy = false,
			.configuration_descriptor = (void*) 0,
	};
	ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

	tinyusb_config_cdcacm_t acm_cfg = { 0 };
	ESP_ERROR_CHECK(tusb_cdc_acm_init(&acm_cfg));

    esp_tusb_init_console(TINYUSB_CDC_ACM_0);

    // ################################################################
	gpio_set_direction(GPIO_NUM_4, GPIO_MODE_INPUT);
	gpio_set_direction(GPIO_NUM_5, GPIO_MODE_OUTPUT);

	// led...
	gpio_set_direction(GPIO_NUM_21, GPIO_MODE_OUTPUT);

    // ################################################################
	ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, 256, 256, 0, (void*) 0, 0));
	ESP_ERROR_CHECK (uart_set_pin( UART_NUM_1, GPIO_NUM_17, GPIO_NUM_18, -1, -1));

	uart_config_t uart_config_1 = {
			.baud_rate = 115200,
			.data_bits = UART_DATA_8_BITS,
			.parity = UART_PARITY_DISABLE,
			.stop_bits = UART_STOP_BITS_1,
			.flow_ctrl = UART_HW_FLOWCTRL_DISABLE };

	ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &uart_config_1));

	xTaskCreate(mdb_loop, "mdb_loop", 10946, (void*) 0, 1, (void*) 0);
}
