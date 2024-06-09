#include <stdio.h>
#include "driver/gpio.h"

#include <rom/ets_sys.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

void mdb_loop(void *pvParameters) {

	gpio_set_direction(GPIO_NUM_22, GPIO_MODE_OUTPUT);

	for (;;) {

		{
			uint8_t checksum= 0;

			uint8_t mMdb_payload[256];
			uint8_t available_tx= 0;

			mMdb_payload[available_tx++] = 0x10; // Cashless Device #1

			write_9((checksum= mMdb_payload[0]) | 0b100000000);
			for(uint8_t x= 1; x < available_tx; x++){
				write_9(mMdb_payload[x]);

				checksum += mMdb_payload[x];
			}

			write_9( checksum );
		}
//		-----------------------------------------------------------
		vTaskDelay(200 / portTICK_PERIOD_MS);

		{
			uint8_t checksum= 0;

			uint8_t mMdb_payload[256];
			uint8_t available_tx= 0;

			mMdb_payload[available_tx++] = 0x60; // Cashless Device #2

			write_9((checksum= mMdb_payload[0]) | 0b100000000);
			for(uint8_t x= 1; x < available_tx; x++){
				write_9(mMdb_payload[x]);

				checksum += mMdb_payload[x];
			}

			write_9( checksum );
		}
//		-----------------------------------------------------------
		vTaskDelay(200 / portTICK_PERIOD_MS);
	}
}

void app_main(void) {

	xTaskCreate(mdb_loop, "mdb_loop", 10946, (void*) 0, 1, (void*) 0);
}
