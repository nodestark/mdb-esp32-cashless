/*
 * mdb-master-esp32s3.c - Vending machine controller
 *
 */

#include "esp_log.h"

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <driver/gpio.h>
#include <rom/ets_sys.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "mdb-controller"

#define pin_mdb_rx  	GPIO_NUM_4  // Pin to receive data from MDB
#define pin_mdb_tx  	GPIO_NUM_5  // Pin to transmit data to MDB
#define pin_mdb_led 	GPIO_NUM_21 // LED to indicate MDB state

#define to_scale_factor(p, x, y) (p / x / pow(10, -(y) ))
#define from_scale_factor(p, x, y) (p * x * pow(10, -(y) ))

#define ACK 	0x00  // Acknowledgment / Checksum correct;
#define RET 	0xAA  // Retransmit the previously sent data. Only the VMC can transmit this byte;
#define NAK 	0xFF  // Negative acknowledge.

#define BIT_MODE_SET 	0b100000000
#define BIT_ADD_SET   	0b011111000
#define BIT_CMD_SET   	0b000000111

enum MDB_COMMAND {
	RESET = 0x00,
	SETUP = 0x01,
	POLL = 0x02,
	VEND = 0x03,
	READER = 0x04,
	EXPANSION = 0x07
};

typedef enum MACHINE_STATE {
	INACTIVE_STATE, DISABLED_STATE, ENABLED_STATE, IDLE_STATE, VEND_STATE
} machine_state_t;

machine_state_t machine_state = INACTIVE_STATE;

struct reader_t {
	uint8_t scaleFactor;
	uint8_t decimalPlaces;
	uint8_t responseTimeSec;
	uint8_t miscellaneous;
};
struct reader_t reader0x10 = { .responseTimeSec = 1 /*1sec*/};

/* PA102 - Product Price
 * PA201 - Number of Products Vended Since Initialisation
 * */
uint8_t coils[1][2] = {{100 /*PA102*/, 0 /*PA201*/}};

void IRAM_ATTR write_9(uint16_t nth9) {

	nth9 <<= 1;
	nth9 |= 0b10000000000; // Start bit | nth9 | Stop bit

	for (uint8_t x = 0; x < 11; x++) {

		gpio_set_level(pin_mdb_tx, (nth9 >> x) & 1);
		ets_delay_us(104); // 9600bps
	}
}

uint16_t IRAM_ATTR read_9() {

	uint16_t coming_read = 0;

	// Wait start bit
	while (gpio_get_level(pin_mdb_rx))
		;

	ets_delay_us(104);
	ets_delay_us(52);

	for(int x = 0; x < 9; x++){
		coming_read |= (gpio_get_level(pin_mdb_rx) << x);
		ets_delay_us(104);
	}

	return coming_read;
}

static QueueHandle_t button_receive_queue = (void*) 0;

static void IRAM_ATTR button0_isr_handler(void* arg) {

	uint8_t itemNumber = 0;
	xQueueSendFromISR(button_receive_queue, &itemNumber, 0);
}

static QueueHandle_t payload_receive_queue = NULL;

void mdb_payload_loop(void *pvParameters) {

	uint8_t mdb_payload_tx[256];

	uint8_t idx = 0;
	for (;;) {

		uint16_t coming_read = read_9();

		mdb_payload_tx[idx++] = coming_read;

		if (coming_read & BIT_MODE_SET) {

			xQueueSend(payload_receive_queue, &mdb_payload_tx, 0);
			idx = 0;
		}
	}
}

void write_payload_9(uint8_t *mdb_payload_tx, uint8_t mdb_length) {

	uint8_t checksum = 0;

	write_9((checksum = mdb_payload_tx[0]) | BIT_MODE_SET);
	for (uint8_t x = 1; x < mdb_length; x++) {
		write_9(mdb_payload_tx[x]);

		checksum += mdb_payload_tx[x];
	}

	write_9(checksum);
}

void mdb_vmc_loop(void *pvParameters) {

	uint8_t itemNumber = -1;

	uint8_t mdb_payload_tx[256];
	uint8_t mdb_payload_rx[256];

	for (;;) {

		if (machine_state == INACTIVE_STATE) {

			mdb_payload_tx[0] = (0x10 /*Cashless Device #1*/ & BIT_ADD_SET) | (RESET & BIT_CMD_SET);
			write_payload_9((uint8_t*) &mdb_payload_tx, 1);

			vTaskDelay(pdMS_TO_TICKS(250));
			xQueueReceive(payload_receive_queue, &mdb_payload_rx, pdMS_TO_TICKS(250)); // ACK*

			mdb_payload_tx[0] = (0x10 /*Cashless Device #1*/ & BIT_ADD_SET) | (POLL & BIT_CMD_SET);
			write_payload_9((uint8_t*) &mdb_payload_tx, 1);

			vTaskDelay(pdMS_TO_TICKS(250));
			if (xQueueReceive(payload_receive_queue, &mdb_payload_rx, pdMS_TO_TICKS(250))) {

				if (mdb_payload_rx[0] == 0x00 /*Just Reset*/) {

					write_9(ACK | BIT_MODE_SET);

					mdb_payload_tx[0] = (0x10 /*Cashless Device #1*/ & BIT_ADD_SET) | (SETUP & BIT_CMD_SET);
					mdb_payload_tx[1] = 0x00; 			// Config Data
					mdb_payload_tx[2] = 1; 			// VMC Feature Level
					mdb_payload_tx[3] = 0; 			// Columns on Display
					mdb_payload_tx[4] = 0; 			// Rows on Display
					mdb_payload_tx[5] = 0b00000001; 	// Display Information

					write_payload_9((uint8_t*) &mdb_payload_tx, 6);

					vTaskDelay(pdMS_TO_TICKS(250));
					if (xQueueReceive(payload_receive_queue, &mdb_payload_rx, pdMS_TO_TICKS(250))) {
						// Reader Config

						reader0x10.scaleFactor = mdb_payload_rx[4];
						reader0x10.decimalPlaces = mdb_payload_rx[5];
						reader0x10.responseTimeSec = mdb_payload_rx[6];
						reader0x10.miscellaneous = mdb_payload_rx[7];

						ESP_LOGI( TAG, "Reader Config, %x, %x, %x, %x", reader0x10.scaleFactor, reader0x10.decimalPlaces, reader0x10.responseTimeSec, reader0x10.miscellaneous);

						machine_state = DISABLED_STATE;
					}
				}
			}

		} else if (machine_state == DISABLED_STATE) {

			ESP_LOGI( TAG, "step 1");

			uint16_t maxPrice = 0, minPrice = 0xffff;

			for (uint8_t c = 0; c < sizeof(coils)/sizeof(coils[0]); c++) {
				if(coils[c][0] > maxPrice) maxPrice = coils[c][0];

				if(coils[c][0] < minPrice) minPrice = coils[c][0];
			}

			maxPrice = to_scale_factor(maxPrice / 100.0f, reader0x10.scaleFactor, reader0x10.decimalPlaces);
			minPrice = to_scale_factor(minPrice / 100.0f, reader0x10.scaleFactor, reader0x10.decimalPlaces);

			mdb_payload_tx[0] = (0x10 /*Cashless Device #1*/ & BIT_ADD_SET) | (SETUP & BIT_CMD_SET);
			mdb_payload_tx[1] = 0x01; // Max/Min Prices
			mdb_payload_tx[2] = maxPrice >> 8;
			mdb_payload_tx[3] = maxPrice;
			mdb_payload_tx[4] = minPrice >> 8;
			mdb_payload_tx[5] = minPrice;

			write_payload_9((uint8_t*) &mdb_payload_tx, 6);

			vTaskDelay(pdMS_TO_TICKS(250));
			xQueueReceive(payload_receive_queue, &mdb_payload_rx, pdMS_TO_TICKS(250)); // ACK*

			mdb_payload_tx[0] = (0x10 /*Cashless Device #1*/ & BIT_ADD_SET) | (EXPANSION & BIT_CMD_SET);
			mdb_payload_tx[1] = 0x00; // Request ID

			mdb_payload_tx[2] = ' '; // Manufacturer code
			mdb_payload_tx[3] = ' ';
			mdb_payload_tx[4] = ' ';

			mdb_payload_tx[5] = ' '; // Serial Number
			mdb_payload_tx[6] = ' ';
			mdb_payload_tx[7] = ' ';
			mdb_payload_tx[8] = ' ';
			mdb_payload_tx[9] = ' ';
			mdb_payload_tx[10] = ' ';
			mdb_payload_tx[11] = ' ';
			mdb_payload_tx[12] = ' ';
			mdb_payload_tx[13] = ' ';
			mdb_payload_tx[14] = ' ';
			mdb_payload_tx[15] = ' ';
			mdb_payload_tx[16] = ' ';

			mdb_payload_tx[17] = ' '; // Model Number
			mdb_payload_tx[18] = ' ';
			mdb_payload_tx[19] = ' ';
			mdb_payload_tx[20] = ' ';
			mdb_payload_tx[21] = ' ';
			mdb_payload_tx[22] = ' ';
			mdb_payload_tx[23] = ' ';
			mdb_payload_tx[24] = ' ';
			mdb_payload_tx[25] = ' ';
			mdb_payload_tx[26] = ' ';
			mdb_payload_tx[27] = ' ';
			mdb_payload_tx[28] = ' ';

			mdb_payload_tx[29] = ' '; // Software Version
			mdb_payload_tx[30] = ' ';

			write_payload_9((uint8_t*) &mdb_payload_tx, 31);

			vTaskDelay(pdMS_TO_TICKS(250));
			if (xQueueReceive(payload_receive_queue, &mdb_payload_rx, pdMS_TO_TICKS(250))) {
				// Peripheral ID

				ESP_LOGI( TAG, "Reader Enable...");

				write_9(ACK | BIT_MODE_SET);

				mdb_payload_tx[0] = (0x10 /*Cashless Device #1*/ & BIT_ADD_SET) | (READER & BIT_CMD_SET);
				mdb_payload_tx[1] = 0x01; // Reader Enable

				write_payload_9((uint8_t*) &mdb_payload_tx, 2);

				vTaskDelay(pdMS_TO_TICKS(250));
				xQueueReceive(payload_receive_queue, &mdb_payload_rx, pdMS_TO_TICKS(250)); // ACK*

				machine_state = ENABLED_STATE;
			}

		} else {

			mdb_payload_tx[0] = (0x10 /*Cashless Device #1*/ & BIT_ADD_SET) | (POLL & BIT_CMD_SET);

			write_payload_9((uint8_t*) &mdb_payload_tx, 1);

			vTaskDelay(pdMS_TO_TICKS(250));
			xQueueReceive(payload_receive_queue, &mdb_payload_rx, pdMS_TO_TICKS(250));

			if (mdb_payload_rx[0] == 0x07 /*End Session*/) {
				machine_state = ENABLED_STATE;

			} else if (mdb_payload_rx[0] == 0x06 /*Vend Denied*/) {

				mdb_payload_tx[0] = (0x10 /*Cashless Device #1*/ & BIT_ADD_SET) | (VEND & BIT_CMD_SET);
				mdb_payload_tx[1] = 0x04; // Session Complete

				write_payload_9((uint8_t*) &mdb_payload_tx, 2);

				vTaskDelay(pdMS_TO_TICKS(250));
				xQueueReceive(payload_receive_queue, &mdb_payload_rx, pdMS_TO_TICKS(250)); // ACK*

				machine_state = IDLE_STATE;

			} else if (mdb_payload_rx[0] == 0x05 /*Vend Approved*/) {

				ESP_LOGI( TAG, "Vend Approved...");

				uint16_t vendAmount = (mdb_payload_rx[1] << 8) | mdb_payload_rx[2];

				mdb_payload_tx[0] = (0x10 /*Cashless Device #1*/ & BIT_ADD_SET) | (VEND & BIT_CMD_SET);
				mdb_payload_tx[1] = 0x02; // Vend Success
				mdb_payload_tx[2] = itemNumber >> 8;
				mdb_payload_tx[3] = itemNumber;

				write_payload_9((uint8_t*) &mdb_payload_tx, 4);

				vTaskDelay(pdMS_TO_TICKS(250));
				xQueueReceive(payload_receive_queue, &mdb_payload_rx, pdMS_TO_TICKS(250)); // ACK*

				machine_state = IDLE_STATE;

				++coils[itemNumber][1];

				mdb_payload_tx[0] = (0x10 /*Cashless Device #1*/ & BIT_ADD_SET) | (VEND & BIT_CMD_SET);
				mdb_payload_tx[1] = 0x04; // Session Complete

				write_payload_9((uint8_t*) &mdb_payload_tx, 2);

				vTaskDelay(pdMS_TO_TICKS(250));
				xQueueReceive(payload_receive_queue, &mdb_payload_rx, pdMS_TO_TICKS(250)); // ACK*

			} else if (mdb_payload_rx[0] == 0x04 /*Session Cancel Request*/) {
				// IDLE_STATE

				mdb_payload_tx[0] = (0x10 /*Cashless Device #1*/ & BIT_ADD_SET) | (VEND & BIT_CMD_SET);
				mdb_payload_tx[1] = 0x04; // Session Complete

				write_payload_9((uint8_t*) &mdb_payload_tx, 2);

				vTaskDelay(pdMS_TO_TICKS(250));
				xQueueReceive(payload_receive_queue, &mdb_payload_rx, pdMS_TO_TICKS(250)); // ACK*

			} else if (mdb_payload_rx[0] == 0x03 /*Begin Session*/) {
				// ENABLED_STATE

				ESP_LOGI( TAG, "Begin Session...");

				machine_state = IDLE_STATE;

				uint16_t fundsAvailable = (mdb_payload_rx[1] << 8) | mdb_payload_rx[2];

				write_9(ACK | BIT_MODE_SET);

			} else if (mdb_payload_rx[0] == 0x0b /*Command Out of Sequence*/) {

				ESP_LOGI( TAG, "Command Out of Sequence...");

				machine_state = INACTIVE_STATE;

			} else if (xQueueReceive(button_receive_queue, &itemNumber, 0)) {

				uint16_t itemPrice = to_scale_factor(coils[itemNumber][0] / 100.0f, reader0x10.scaleFactor, reader0x10.decimalPlaces);

				/*Vend Cancel (Vend Request)*/

				if (machine_state == IDLE_STATE) {

					mdb_payload_tx[0] = (0x10 /*Cashless Device #1*/ & BIT_ADD_SET) | (VEND & BIT_CMD_SET);
					mdb_payload_tx[1] = 0x00; // Vend Request
					mdb_payload_tx[2] = itemPrice >> 8;
					mdb_payload_tx[3] = itemPrice;

					mdb_payload_tx[4] = itemNumber >> 8;
					mdb_payload_tx[5] = itemNumber;

					write_payload_9((uint8_t*) &mdb_payload_tx, 6);

					vTaskDelay(pdMS_TO_TICKS(250));
					xQueueReceive(payload_receive_queue, &mdb_payload_rx, pdMS_TO_TICKS(250)); // ACK*

					machine_state = VEND_STATE;

				} else if (machine_state == ENABLED_STATE) {

					mdb_payload_tx[0] = (0x10 /*Cashless Device #1*/ & BIT_ADD_SET) | (VEND & BIT_CMD_SET);
					mdb_payload_tx[1] = 0x05; // Cash Sale
					mdb_payload_tx[2] = itemPrice >> 8;
					mdb_payload_tx[3] = itemPrice;

					mdb_payload_tx[4] = itemNumber >> 8;
					mdb_payload_tx[5] = itemNumber;

					write_payload_9((uint8_t*) &mdb_payload_tx, 6);

					vTaskDelay(pdMS_TO_TICKS(250));
					xQueueReceive(payload_receive_queue, &mdb_payload_rx, pdMS_TO_TICKS(250)); // ACK*

					++coils[itemNumber][1];
				}
			}
		}

		{
			mdb_payload_tx[0] = 0x60 /*Cashless Device #2*/ | RESET;
			write_payload_9((uint8_t*) &mdb_payload_tx, 1);

			vTaskDelay(pdMS_TO_TICKS(250));
			xQueueReceive(payload_receive_queue, &mdb_payload_rx, 500 / portTICK_PERIOD_MS); // ACK
		}

		gpio_set_level(pin_mdb_led, machine_state > ENABLED_STATE);
	}
}

void eva_dts_loop(void *pvParameters) {

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

	//
	uint8_t data[32];

	while (1) {

		// ENQ <-
		uart_read_bytes(UART_NUM_1, &data, 1, portMAX_DELAY);
		if (data[0] != 0x05)
			continue;

		// -------------------------------------- First Handshake --------------------------------------

		// DLE 0 ->
		uart_write_bytes(UART_NUM_1, "\x10\x30", 2);

		// DLE SOH <-
		uart_read_bytes(UART_NUM_1, &data, 2, pdMS_TO_TICKS(100));
		if (data[0] != 0x10 || data[1] != 0x01)
			continue;

		// Communication ID <-
		uart_read_bytes(UART_NUM_1, &data, 10, pdMS_TO_TICKS(100));

		// Operation Request <-
		uart_read_bytes(UART_NUM_1, &data, 1, pdMS_TO_TICKS(100));

		// Revision & Level <-
		uart_read_bytes(UART_NUM_1, &data, 6, pdMS_TO_TICKS(100));

		// DLE ETX <-
		uart_read_bytes(UART_NUM_1, &data, 2, pdMS_TO_TICKS(100));
		if (data[0] != 0x10 || data[1] != 0x03)
			continue;

		// CRC-16 <-
		uart_read_bytes(UART_NUM_1, &data, 2, pdMS_TO_TICKS(100));

		// DLE 1 ->
		uart_write_bytes(UART_NUM_1, "\x10\x31", 2);

		// EOT <-
		uart_read_bytes(UART_NUM_1, &data, 1, pdMS_TO_TICKS(100));
		if (data[0] != 0x04)
			continue;

		// -------------------------------------- Second Handshake --------------------------------------

		// ENQ ->
		uart_write_bytes(UART_NUM_1, "\x05", 1);

		// DLE 0 <-
		uart_read_bytes(UART_NUM_1, &data, 2, pdMS_TO_TICKS(100));
		if (data[0] != 0x10 || data[1] != '0')
			continue;

		// DLE SOH ->
		uart_write_bytes(UART_NUM_1, "\x10\x01", 2);

		// Response Code ->
		uart_write_bytes(UART_NUM_1, "00", 2);
		// Communication ID ->
		uart_write_bytes(UART_NUM_1, "1234567890", 10);
		// Revision & Level ->
		uart_write_bytes(UART_NUM_1, "R00L06", 6);

		// DLE ETX ->
		uart_write_bytes(UART_NUM_1, "\x10\x03", 2);
		// CRC-16 ->
		uart_write_bytes(UART_NUM_1, "\xFF\xFF", 2);

		// DLE 1 <-
		uart_read_bytes(UART_NUM_1, &data, 2, pdMS_TO_TICKS(100));
		if (data[0] != 0x10 || data[1] != '1')
			continue;

		// EOT ->
		uart_write_bytes(UART_NUM_1, "\x04", 1);

		// -------------------------------------- Data transfer --------------------------------------

		// ENQ ->
		uart_write_bytes(UART_NUM_1, "\x05", 1);

		uint8_t block = 0x00;
		for (uint8_t c = 0; c < sizeof(coils)/sizeof(coils[0]); c++) {

			// DLE 0|1 <-
			uart_read_bytes(UART_NUM_1, &data, 2, pdMS_TO_TICKS(200));
			if (data[0] != 0x10 || data[1] != ('0' + (block++ & 0x01)) )
				break;

			// DLE STX ->
			uart_write_bytes(UART_NUM_1, "\x10\x02", 2);

			char pa1[100];
			sprintf(pa1, "PA1*%d*%d****\x0D\x0A", c, coils[c][0]);

			uart_write_bytes(UART_NUM_1, &pa1, strlen(pa1));

			char pa2[100];
			sprintf(pa2, "PA2*%d*0*0*0\x0D\x0A", coils[c][1]);

			uart_write_bytes(UART_NUM_1, &pa2, strlen(pa2));

			// DLE ETB ->
			uart_write_bytes(UART_NUM_1, "\x10\x17", 2);
			// CRC-16
			uart_write_bytes(UART_NUM_1, "\xff\xff", 2);
		}

		// DLE 0|1 <-
		uart_read_bytes(UART_NUM_1, &data, 2, pdMS_TO_TICKS(200));
		if (data[0] != 0x10 || data[1] != ('0' + (block++ & 0x01)) )
			break;

		// DLE STX ->
		uart_write_bytes(UART_NUM_1, "\x10\x02", 2);

		char *ea3 = "EA3**000101*000080**000101*000080***490*490\x0D\x0A";
		uart_write_bytes(UART_NUM_1, ea3, strlen(ea3));

		char *g85 = "G85*1BB8\x0D\x0A";
		uart_write_bytes(UART_NUM_1, g85, strlen(g85));

		char *se = "SE*280*0001\x0D\x0A";
		uart_write_bytes(UART_NUM_1, se, strlen(se));

		char *dxe = "DXE*1*1\x0D\x0A";
		uart_write_bytes(UART_NUM_1, dxe, strlen(dxe));

		// DLE ETX ->
		uart_write_bytes(UART_NUM_1, "\x10\x03", 2);
		// CRC-16
		uart_write_bytes(UART_NUM_1, "\xff\xff", 2);

		// DLE 0|1 <-
		uart_read_bytes(UART_NUM_1, &data, 2, pdMS_TO_TICKS(200));
		if (data[0] != 0x10 || data[1] != ('0' + (block++ & 0x01)) )
			break;

		// EOT ->
		uart_write_bytes(UART_NUM_1, "\x04", 1);
	}
}

void app_main(void) {

	gpio_set_direction(pin_mdb_rx, GPIO_MODE_INPUT);
	gpio_set_direction(pin_mdb_tx, GPIO_MODE_OUTPUT);

	gpio_set_direction(pin_mdb_led, GPIO_MODE_OUTPUT);

	//
	button_receive_queue = xQueueCreate(1 /*queue-length*/, sizeof(uint8_t));

	gpio_config_t io_conf = {
			.intr_type = GPIO_INTR_NEGEDGE, // Borda de descida (pressionado)
			.mode = GPIO_MODE_INPUT,
			.pin_bit_mask = (1ULL << GPIO_NUM_0),
			.pull_up_en = GPIO_PULLUP_ENABLE,
			.pull_down_en = GPIO_PULLDOWN_DISABLE, };

	gpio_config(&io_conf);

	gpio_install_isr_service(0 /*default*/);
	gpio_isr_handler_add(GPIO_NUM_0, button0_isr_handler, (void*) 0);

	//
	payload_receive_queue = xQueueCreate(1 /*queue-length*/, 256 * sizeof(uint8_t));
	xTaskCreate(mdb_payload_loop, "mdb_loop", 6765, (void*) 0, 1, (void*) 0);

	xTaskCreate(mdb_vmc_loop, "vmc_loop", 6765, (void*) 0, 1, (void*) 0);

	xTaskCreate(eva_dts_loop, "dex_loop", 6765, (void*) 0, 1, (void*) 0);
}
