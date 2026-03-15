#ifndef MDB_9TH_H
#define MDB_9TH_H

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define PIN_MDB_RX   GPIO_NUM_4
#define PIN_MDB_TX   GPIO_NUM_5

extern QueueHandle_t mdb_queue;

uint16_t read_9(uint8_t *checksum);
void write_payload_9(uint8_t *mdb_payload, uint8_t length);

void mdb_9th_init(void);

#endif