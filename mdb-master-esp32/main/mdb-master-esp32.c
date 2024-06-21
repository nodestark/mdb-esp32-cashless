#include <stdio.h>
#include "driver/gpio.h"

#include <rom/ets_sys.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_timer.h"

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

typedef enum MACHINE_STATE {
	INACTIVE_STATE, DISABLED_STATE, ENABLED_STATE, IDLE_STATE, VEND_STATE
} machine_state_t;

machine_state_t machine_state = INACTIVE_STATE;

void write_9( uint16_t nth9 ) {

	gpio_set_level(GPIO_NUM_22, 0); // start
	ets_delay_us(104);

	for(uint8_t x= 0; x < 9; x++){

		gpio_set_level(GPIO_NUM_22, (nth9 >> x) & 1);
		ets_delay_us(104); // 9600bps
	}

	gpio_set_level(GPIO_NUM_22, 1); // stop
	ets_delay_us(104);
}

uint16_t read_9() {

	uint16_t coming_read = 0;

	while (gpio_get_level(GPIO_NUM_21))
		;

	ets_delay_us(156);
	for(uint8_t x= 0; x < 9; x++){

		coming_read |= (gpio_get_level(GPIO_NUM_21) << x);
		ets_delay_us(104); // 9600bps
	}

	return coming_read;
}

static QueueHandle_t button_receive_queue = NULL;

void button_loop(void *pvParameters) {

	gpio_set_direction(GPIO_NUM_0, GPIO_MODE_INPUT);
	gpio_set_pull_mode(GPIO_NUM_0, GPIO_PULLUP_ONLY);

	for(;;){

		if( gpio_get_level(GPIO_NUM_0) == 0 ){

			while(gpio_get_level(GPIO_NUM_0) == 0);

			uint32_t time= esp_timer_get_time();
			xQueueSend(button_receive_queue, &time, 0);
		}

		vTaskDelay(34 / portTICK_PERIOD_MS);
	}
}

struct flow_payload_msg_t {
	uint8_t payload[256];
	uint8_t length;
};

static QueueHandle_t payload_receive_queue = NULL;

void payload_loop(void *pvParameters) {

	struct flow_payload_msg_t msg = {.length = 0};

	for (;;) {

		uint16_t coming_read = msg.payload[msg.length++] = read_9();

		if(coming_read & BIT_MODE_SET) {

			xQueueSend(payload_receive_queue, &msg, 0);
			msg.length = 0;
		}
	}
}

void writePayload_ttl9(uint8_t *mdb_payload, uint8_t mdb_length) {

	uint8_t checksum= 0;

	write_9((checksum = mdb_payload[0]) | BIT_MODE_SET);
	for (uint8_t x = 1; x < mdb_length; x++) {
		write_9(mdb_payload[x]);

		checksum += mdb_payload[x];
	}

	write_9(checksum);
}

void mdb_loop(void *pvParameters) {

	gpio_set_direction(GPIO_NUM_21, GPIO_MODE_INPUT);
	gpio_set_direction(GPIO_NUM_22, GPIO_MODE_OUTPUT);

	for (;;) {

		uint8_t mdb_payload[256];

		struct flow_payload_msg_t msg;

		if(machine_state == DISABLED_STATE) {

			uint16_t maxPrice = 200;
			uint16_t minPrice = 100;

			mdb_payload[0] = (0x10 /*Cashless Device #1*/ & BIT_ADD_SET) | (SETUP & BIT_CMD_SET);
			mdb_payload[1] = 0x01; // Max/Min Prices
			mdb_payload[2] = maxPrice >> 8;
			mdb_payload[3] = maxPrice;
			mdb_payload[4] = minPrice >> 8;
			mdb_payload[5] = minPrice;

			writePayload_ttl9(&mdb_payload, 6);

			xQueueReceive(payload_receive_queue, &msg, 500 / portTICK_PERIOD_MS); // ACK*

			mdb_payload[0] = (0x10 /*Cashless Device #1*/ & BIT_ADD_SET) | (EXPANSION & BIT_CMD_SET);
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

			writePayload_ttl9(&mdb_payload, 31);

			if (xQueueReceive(payload_receive_queue, &msg, 200 / portTICK_PERIOD_MS)) {
				// Peripheral ID

				write_9(ACK | BIT_MODE_SET);

				mdb_payload[0] = (0x10 /*Cashless Device #1*/ & BIT_ADD_SET) | (READER & BIT_CMD_SET);
				mdb_payload[1] = 0x01; // Reader Enable

				writePayload_ttl9(&mdb_payload, 2);

				xQueueReceive(payload_receive_queue, &msg, 200 / portTICK_PERIOD_MS); // ACK*

				machine_state = ENABLED_STATE;
			}

		} else if ( machine_state == INACTIVE_STATE ){

			mdb_payload[0] = (0x10 /*Cashless Device #1*/ & BIT_ADD_SET) | (RESET & BIT_CMD_SET);
			writePayload_ttl9(&mdb_payload, 1);

			xQueueReceive(payload_receive_queue, &msg, 200 / portTICK_PERIOD_MS); // ACK*

			mdb_payload[0] = (0x10 /*Cashless Device #1*/ & BIT_ADD_SET) | (POLL & BIT_CMD_SET);

			writePayload_ttl9(&mdb_payload, 1);

			if (xQueueReceive(payload_receive_queue, &msg, 200 / portTICK_PERIOD_MS)) {

				if(msg.payload[0] == 0x00 /*Just Reset*/){

					mdb_payload[0] = (0x10 /*Cashless Device #1*/ & BIT_ADD_SET) | (SETUP & BIT_CMD_SET);
					mdb_payload[1] = 0x00; 			// Config Data
					mdb_payload[2] = 1; 			// VMC Feature Level
					mdb_payload[3] = 0; 			// Columns on Display
					mdb_payload[4] = 0; 			// Rows on Display
					mdb_payload[5] = 0b00000001; 	// Display Information

					writePayload_ttl9(&mdb_payload, 6);

					if (xQueueReceive(payload_receive_queue, &msg, 200 / portTICK_PERIOD_MS)) {
						// Reader Config

						machine_state = DISABLED_STATE;
					}
				}
			}

		} else {

			mdb_payload[0] = (0x10 /*Cashless Device #1*/ & BIT_ADD_SET) | (POLL & BIT_CMD_SET);
			writePayload_ttl9(&mdb_payload, 1);

			struct flow_payload_msg_t msg;
			if ( xQueueReceive(payload_receive_queue, &msg, 500 / portTICK_PERIOD_MS) ){

				uint32_t button_time;

				if(msg.payload[0] == 0x07 /*End Session*/){
					machine_state = ENABLED_STATE;

				} else if(msg.payload[0] == 0x06 /*Vend Denied*/){

					mdb_payload[0] = (0x10 /*Cashless Device #1*/ & BIT_ADD_SET) | (VEND & BIT_CMD_SET);
					mdb_payload[1] = 0x04; // Session Complete

					writePayload_ttl9(&mdb_payload, 2);

					xQueueReceive(payload_receive_queue, &msg, 500 / portTICK_PERIOD_MS); // ACK*
					machine_state = IDLE_STATE;

				} else if(msg.payload[0] == 0x05 /*Vend Approved*/){

					uint16_t vendAmount = (msg.payload[1] << 8) | msg.payload[2];

					uint16_t itemNumber = 0;

					mdb_payload[0] = (0x10 /*Cashless Device #1*/ & BIT_ADD_SET) | (VEND & BIT_CMD_SET);
					mdb_payload[1] = 0x02; // Vend Success
					mdb_payload[2] = itemNumber >> 8;
					mdb_payload[3] = itemNumber;

					writePayload_ttl9(&mdb_payload, 4);

					xQueueReceive(payload_receive_queue, &msg, 500 / portTICK_PERIOD_MS); // ACK*

					machine_state = IDLE_STATE;

					mdb_payload[0] = (0x10 /*Cashless Device #1*/ & BIT_ADD_SET) | (VEND & BIT_CMD_SET);
					mdb_payload[1] = 0x04; // Session Complete

					writePayload_ttl9(&mdb_payload, 2);

					xQueueReceive(payload_receive_queue, &msg, 500 / portTICK_PERIOD_MS); // ACK*

				} else if(msg.payload[0] == 0x04 /*Session Cancel Request*/){
					// IDLE_STATE

					mdb_payload[0] = (0x10 /*Cashless Device #1*/ & BIT_ADD_SET) | (VEND & BIT_CMD_SET);
					mdb_payload[1] = 0x04; // Session Complete

					writePayload_ttl9(&mdb_payload, 2);

					xQueueReceive(payload_receive_queue, &msg, 500 / portTICK_PERIOD_MS); // ACK*

				} else if(msg.payload[0] == 0x03 /*Begin Session*/){
					// ENABLED_STATE

					machine_state = IDLE_STATE;

					uint16_t fundsAvailable = (msg.payload[1] << 8) | msg.payload[2];

					write_9(ACK | BIT_MODE_SET);

				} else if(msg.payload[0] == 0x0b /*Command Out of Sequence*/) {

					machine_state = INACTIVE_STATE;

				} else if (xQueueReceive(button_receive_queue, &button_time, 0)){

					uint16_t itemPrice = 5000;
					uint16_t itemNumber = 0;

					/*Vend Cancel (Vend Request)*/

					if(machine_state == IDLE_STATE){

						mdb_payload[0] = (0x10 /*Cashless Device #1*/ & BIT_ADD_SET) | (VEND & BIT_CMD_SET);
						mdb_payload[1] = 0x00; // Vend Request
						mdb_payload[2] = itemPrice >> 8;
						mdb_payload[3] = itemPrice;

						mdb_payload[4] = itemNumber >> 8;
						mdb_payload[5] = itemNumber;

						writePayload_ttl9(&mdb_payload, 6);

						xQueueReceive(payload_receive_queue, &msg, 500 / portTICK_PERIOD_MS); // ACK*

						machine_state = VEND_STATE;

					} else if(machine_state == ENABLED_STATE){

						mdb_payload[0] = (0x10 /*Cashless Device #1*/ & BIT_ADD_SET) | (VEND & BIT_CMD_SET);
						mdb_payload[1] = 0x05; // Cash Sale
						mdb_payload[2] = itemPrice >> 8;
						mdb_payload[3] = itemPrice;

						mdb_payload[4] = itemNumber >> 8;
						mdb_payload[5] = itemNumber;

						writePayload_ttl9(&mdb_payload, 6);

						xQueueReceive(payload_receive_queue, &msg, 500 / portTICK_PERIOD_MS); // ACK*
					}
				}

			} else machine_state = INACTIVE_STATE;
		}

		{
			mdb_payload[0] = (0x60 /*Cashless Device #2*/ & BIT_ADD_SET) | (RESET & BIT_CMD_SET);

			writePayload_ttl9(&mdb_payload, 1);

			struct flow_payload_msg_t msg;
			xQueueReceive(payload_receive_queue, &msg, 500 / portTICK_PERIOD_MS); // ACK
		}
	}
}

void app_main(void) {

	button_receive_queue = xQueueCreate(1 /*queue-length*/, sizeof(uint32_t));
	xTaskCreate(button_loop, "button_loop", 6765, (void*) 0, 1, (void*) 0);

	payload_receive_queue = xQueueCreate(1 /*queue-length*/, sizeof(struct flow_payload_msg_t));
	xTaskCreate(payload_loop, "payload_loop", 6765, (void*) 0, 1, (void*) 0);

	xTaskCreate(mdb_loop, "mdb_loop", 6765, (void*) 0, 1, (void*) 0);
}
