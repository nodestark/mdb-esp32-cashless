/*
 * VMflow.xyz
 *
 * mdb-slave-esp32s3.c - Vending machine peripheral
 *
 */

#include <esp_log.h>

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

#include "bleprph.h"
#include "nimble.h"
#include <time.h>

#define TAG "mdb-target"

#define pin_mdb_rx  	GPIO_NUM_4  // Pin to receive data from MDB
#define pin_mdb_tx  	GPIO_NUM_5  // Pin to transmit data to MDB
#define pin_mdb_led 	GPIO_NUM_21 // LED to indicate MDB state

// Functions for scale factor conversion
#define to_scale_factor(p, x, y) (p / x / pow(10, -(y) ))   // Converts to scale factor
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
	REQUEST_ID = 0x00, DIAGNOSTICS = 0xFF
};

// Defining machine states
typedef enum MACHINE_STATE {
	INACTIVE_STATE, DISABLED_STATE, ENABLED_STATE, IDLE_STATE, VEND_STATE
} machine_state_t;

machine_state_t machine_state = INACTIVE_STATE; // Initial machine state

// Control flags for MDB flows
bool session_begin_todo = false;
bool session_cancel_todo = false;
bool session_end_todo = false;
//
bool vend_approved_todo = false;
bool vend_denied_todo = false;
//
bool cashless_reset_todo = false;
bool outsequence_todo = false;

RingbufHandle_t dexRingbuf;

// MQTT client handle
esp_mqtt_client_handle_t mqttClient = (void*) 0;

// Message queues for communication
static QueueHandle_t mdbSessionQueue = (void*) 0;

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

    int32_t timestamp = ((uint32_t) payload[6] << 24) |
						((uint32_t) payload[7] << 16) |
						((uint32_t) payload[8] << 8)  |
						((uint32_t) payload[9] << 0);

    time_t now = time((void*) 0);

    if( abs((int32_t) now - timestamp) > 8 /*sec*/){
        return 0;
    }

    if(itemPrice)
        *itemPrice = ((uint16_t) payload[2] << 8) | ((uint16_t) payload[3] << 0);

    if(itemNumber)
        *itemNumber = ((uint16_t) payload[4] << 8) | ((uint16_t) payload[5] << 0);

    return 1;
}

// Encode payload to communication between BLE and MQTT
void xorEncodeWithPasskey(uint16_t *itemPrice, uint16_t *itemNumber, uint8_t *payload) {

	esp_fill_random(payload + 1, sizeof(my_passkey));

	time_t now = time((void*) 0);

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

void write_9(uint16_t nth9) {
    uint8_t ones = __builtin_popcount((uint8_t) nth9);

    uart_wait_tx_done(UART_NUM_2, pdMS_TO_TICKS(250));

    // Use the parity bit to send the mode bit 🤩
    if ((nth9 >> 8) & 1) {
        uart_set_parity(UART_NUM_2, ones % 2 ? UART_PARITY_EVEN : UART_PARITY_ODD);
    } else {
        uart_set_parity(UART_NUM_2, ones % 2 ? UART_PARITY_ODD : UART_PARITY_EVEN);
    }

    uart_write_bytes(UART_NUM_2, (uint8_t*) &nth9, 1);
}

// Function to transmit the payload via UART9 (using MDB protocol)
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

// Main MDB loop function
void mdb_cashless_loop(void *pvParameters) {

	time_t session_begin_time = 0;

	uint16_t fundsAvailable = 0;
	uint16_t itemPrice = 0;
	uint16_t itemNumber = 0;

	// Payload buffer and available transmission flag
	uint8_t mdb_payload_tx[36];
    uint8_t available_tx = 0;

	uint8_t mdb_payload_rx[36];
    uint8_t available_rx = 0;

	for (;;) {

        available_rx = uart_read_bytes(UART_NUM_2, mdb_payload_rx, 1, portMAX_DELAY);

        if(mdb_payload_rx[0] == ACK){
            continue;
        } else if(mdb_payload_rx[0] == NAK){
            continue;
        } else if(mdb_payload_rx[0] == RET){
            continue;
        }

        size_t len;
	    while( (len= uart_read_bytes(UART_NUM_2, mdb_payload_rx + available_rx, 1, pdMS_TO_TICKS(10))) > 0)
	        available_rx += len;

	    if ((mdb_payload_rx[0] & BIT_ADD_SET) == 0x10) {

            uint8_t chk = 0x00;
            for(int x= 0; x < (available_rx - 1); x++)
                chk += mdb_payload_rx[x];

            if(chk != mdb_payload_rx[available_rx - 1]){
                ESP_LOGI( TAG, "CHK invalid.");
                continue;
            }

            // Intended address
            gpio_set_level(pin_mdb_led, 1);

	        available_tx = 0;

            switch (mdb_payload_rx[0] & BIT_CMD_SET) {

            case RESET: {

                if (machine_state == VEND_STATE) {
                    // Reset during VEND_STATE is interpreted as VEND_SUCCESS
                }

                cashless_reset_todo = true;
                machine_state = INACTIVE_STATE;

                ESP_LOGI( TAG, "RESET");
                break;
            }
            case SETUP: {

                switch (mdb_payload_rx[1]) {

                case CONFIG_DATA: {

                    machine_state = DISABLED_STATE;

                    uint8_t vmcFeatureLevel = mdb_payload_rx[2];
                    uint8_t vmcColumnsOnDisplay = mdb_payload_rx[3];
                    uint8_t vmcRowsOnDisplay = mdb_payload_rx[4];
                    uint8_t vmcDisplayInfo = mdb_payload_rx[5];

                    mdb_payload_tx[0] = 0x01;        	// Reader Config Data
                    mdb_payload_tx[1] = 1;           	// Reader Feature Level
                    mdb_payload_tx[2] = 0xff;        	// Country Code High
                    mdb_payload_tx[3] = 0xff;        	// Country Code Low
                    mdb_payload_tx[4] = 1;           	// Scale Factor
                    mdb_payload_tx[5] = 2;           	// Decimal Places
                    mdb_payload_tx[6] = 3; 			    // Maximum Response Time (5s)
                    mdb_payload_tx[7] = 0b00001001;  	// Miscellaneous Options
                    available_tx = 8;

                    ESP_LOGI( TAG, "CONFIG_DATA");
                    break;
                }
                case MAX_MIN_PRICES: {

                    uint16_t maxPrice = (mdb_payload_rx[2] << 8) | mdb_payload_rx[3];
                    uint16_t minPrice = (mdb_payload_rx[4] << 8) | mdb_payload_rx[5];

                    ESP_LOGI( TAG, "MAX_MIN_PRICES");
                    break;
                }
                }

                break;
            }
            case POLL: {

                if (cashless_reset_todo) {
                    // Just reset
                    cashless_reset_todo = false;
                    mdb_payload_tx[0] = 0x00;
                    available_tx = 1;

                } else if (machine_state == ENABLED_STATE && xQueueReceive(mdbSessionQueue, &fundsAvailable, 0)) {
                    // Begin session
                    session_begin_todo = false;
                    machine_state = IDLE_STATE;

                    mdb_payload_tx[0] = 0x03;
                    mdb_payload_tx[1] = fundsAvailable >> 8;
                    mdb_payload_tx[2] = fundsAvailable;
                    available_tx = 3;

                    time( &session_begin_time);

                } else if (session_cancel_todo) {
                    // Cancel session
                    session_cancel_todo = false;

                    mdb_payload_tx[0] = 0x04;
                    available_tx = 1;

                } else if (vend_approved_todo) {
                    // Vend approved
                    vend_approved_todo = false;

                    mdb_payload_tx[0] = 0x05;
                    mdb_payload_tx[1] = itemPrice >> 8;
                    mdb_payload_tx[2] = itemPrice;
                    available_tx = 3;

                } else if (vend_denied_todo) {
                    // Vend denied
                    vend_denied_todo = false;
                    machine_state = IDLE_STATE;

                    mdb_payload_tx[0] = 0x06;
                    available_tx = 1;

                } else if (session_end_todo) {
                    // End session
                    session_end_todo = false;
                    machine_state = ENABLED_STATE;

                    mdb_payload_tx[0] = 0x07;
                    available_tx = 1;

                } else if (outsequence_todo) {
                    // Command out of sequence
                    outsequence_todo = false;

                    mdb_payload_tx[0] = 0x0b;
                    available_tx = 1;

                } else {

                    time_t now = time((void*) 0);

                    if (machine_state >= IDLE_STATE && (now - session_begin_time /*elapsed*/) > 60 /*60 sec*/) {
                        session_cancel_todo = true;
                    }
                }

                break;
            }
            case VEND: {

                switch (mdb_payload_rx[1]) {
                case VEND_REQUEST: {

                    machine_state = VEND_STATE;

                    uint16_t itemPrice = (mdb_payload_rx[2] << 8) | mdb_payload_rx[3];
                    uint16_t itemNumber = (mdb_payload_rx[4] << 8) | mdb_payload_rx[5];

                    if(fundsAvailable && (fundsAvailable != 0xffff)){

                        if (itemPrice <= fundsAvailable) {
                            vend_approved_todo = true;
                        } else {
                            vend_denied_todo = true;
                        }
                    }

                    /* PIPE_BLE */
                    uint8_t ble_payload_tx[19];
                    ble_payload_tx[0] = 0x0a;

                    xorEncodeWithPasskey(&itemPrice, &itemNumber, ble_payload_tx);

                    sendBleNotification((char*) ble_payload_tx, sizeof(ble_payload_tx));

                    ESP_LOGI( TAG, "VEND_REQUEST");
                    break;
                }
                case VEND_CANCEL: {
                    vend_denied_todo = true;

                    ESP_LOGI( TAG, "VEND_CANCEL");
                    break;
                }
                case VEND_SUCCESS: {

                    machine_state = IDLE_STATE;

                    itemNumber = (mdb_payload_rx[2] << 8) | mdb_payload_rx[3];

                    /* PIPE_BLE */
                    uint8_t ble_payload_tx[19];
                    ble_payload_tx[0] = 0x0b;

                    xorEncodeWithPasskey(&itemPrice, &itemNumber, ble_payload_tx);

                    sendBleNotification((char*) ble_payload_tx, sizeof(ble_payload_tx));

                    ESP_LOGI( TAG, "VEND_SUCCESS");
                    break;
                }
                case VEND_FAILURE: {
                    machine_state = IDLE_STATE;

                    /* PIPE_BLE */
                    uint8_t ble_payload_tx[19];
                    ble_payload_tx[0] = 0x0c;

                    xorEncodeWithPasskey(&itemPrice, &itemNumber, ble_payload_tx);

                    sendBleNotification((char*) ble_payload_tx, sizeof(ble_payload_tx));
                    break;
                }
                case SESSION_COMPLETE: {
                    session_end_todo = true;

                    /* PIPE_BLE */
                    uint8_t ble_payload_tx[19];
                    ble_payload_tx[0] = 0x0d;

                    xorEncodeWithPasskey(&itemPrice, &itemNumber, ble_payload_tx);

                    sendBleNotification((char*) ble_payload_tx, sizeof(ble_payload_tx));

                    ESP_LOGI( TAG, "SESSION_COMPLETE");
                    break;
                }
                case CASH_SALE: {

                    uint16_t itemPrice = (mdb_payload_rx[2] << 8) | mdb_payload_rx[3];
                    uint16_t itemNumber = (mdb_payload_rx[4] << 8) | mdb_payload_rx[5];

                    /* PIPE_MQTT */
                    uint8_t payload[19];
                    payload[0] = 0x21;

                    xorEncodeWithPasskey(&itemPrice, &itemNumber, payload);

                    char topic[64];
                    snprintf(topic, sizeof(topic), "/domain/%s/sale", my_subdomain);

                    esp_mqtt_client_publish(mqttClient, topic, (char*) payload, sizeof(payload), 1, 0);

                    ESP_LOGI( TAG, "CASH_SALE");
                    break;
                }
                }

                break;
            }
            case READER: {

                switch (mdb_payload_rx[1]) {
                case READER_DISABLE: {
                    machine_state = DISABLED_STATE;

                    ESP_LOGI( TAG, "READER_DISABLE");
                    break;
                }
                case READER_ENABLE: {
                    machine_state = ENABLED_STATE;

                    ESP_LOGI( TAG, "READER_ENABLE");
                    break;
                }
                case READER_CANCEL: {
                    mdb_payload_tx[ 0 ] = 0x08; // Canceled
                    available_tx = 1;

                    ESP_LOGI( TAG, "READER_CANCEL");
                    break;
                }
                }

                break;
            }
            case EXPANSION: {

                switch (mdb_payload_rx[1]) {
                case REQUEST_ID: {

                    struct mdb_id_t {
                        char manufacturerCode[3];
                        char serialNumber[12];
                        char modelNumber[12];
                        char softwareVersion[2];
                    } *mdb_id = (struct mdb_id_t*) &mdb_payload_rx[2];

                    mdb_payload_tx[ 0 ] = 0x09; 	                            // Peripheral ID

                    strncpy((char*) &mdb_payload_tx[1], "   ", 3);              // Manufacture code
                    strncpy((char*) &mdb_payload_tx[4], "            ", 12);    // Serial number
                    strncpy((char*) &mdb_payload_tx[16], "            ", 12);   // Model number
                    strncpy((char*) &mdb_payload_tx[28], "  ", 2);              // Software version

                    available_tx = 30;

                    ESP_LOGI( TAG, "REQUEST_ID");
                    break;
                }
                }

                break;
            }
            }

            // Transmit the prepared payload via UART
            write_payload_9(mdb_payload_tx, available_tx);

	    } else {

            gpio_set_level(pin_mdb_led, 0); // Not the intended address
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
	snprintf(topic, sizeof(topic), "/domain/%s/dex", my_subdomain);

    esp_mqtt_client_publish(mqttClient, topic, (char*) dex, dex_size, 0, 0);

    vRingbufferReturnItem(dexRingbuf, (void*) dex);
}

void ble_event_handler(char *ble_payload) {

    printf("ble_event_handler %x\n", (uint8_t) ble_payload[0]);

	switch ( (uint8_t) ble_payload[0] ) {
    case 0x00: {
        nvs_handle_t handle;
		ESP_ERROR_CHECK( nvs_open("vmflow", NVS_READWRITE, &handle) );

		size_t s_len;
		if (nvs_get_str(handle, "domain", NULL, &s_len) != ESP_OK) {

			strcpy((char*) &my_subdomain, ble_payload + 1);

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

			strcpy((char*) &my_passkey, ble_payload + 1);

			ESP_ERROR_CHECK( nvs_set_str(handle, "passkey", (char*) &my_passkey) );
			ESP_ERROR_CHECK( nvs_commit(handle) );

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

        if(xorDecodeWithPasskey((void*) 0, (void*) 0, (uint8_t*) ble_payload)){
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

	gpio_set_direction(pin_mdb_led, GPIO_MODE_OUTPUT);

    // Initialize UART2 driver and configure MDB pins
	uart_config_t uart_config_2 = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_EVEN,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE };

	uart_param_config(UART_NUM_2, &uart_config_2);
	uart_set_pin(UART_NUM_2, pin_mdb_tx, pin_mdb_rx, -1, -1);
	uart_driver_install(UART_NUM_2, 256, 256, 0, (void*) 0, 0);

	// Initialize UART1 driver and configure EVA DTS DEX/DDCMP pins
	uart_config_t uart_config_1 = {
			.baud_rate = 9600,
			.data_bits = UART_DATA_8_BITS,
			.parity = UART_PARITY_DISABLE,
			.stop_bits = UART_STOP_BITS_1,
			.flow_ctrl = UART_HW_FLOWCTRL_DISABLE };

	uart_param_config(UART_NUM_1, &uart_config_1);
	uart_set_pin( UART_NUM_1, GPIO_NUM_43, GPIO_NUM_44, -1, -1);
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
		.session.last_will.topic = lwt_topic, // LWT (Last Will and Testament)...
		.session.last_will.msg = "offline",
		.session.last_will.qos = 1,
		.session.last_will.retain = 1,
	};

	mqttClient = esp_mqtt_client_init(&mqttCfg);
	esp_mqtt_client_register_event(mqttClient, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

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

	// Creation of the queue for MDB sessions and the main MDB task
	mdbSessionQueue = xQueueCreate(1 /*queue-length*/, sizeof(uint16_t));
	xTaskCreate(mdb_cashless_loop, "cashless_loop", 4096, (void*) 0, 1, (void*) 0);
}