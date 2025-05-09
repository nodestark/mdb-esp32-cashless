/*
 * mdb-master-esp32.c - Vending machine controller
 *
 * Written by Leonardo Soares <leonardobsi@gmail.com>
 *
 */

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

uint8_t coils[1][2] = {{100 /*PA102*/, 0 /*PA201*/}};

void write_9(uint16_t nth9) {

	gpio_set_level(pin_mdb_tx, 0); // start
	ets_delay_us(104);

	for (uint8_t x = 0; x < 9; x++) {

		gpio_set_level(pin_mdb_tx, (nth9 >> x) & 1);
		ets_delay_us(104); // 9600bps
	}

	gpio_set_level(pin_mdb_tx, 1); // stop
	ets_delay_us(104);
}

uint16_t read_9() {

	uint16_t coming_read = 0;

	while (gpio_get_level(pin_mdb_rx))
		;

	ets_delay_us(156);
	for (uint8_t x = 0; x < 9; x++) {

		coming_read |= (gpio_get_level(pin_mdb_rx) << x);
		ets_delay_us(104); // 9600bps
	}

	return coming_read;
}

static QueueHandle_t button_receive_queue = (void*) 0;

static void IRAM_ATTR button_isr_handler(void* arg) {

	uint8_t btn_selected = 0;
	xQueueSendFromISR(button_receive_queue, &btn_selected, 0);
}

struct flow_payload_msg_t {
	uint8_t payload[256];
};

static QueueHandle_t payload_receive_queue = NULL;

void payload_loop(void *pvParameters) {

	struct flow_payload_msg_t msg;

	uint8_t idx = 0;
	for (;;) {

		uint16_t coming_read = read_9();

		msg.payload[idx++] = coming_read;

		if (coming_read & BIT_MODE_SET) {

			xQueueSend(payload_receive_queue, &msg, 0);
			idx = 0;
		}
	}
}

void writePayload_ttl9(uint8_t *mdb_payload, uint8_t mdb_length) {

	uint8_t checksum = 0;

	write_9((checksum = mdb_payload[0]) | BIT_MODE_SET);
	for (uint8_t x = 1; x < mdb_length; x++) {
		write_9(mdb_payload[x]);

		checksum += mdb_payload[x];
	}

	write_9(checksum);
}

void mdb_loop(void *pvParameters) {

	gpio_set_direction(pin_mdb_led, GPIO_MODE_OUTPUT);

	gpio_set_direction(pin_mdb_rx, GPIO_MODE_INPUT);
	gpio_set_direction(pin_mdb_tx, GPIO_MODE_OUTPUT);

	uint8_t btn_selected = -1;

	uint8_t mdb_payload[256];

	struct flow_payload_msg_t msg;

	for (;;) {

		if (machine_state == INACTIVE_STATE) {

			mdb_payload[0] = (0x10 /*Cashless Device #1*/& BIT_ADD_SET) | (RESET & BIT_CMD_SET);
			writePayload_ttl9((uint8_t*) &mdb_payload, 1);

			xQueueReceive(payload_receive_queue, &msg, pdMS_TO_TICKS(reader0x10.responseTimeSec * 1000)); // ACK*

			mdb_payload[0] = (0x10 /*Cashless Device #1*/& BIT_ADD_SET) | (POLL & BIT_CMD_SET);

			writePayload_ttl9((uint8_t*) &mdb_payload, 1);

			if (xQueueReceive(payload_receive_queue, &msg, pdMS_TO_TICKS(reader0x10.responseTimeSec * 1000))) {

				if (msg.payload[0] == 0x00 /*Just Reset*/) {

					write_9(ACK | BIT_MODE_SET);

					mdb_payload[0] = (0x10 /*Cashless Device #1*/& BIT_ADD_SET) | (SETUP & BIT_CMD_SET);
					mdb_payload[1] = 0x00; 			// Config Data
					mdb_payload[2] = 1; 			// VMC Feature Level
					mdb_payload[3] = 0; 			// Columns on Display
					mdb_payload[4] = 0; 			// Rows on Display
					mdb_payload[5] = 0b00000001; 	// Display Information

					writePayload_ttl9((uint8_t*) &mdb_payload, 6);

					if (xQueueReceive(payload_receive_queue, &msg, pdMS_TO_TICKS(reader0x10.responseTimeSec * 1000))) {
						// Reader Config

						reader0x10.scaleFactor = msg.payload[4];
						reader0x10.decimalPlaces = msg.payload[5];
						reader0x10.responseTimeSec = msg.payload[6];
						reader0x10.miscellaneous = msg.payload[7];

						machine_state = DISABLED_STATE;
					}
				}
			}

		} else if (machine_state == DISABLED_STATE) {

			uint16_t maxPrice = to_scale_factor(2.00, reader0x10.scaleFactor, reader0x10.decimalPlaces);
			uint16_t minPrice = to_scale_factor(1.00, reader0x10.scaleFactor, reader0x10.decimalPlaces);

			mdb_payload[0] = (0x10 /*Cashless Device #1*/& BIT_ADD_SET) | (SETUP & BIT_CMD_SET);
			mdb_payload[1] = 0x01; // Max/Min Prices
			mdb_payload[2] = maxPrice >> 8;
			mdb_payload[3] = maxPrice;
			mdb_payload[4] = minPrice >> 8;
			mdb_payload[5] = minPrice;

			writePayload_ttl9((uint8_t*) &mdb_payload, 6);

			xQueueReceive(payload_receive_queue, &msg, pdMS_TO_TICKS(reader0x10.responseTimeSec * 1000)); // ACK*

			mdb_payload[0] = (0x10 /*Cashless Device #1*/& BIT_ADD_SET) | (EXPANSION & BIT_CMD_SET);
			mdb_payload[1] = 0x00; // Request ID

			mdb_payload[2] = ' '; // Manufacturer code
			mdb_payload[3] = ' ';
			mdb_payload[4] = ' ';

			mdb_payload[5] = ' '; // Serial Number
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
			mdb_payload[16] = ' ';

			mdb_payload[17] = ' '; // Model Number
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
			mdb_payload[28] = ' ';

			mdb_payload[29] = ' '; // Software Version
			mdb_payload[30] = ' ';

			writePayload_ttl9((uint8_t*) &mdb_payload, 31);

			if (xQueueReceive(payload_receive_queue, &msg, pdMS_TO_TICKS(reader0x10.responseTimeSec * 1000))) {
				// Peripheral ID

				write_9(ACK | BIT_MODE_SET);

				mdb_payload[0] = (0x10 /*Cashless Device #1*/& BIT_ADD_SET) | (READER & BIT_CMD_SET);
				mdb_payload[1] = 0x01; // Reader Enable

				writePayload_ttl9((uint8_t*) &mdb_payload, 2);

				xQueueReceive(payload_receive_queue, &msg, pdMS_TO_TICKS(reader0x10.responseTimeSec * 1000)); // ACK*

				machine_state = ENABLED_STATE;
			}

		} else {

			mdb_payload[0] = (0x10 /*Cashless Device #1*/& BIT_ADD_SET) | (POLL & BIT_CMD_SET);
			writePayload_ttl9((uint8_t*) &mdb_payload, 1);

			if (xQueueReceive(payload_receive_queue, &msg, pdMS_TO_TICKS(reader0x10.responseTimeSec * 1000))) {

				if (msg.payload[0] == 0x07 /*End Session*/) {
					machine_state = ENABLED_STATE;

				} else if (msg.payload[0] == 0x06 /*Vend Denied*/) {

					mdb_payload[0] = (0x10 /*Cashless Device #1*/& BIT_ADD_SET) | (VEND & BIT_CMD_SET);
					mdb_payload[1] = 0x04; // Session Complete

					writePayload_ttl9((uint8_t*) &mdb_payload, 2);

					xQueueReceive(payload_receive_queue, &msg, pdMS_TO_TICKS(reader0x10.responseTimeSec * 1000)); // ACK*
					machine_state = IDLE_STATE;

				} else if (msg.payload[0] == 0x05 /*Vend Approved*/) {

					uint16_t vendAmount = (msg.payload[1] << 8) | msg.payload[2];

					uint16_t itemNumber = btn_selected;

					mdb_payload[0] = (0x10 /*Cashless Device #1*/& BIT_ADD_SET) | (VEND & BIT_CMD_SET);
					mdb_payload[1] = 0x02; // Vend Success
					mdb_payload[2] = itemNumber >> 8;
					mdb_payload[3] = itemNumber;

					writePayload_ttl9((uint8_t*) &mdb_payload, 4);

					xQueueReceive(payload_receive_queue, &msg, pdMS_TO_TICKS(reader0x10.responseTimeSec * 1000)); // ACK*

					machine_state = IDLE_STATE;

					++coils[btn_selected][1];

					mdb_payload[0] = (0x10 /*Cashless Device #1*/& BIT_ADD_SET) | (VEND & BIT_CMD_SET);
					mdb_payload[1] = 0x04; // Session Complete

					writePayload_ttl9((uint8_t*) &mdb_payload, 2);

					xQueueReceive(payload_receive_queue, &msg, pdMS_TO_TICKS(reader0x10.responseTimeSec * 1000)); // ACK*

				} else if (msg.payload[0] == 0x04 /*Session Cancel Request*/) {
					// IDLE_STATE

					mdb_payload[0] = (0x10 /*Cashless Device #1*/& BIT_ADD_SET) | (VEND & BIT_CMD_SET);
					mdb_payload[1] = 0x04; // Session Complete

					writePayload_ttl9((uint8_t*) &mdb_payload, 2);

					xQueueReceive(payload_receive_queue, &msg, pdMS_TO_TICKS(reader0x10.responseTimeSec * 1000)); // ACK*

				} else if (msg.payload[0] == 0x03 /*Begin Session*/) {
					// ENABLED_STATE

					machine_state = IDLE_STATE;

					uint16_t fundsAvailable = (msg.payload[1] << 8) | msg.payload[2];

					write_9(ACK | BIT_MODE_SET);

				} else if (msg.payload[0] == 0x0b /*Command Out of Sequence*/) {

					machine_state = INACTIVE_STATE;

				} else if (xQueueReceive(button_receive_queue, &btn_selected, 0)) {

					uint16_t itemPrice = to_scale_factor(coils[btn_selected][0] / 100.0f, reader0x10.scaleFactor, reader0x10.decimalPlaces);
					uint16_t itemNumber = btn_selected;

					/*Vend Cancel (Vend Request)*/

					if (machine_state == IDLE_STATE) {

						mdb_payload[0] = (0x10 /*Cashless Device #1*/ & BIT_ADD_SET) | (VEND & BIT_CMD_SET);
						mdb_payload[1] = 0x00; // Vend Request
						mdb_payload[2] = itemPrice >> 8;
						mdb_payload[3] = itemPrice;

						mdb_payload[4] = itemNumber >> 8;
						mdb_payload[5] = itemNumber;

						writePayload_ttl9((uint8_t*) &mdb_payload, 6);

						xQueueReceive(payload_receive_queue, &msg, pdMS_TO_TICKS( reader0x10.responseTimeSec * 1000)); // ACK*

						machine_state = VEND_STATE;

					} else if (machine_state == ENABLED_STATE) {

						mdb_payload[0] = (0x10 /*Cashless Device #1*/ & BIT_ADD_SET) | (VEND & BIT_CMD_SET);
						mdb_payload[1] = 0x05; // Cash Sale
						mdb_payload[2] = itemPrice >> 8;
						mdb_payload[3] = itemPrice;

						mdb_payload[4] = itemNumber >> 8;
						mdb_payload[5] = itemNumber;

						writePayload_ttl9((uint8_t*) &mdb_payload, 6);

						xQueueReceive(payload_receive_queue, &msg, pdMS_TO_TICKS( reader0x10.responseTimeSec * 1000)); // ACK*

						++coils[btn_selected][1];
					}
				}

			} else
				machine_state = INACTIVE_STATE;
		}

		{
			mdb_payload[0] = (0x60 /*Cashless Device #2*/& BIT_ADD_SET) | (RESET & BIT_CMD_SET);

			writePayload_ttl9((uint8_t*) &mdb_payload, 1);

			xQueueReceive(payload_receive_queue, &msg, 500 / portTICK_PERIOD_MS); // ACK
		}

		gpio_set_level(pin_mdb_led, machine_state > ENABLED_STATE);
	}
}

void eva_loop(void *pvParameters) {

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
		uart_read_bytes(UART_NUM_1, &data, 2, pdMS_TO_TICKS(10));
		if (data[0] != 0x10 || data[1] != 0x01)
			continue;

		// Communication ID <-
		uart_read_bytes(UART_NUM_1, &data, 10, pdMS_TO_TICKS(10));

		// Operation Request <-
		uart_read_bytes(UART_NUM_1, &data, 1, pdMS_TO_TICKS(10));

		// Revision & Level <-
		uart_read_bytes(UART_NUM_1, &data, 6, pdMS_TO_TICKS(10));

		// DLE ETX <-
		uart_read_bytes(UART_NUM_1, &data, 2, pdMS_TO_TICKS(10));
		if (data[0] != 0x10 || data[1] != 0x03)
			continue;

		// CRC-16 <-
		uart_read_bytes(UART_NUM_1, &data, 2, pdMS_TO_TICKS(10));

		// DLE 1 ->
		uart_write_bytes(UART_NUM_1, "\x10\x31", 2);

		// EOT <-
		uart_read_bytes(UART_NUM_1, &data, 1, pdMS_TO_TICKS(10));
		if (data[0] != 0x04)
			continue;

		// -------------------------------------- Second Handshake --------------------------------------

		// ENQ ->
		uart_write_bytes(UART_NUM_1, "\x05", 1);

		// DLE 0 <-
		uart_read_bytes(UART_NUM_1, &data, 2, pdMS_TO_TICKS(10));
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
		uart_read_bytes(UART_NUM_1, &data, 2, pdMS_TO_TICKS(10));
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
			uart_read_bytes(UART_NUM_1, &data, 2, pdMS_TO_TICKS(10));
			if (block & 0x01) {
				if (data[0] != 0x10 || data[1] != '1')
					break;
			} else {
				if (data[0] != 0x10 || data[1] != '0')
					break;
			}
			++block;

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
		uart_read_bytes(UART_NUM_1, &data, 2, pdMS_TO_TICKS(10));
		if (block & 0x01) {
			if (data[0] != 0x10 || data[1] != '1')
				continue;
		} else {
			if (data[0] != 0x10 || data[1] != '0')
				continue;
		}
		++block;

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
		uart_read_bytes(UART_NUM_1, &data, 2, pdMS_TO_TICKS(10));
		if (block & 0x01) {
			if (data[0] != 0x10 || data[1] != '1')
				continue;
		} else {
			if (data[0] != 0x10 || data[1] != '0')
				continue;
		}

		// EOT ->
		uart_write_bytes(UART_NUM_1, "\x04", 1);
	}
}

void app_main(void) {

	button_receive_queue = xQueueCreate(1 /*queue-length*/, sizeof(uint8_t));

	gpio_config_t io_conf = {
			.intr_type = GPIO_INTR_NEGEDGE, // Borda de descida (pressionado)
			.mode = GPIO_MODE_INPUT,
			.pin_bit_mask = (1ULL << GPIO_NUM_0),
			.pull_up_en = GPIO_PULLUP_ENABLE,
			.pull_down_en = GPIO_PULLDOWN_DISABLE, };

	gpio_config(&io_conf);

	gpio_install_isr_service(0 /*default*/);
	gpio_isr_handler_add(GPIO_NUM_0, button_isr_handler, (void*) 0);

	// Initialize UART1 driver and configure TX/RX pins
	uart_driver_install(UART_NUM_1, 256, 256, 0, (void*) 0, 0);
	uart_set_pin(UART_NUM_1, GPIO_NUM_17, GPIO_NUM_18, -1, -1);

	uart_config_t uart_config_1 = {
			.baud_rate = 9600,
			.data_bits = UART_DATA_8_BITS,
			.parity = UART_PARITY_DISABLE,
			.stop_bits = UART_STOP_BITS_1,
			.flow_ctrl = UART_HW_FLOWCTRL_DISABLE };

	uart_param_config(UART_NUM_1, &uart_config_1);

	//
	payload_receive_queue = xQueueCreate(1 /*queue-length*/, sizeof(struct flow_payload_msg_t));
	xTaskCreate(payload_loop, "payload_loop", 6765, (void*) 0, 1, (void*) 0);

	xTaskCreate(mdb_loop, "mdb_loop", 6765, (void*) 0, 1, (void*) 0);

	xTaskCreate(eva_loop, "eva_loop", 6765, (void*) 0, 1, (void*) 0);
}
//
