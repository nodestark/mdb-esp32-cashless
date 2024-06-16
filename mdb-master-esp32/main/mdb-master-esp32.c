#include <stdio.h>
#include "driver/gpio.h"

#include <rom/ets_sys.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

struct flow_payload_msg_t {
	uint8_t payload[256];
	uint8_t length;
};

static QueueHandle_t payload_receive_queue = NULL;

void readPayload_loop(void *pvParameters) {

	struct flow_payload_msg_t msg = {.length = 0};

	uint8_t checksum = 0;
	for (;;) {

		uint16_t coming_read = read_9();

		msg.payload[msg.length++] = coming_read;

		checksum += coming_read;

		if(coming_read & BIT_MODE_SET) {

			if(checksum == (uint8_t) coming_read){

				xQueueSend(payload_receive_queue, &msg, 0);
			}

			checksum = msg.length = 0;
		}
	}
}

void writePayload_ttl9(uint8_t *mdb_payload, uint8_t mdb_length) {

	uint8_t checksum= 0;

	write_9((checksum = mdb_payload[0]) | 0b100000000);
	for (uint8_t x = 1; x < mdb_length; x++) {
		write_9(mdb_payload[x]);

		checksum += mdb_payload[x];
	}

	write_9(checksum);
}

void mdb_loop(void *pvParameters) {

	gpio_set_direction(GPIO_NUM_22, GPIO_MODE_OUTPUT);

	for (;;) {

		{
			uint8_t mdb_payload[256];
			uint8_t mdb_length= 0;

			mdb_payload[0] = (0x10 /*Cashless Device #1*/ & BIT_ADD_SET) | (RESET & BIT_CMD_SET);
			mdb_length= 1;

			writePayload_ttl9(&mdb_payload, mdb_length);

			struct flow_payload_msg_t msg;
			if ( xQueueReceive(payload_receive_queue, &msg, 200 / portTICK_PERIOD_MS) ){
				if(msg.payload[0] == ACK){

				}
			}
		}

		{
			uint8_t mdb_payload[256];
			uint8_t mdb_length= 0;

			mdb_payload[0] = (0x60 /*Cashless Device #2*/ & BIT_ADD_SET) | (RESET & BIT_CMD_SET);
			mdb_length= 1;

			writePayload_ttl9(&mdb_payload, mdb_length);

			struct flow_payload_msg_t msg;
			if ( xQueueReceive(payload_receive_queue, &msg, 500 / portTICK_PERIOD_MS) ){
				if(msg.payload[0] == ACK){

				}
			}
		}
	}
}

void app_main(void) {

	payload_receive_queue = xQueueCreate(1 /*queue-length*/, sizeof(struct flow_payload_msg_t));
	xTaskCreate(readPayload_loop, "readPayload_loop", 10946, (void*) 0, 1, (void*) 0);

	xTaskCreate(mdb_loop, "mdb_loop", 10946, (void*) 0, 1, (void*) 0);
}
