/*
 * VMflow.xyz
 *
 * mdb-master-esp32s3.c - Vending machine controller with WebUI
 *
 */

#include <esp_log.h>

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <float.h>
#include <math.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <rom/ets_sys.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_random.h>

#include "led_strip.h"
#include "webui_server.h"

#define TAG "mdb_vmc"

#define pin_dex_rx  	GPIO_NUM_18
#define pin_dex_tx      GPIO_NUM_17
#define pin_mdb_rx  	GPIO_NUM_4  // Pin to receive data from MDB
#define pin_mdb_tx  	GPIO_NUM_5  // Pin to transmit data to MDB
#define pin_mdb_led 	GPIO_NUM_48 // LED to indicate MDB state

// Functions for scale factor conversion
#define TO_SCALE_FACTOR(p, scale_to, dec_to) (p / scale_to / pow(10, -(dec_to) ))               // Converts to scale factor
#define FROM_SCALE_FACTOR(p, scale_from, dec_from) (p * scale_from * pow(10, -(dec_from) ))     // Converts from scale factor

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

/* ---------- Shared state ---------- */

struct reader_t reader0x08 = { .machineState = INACTIVE_STATE };
struct reader_t reader0x10 = { .machineState = INACTIVE_STATE };
struct reader_t reader0x60 = { .machineState = INACTIVE_STATE };

QueueHandle_t command_queue = NULL;

/* ---------- Product catalog ---------- */

const product_t products[MAX_PRODUCTS] = {
    { 1,  "Cola",           1.50f },
    { 2,  "Fanta",          1.50f },
    { 3,  "Water",          1.00f },
    { 4,  "Iced Tea",       1.80f },
    { 5,  "Coffee",         2.00f },
    { 6,  "Snickers",       1.20f },
    { 7,  "Twix",           1.20f },
    { 8,  "Chips",          2.50f },
    { 9,  "Gummy Bears",    1.00f },
    { 10, "Red Bull",       2.80f },
};

/* PA102 - Product Price
 * PA201 - Number of Products Vended Since Initialisation
 * */
typedef struct {
    float pa102;
    uint16_t pa201;
} coil_t;

coil_t coils[1] = {
    {.pa102 = 1.25f, .pa201 = 0}
};

/* ---------- MDB Log Ringbuffer ---------- */

mdb_log_entry_t mdb_log[MDB_LOG_SIZE];
volatile int mdb_log_head = 0;
volatile int mdb_log_count = 0;

void mdb_log_add(char direction, const char *fmt, ...) {
    mdb_log_entry_t *entry = &mdb_log[mdb_log_head % MDB_LOG_SIZE];
    entry->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);
    entry->direction = direction;

    va_list args;
    va_start(args, fmt);
    vsnprintf(entry->description, sizeof(entry->description), fmt, args);
    va_end(args);

    mdb_log_head++;
    if (mdb_log_count < MDB_LOG_SIZE) mdb_log_count++;
}

/* ---------- Button ISR ---------- */

static volatile int64_t last_button_time = 0;

static void IRAM_ATTR button0_isr_handler(void* arg) {

	int64_t now = esp_timer_get_time();

	if (now - last_button_time > 500000) { // 500ms debounce
        vmc_command_t cmd = {
            .type = CMD_VEND_REQUEST,
            .target_addr = 0,
            .item_number = (esp_random() % 10) + 1,
            .item_price = 0,  // 0 = random price
        };
        xQueueSendFromISR(command_queue, &cmd, NULL);
        last_button_time = now;
    }
}

/* ---------- MDB 9-bit UART ---------- */

void write_9(uint16_t nth9) {
    uint8_t ones = __builtin_popcount((uint8_t) nth9);

    uart_wait_tx_done(UART_NUM_2, pdMS_TO_TICKS(250));

    if ((nth9 >> 8) & 1) {
        uart_set_parity(UART_NUM_2, ones % 2 ? UART_PARITY_EVEN : UART_PARITY_ODD);
    } else {
        uart_set_parity(UART_NUM_2, ones % 2 ? UART_PARITY_ODD : UART_PARITY_EVEN);
    }

    uart_write_bytes(UART_NUM_2, (uint8_t*) &nth9, 1);
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

/* ---------- MDB Poll Cashless ---------- */

// Poll one cashless device through its full MDB state machine.
// addr: MDB address (0x10 for Cashless #1, 0x60 for Cashless #2)
void mdb_poll_cashless(uint8_t addr, struct reader_t *reader,
                       uint8_t *mdb_payload_tx, uint8_t *mdb_payload_rx)
{
	const char *name = (addr == 0x10) ? "#1(0x10)" : "#2(0x60)";
	size_t len;

	uart_flush(UART_NUM_2);

	if (reader->machineState == INACTIVE_STATE) {

		mdb_payload_tx[0] = (addr & BIT_ADD_SET) | (RESET & BIT_CMD_SET);
		write_payload_9(mdb_payload_tx, 1);
		mdb_log_add('T', "%s RESET", name);

		len = uart_read_bytes(UART_NUM_2, mdb_payload_rx, 1, pdMS_TO_TICKS(30)); // ACK*

		if (len == 1) {
			mdb_log_add('R', "%s ACK (0x%02X)", name, mdb_payload_rx[0]);

			mdb_payload_tx[0] = (addr & BIT_ADD_SET) | (POLL & BIT_CMD_SET);
			write_payload_9(mdb_payload_tx, 1);
			mdb_log_add('T', "%s POLL", name);

			len = uart_read_bytes(UART_NUM_2, mdb_payload_rx, 2, pdMS_TO_TICKS(30)); // CHK*
			if (len != 2) return;

			mdb_log_add('R', "%s POLL resp: 0x%02X", name, mdb_payload_rx[0]);
			write_9(ACK | BIT_MODE_SET);

			if (mdb_payload_rx[0] == 0x00 /*Just Reset*/) {

				mdb_payload_tx[0] = (addr & BIT_ADD_SET) | (SETUP & BIT_CMD_SET);
				mdb_payload_tx[1] = 0x00; 			// Config Data
				mdb_payload_tx[2] = 1; 			    // VMC Feature Level
				mdb_payload_tx[3] = 0; 			    // Columns on Display
				mdb_payload_tx[4] = 0; 			    // Rows on Display
				mdb_payload_tx[5] = 0b00000001; 	// Display Information

				write_payload_9(mdb_payload_tx, 6);
				mdb_log_add('T', "%s SETUP Config (Level=1)", name);

				len = uart_read_bytes(UART_NUM_2, mdb_payload_rx, 9, pdMS_TO_TICKS(40)); // CHK*
				if (len != 9) return;

				write_9(ACK | BIT_MODE_SET);

				reader->featureLevel = mdb_payload_rx[1];
				reader->countryCode = (mdb_payload_rx[2] << 8) | mdb_payload_rx[3];
				reader->scaleFactor = mdb_payload_rx[4];
				reader->decimalPlaces = mdb_payload_rx[5];
				reader->responseTimeSec = mdb_payload_rx[6];
				reader->miscellaneous = mdb_payload_rx[7];

				reader->machineState = DISABLED_STATE;

				mdb_log_add('R', "%s Config: FL=%d SF=%d DP=%d", name,
				            reader->featureLevel, reader->scaleFactor, reader->decimalPlaces);
				ESP_LOGI(TAG, "%s Reader Config (SF=%d DP=%d)", name,
				         reader->scaleFactor, reader->decimalPlaces);
			}
		}

	} else if (reader->machineState == DISABLED_STATE) {

		// Calculate max/min prices from product catalog
		float maxPrice = products[0].price;
		float minPrice = products[0].price;
		for (int i = 1; i < MAX_PRODUCTS; i++) {
			if (products[i].price > maxPrice) maxPrice = products[i].price;
			if (products[i].price < minPrice) minPrice = products[i].price;
		}

		uint16_t scaledMaxPrice = TO_SCALE_FACTOR(maxPrice, reader->scaleFactor, reader->decimalPlaces);
		uint16_t scaledMinPrice = TO_SCALE_FACTOR(minPrice, reader->scaleFactor, reader->decimalPlaces);

		mdb_payload_tx[0] = (addr & BIT_ADD_SET) | (SETUP & BIT_CMD_SET);
		mdb_payload_tx[1] = 0x01; // Max|Min Prices
		mdb_payload_tx[2] = scaledMaxPrice >> 8;
		mdb_payload_tx[3] = scaledMaxPrice;
		mdb_payload_tx[4] = scaledMinPrice >> 8;
		mdb_payload_tx[5] = scaledMinPrice;

		write_payload_9(mdb_payload_tx, 6);
		mdb_log_add('T', "%s SETUP Max/Min (%d/%d)", name, scaledMaxPrice, scaledMinPrice);

		len = uart_read_bytes(UART_NUM_2, mdb_payload_rx, 1, pdMS_TO_TICKS(30)); // ACK*
		if (len != 1) return;

		mdb_payload_tx[0] = (addr & BIT_ADD_SET) | (EXPANSION & BIT_CMD_SET);
		mdb_payload_tx[1] = 0x00; // Request ID

		mdb_payload_tx[2] = 'V'; // Manufacturer code
		mdb_payload_tx[3] = 'M';
		mdb_payload_tx[4] = 'F';

		memset(&mdb_payload_tx[5], ' ', 12);  // Serial Number
		memset(&mdb_payload_tx[17], ' ', 12); // Model Number

		mdb_payload_tx[29] = '0'; // Software Version
		mdb_payload_tx[30] = '1';

		write_payload_9(mdb_payload_tx, 31);
		mdb_log_add('T', "%s EXPANSION Request ID", name);

		len = uart_read_bytes(UART_NUM_2, mdb_payload_rx, 31, pdMS_TO_TICKS(125)); // CHK*
		if (len != 31) return;

		write_9(ACK | BIT_MODE_SET);

		// Store expansion ID info in reader struct
		snprintf(reader->manufacturer, sizeof(reader->manufacturer), "%.*s", 3, &mdb_payload_rx[1]);
		snprintf(reader->serial, sizeof(reader->serial), "%.*s", 12, &mdb_payload_rx[4]);
		snprintf(reader->model, sizeof(reader->model), "%.*s", 12, &mdb_payload_rx[16]);
		snprintf(reader->sw_version, sizeof(reader->sw_version), "%.*s", 2, &mdb_payload_rx[28]);

		mdb_log_add('R', "%s ID: %s %s %s v%s", name,
		            reader->manufacturer, reader->serial, reader->model, reader->sw_version);
		ESP_LOGI(TAG, "%s Manufacture code: %s", name, reader->manufacturer);
		ESP_LOGI(TAG, "%s Serial number: %s", name, reader->serial);
		ESP_LOGI(TAG, "%s Model number: %s", name, reader->model);
		ESP_LOGI(TAG, "%s Software version: %s", name, reader->sw_version);

		ESP_LOGI(TAG, "%s Reader Enable", name);

		mdb_payload_tx[0] = (addr & BIT_ADD_SET) | (READER & BIT_CMD_SET);
		mdb_payload_tx[1] = 0x01; // Reader Enable

		write_payload_9(mdb_payload_tx, 2);
		mdb_log_add('T', "%s READER Enable", name);

		len = uart_read_bytes(UART_NUM_2, mdb_payload_rx, 1, pdMS_TO_TICKS(30)); // ACK*
		if (len != 1) return;

		reader->machineState = ENABLED_STATE;
		mdb_log_add('I', "%s State -> ENABLED", name);

	} else {

		// Check for commands from WebUI/button
		vmc_command_t cmd;
		bool has_cmd = false;
		if (xQueuePeek(command_queue, &cmd, 0) == pdTRUE) {
			// Check if this command targets this address (or any)
			if (cmd.target_addr == 0 || cmd.target_addr == addr) {
				xQueueReceive(command_queue, &cmd, 0);
				has_cmd = true;
			}
		}

		// Handle non-poll commands first
		if (has_cmd) {
			if (cmd.type == CMD_RESET) {
				mdb_log_add('I', "%s RESET requested", name);
				reader->machineState = INACTIVE_STATE;
				return;
			}
			if (cmd.type == CMD_READER_DISABLE) {
				mdb_payload_tx[0] = (addr & BIT_ADD_SET) | (READER & BIT_CMD_SET);
				mdb_payload_tx[1] = 0x00; // Reader Disable

				write_payload_9(mdb_payload_tx, 2);
				mdb_log_add('T', "%s READER Disable", name);

				len = uart_read_bytes(UART_NUM_2, mdb_payload_rx, 1, pdMS_TO_TICKS(30));
				reader->machineState = DISABLED_STATE;
				mdb_log_add('I', "%s State -> DISABLED", name);
				return;
			}
			if (cmd.type == CMD_READER_ENABLE && reader->machineState == DISABLED_STATE) {
				mdb_payload_tx[0] = (addr & BIT_ADD_SET) | (READER & BIT_CMD_SET);
				mdb_payload_tx[1] = 0x01; // Reader Enable

				write_payload_9(mdb_payload_tx, 2);
				mdb_log_add('T', "%s READER Enable", name);

				len = uart_read_bytes(UART_NUM_2, mdb_payload_rx, 1, pdMS_TO_TICKS(30));
				reader->machineState = ENABLED_STATE;
				mdb_log_add('I', "%s State -> ENABLED", name);
				return;
			}
			if (cmd.type == CMD_VEND_CANCEL && reader->machineState == VEND_STATE) {
				mdb_payload_tx[0] = (addr & BIT_ADD_SET) | (VEND & BIT_CMD_SET);
				mdb_payload_tx[1] = 0x01; // Vend Cancel

				write_payload_9(mdb_payload_tx, 2);
				mdb_log_add('T', "%s VEND Cancel", name);

				len = uart_read_bytes(UART_NUM_2, mdb_payload_rx, 1, pdMS_TO_TICKS(30));
				reader->machineState = IDLE_STATE;
				mdb_log_add('I', "%s State -> IDLE (cancel)", name);
				return;
			}
		}

		// Regular poll
		mdb_payload_tx[0] = (addr & BIT_ADD_SET) | (POLL & BIT_CMD_SET);
		write_payload_9(mdb_payload_tx, 1);

		len = uart_read_bytes(UART_NUM_2, mdb_payload_rx, 1, pdMS_TO_TICKS(30));
		if (len == 1) {
			reader->poll_fail_count = 0;

			// 0x00 = ACK (nothing to report) — fall through to command handling below

			if (mdb_payload_rx[0] == 0x07 /*End Session*/) {

				len += uart_read_bytes(UART_NUM_2, mdb_payload_rx + len, 1, pdMS_TO_TICKS(30)); // CHK*
				if (len != 2) return;

				write_9(ACK | BIT_MODE_SET);
				reader->machineState = ENABLED_STATE;
				mdb_log_add('R', "%s End Session", name);

			} else if (mdb_payload_rx[0] == 0x06 /*Vend Denied*/) {
				ESP_LOGI(TAG, "%s Vend Denied", name);
				mdb_log_add('R', "%s Vend DENIED", name);

				len += uart_read_bytes(UART_NUM_2, mdb_payload_rx + len, 1, pdMS_TO_TICKS(30)); // CHK*
				if (len != 2) return;

				write_9(ACK | BIT_MODE_SET);

				mdb_payload_tx[0] = (addr & BIT_ADD_SET) | (VEND & BIT_CMD_SET);
				mdb_payload_tx[1] = 0x04; // Session Complete

				write_payload_9(mdb_payload_tx, 2);
				mdb_log_add('T', "%s Session Complete", name);

				len = uart_read_bytes(UART_NUM_2, mdb_payload_rx, 1, pdMS_TO_TICKS(30)); // ACK*
				reader->machineState = IDLE_STATE;

			} else if (mdb_payload_rx[0] == 0x05 /*Vend Approved*/) {
				ESP_LOGI(TAG, "%s Vend Approved", name);

				len += uart_read_bytes(UART_NUM_2, mdb_payload_rx + len, 3, pdMS_TO_TICKS(30)); // CHK*
				if (len != 4) return;

				write_9(ACK | BIT_MODE_SET);

				uint16_t vendAmount = (mdb_payload_rx[1] << 8) | mdb_payload_rx[2];
				mdb_log_add('R', "%s Vend APPROVED amt=%d", name, vendAmount);

				// Use the item number from the last vend request command
				// (stored as the most recent CMD_VEND_REQUEST item_number)
				mdb_payload_tx[0] = (addr & BIT_ADD_SET) | (VEND & BIT_CMD_SET);
				mdb_payload_tx[1] = 0x02; // Vend Success
				mdb_payload_tx[2] = 0;
				mdb_payload_tx[3] = 0;

				write_payload_9(mdb_payload_tx, 4);
				mdb_log_add('T', "%s Vend Success", name);

				len = uart_read_bytes(UART_NUM_2, mdb_payload_rx, 1, pdMS_TO_TICKS(30)); // ACK*

				reader->machineState = IDLE_STATE;

				mdb_payload_tx[0] = (addr & BIT_ADD_SET) | (VEND & BIT_CMD_SET);
				mdb_payload_tx[1] = 0x04; // Session Complete

				write_payload_9(mdb_payload_tx, 2);
				mdb_log_add('T', "%s Session Complete", name);

				len = uart_read_bytes(UART_NUM_2, mdb_payload_rx, 1, pdMS_TO_TICKS(30)); // ACK*

			} else if (mdb_payload_rx[0] == 0x04 /*Session Cancel Request*/) {

				len += uart_read_bytes(UART_NUM_2, mdb_payload_rx + len, 1, pdMS_TO_TICKS(30)); // CHK*
				if (len != 2) return;

				write_9(ACK | BIT_MODE_SET);
				mdb_log_add('R', "%s Session Cancel Request", name);

				mdb_payload_tx[0] = (addr & BIT_ADD_SET) | (VEND & BIT_CMD_SET);
				mdb_payload_tx[1] = 0x04; // Session Complete

				write_payload_9(mdb_payload_tx, 2);
				mdb_log_add('T', "%s Session Complete", name);

				len = uart_read_bytes(UART_NUM_2, mdb_payload_rx, 1, pdMS_TO_TICKS(30)); // ACK*

			} else if (mdb_payload_rx[0] == 0x03 /*Begin Session*/) {

				ESP_LOGI(TAG, "%s Begin Session", name);

				len += uart_read_bytes(UART_NUM_2, mdb_payload_rx + len, 3, pdMS_TO_TICKS(30)); // CHK*
				if (len != 4) return;

				write_9(ACK | BIT_MODE_SET);

				uint16_t fundsAvailable = (mdb_payload_rx[1] << 8) | mdb_payload_rx[2];
				mdb_log_add('R', "%s Begin Session funds=%d", name, fundsAvailable);

				reader->machineState = IDLE_STATE;
				mdb_log_add('I', "%s State -> IDLE", name);

			} else if (mdb_payload_rx[0] == 0x0b /*Command Out of Sequence*/) {
				ESP_LOGI(TAG, "%s Command Out of Sequence", name);
				mdb_log_add('R', "%s Cmd Out of Sequence", name);

				len += uart_read_bytes(UART_NUM_2, mdb_payload_rx + len, 1, pdMS_TO_TICKS(30)); // CHK*
				if (len != 2) return;

				write_9(ACK | BIT_MODE_SET);

				reader->machineState = INACTIVE_STATE;
				mdb_log_add('I', "%s State -> INACTIVE", name);

			} else if (mdb_payload_rx[0] == 0x0a /*Malfunction/Error*/) {
				len += uart_read_bytes(UART_NUM_2, mdb_payload_rx + len, 2, pdMS_TO_TICKS(30)); // error_code + CHK
				if (len < 2) return;

				write_9(ACK | BIT_MODE_SET);

				uint8_t error_code = (len >= 3) ? mdb_payload_rx[1] : 0xFF;
				ESP_LOGW(TAG, "%s Malfunction/Error: 0x%02X", name, error_code);
				mdb_log_add('R', "%s ERROR code=0x%02X", name, error_code);

			} else if (mdb_payload_rx[0] == 0x08 /*Cancelled*/) {
				len += uart_read_bytes(UART_NUM_2, mdb_payload_rx + len, 1, pdMS_TO_TICKS(30)); // CHK
				if (len != 2) return;

				write_9(ACK | BIT_MODE_SET);
				mdb_log_add('R', "%s Cancelled", name);

			} else if (has_cmd && cmd.type == CMD_VEND_REQUEST) {

				uint16_t itemPrice = cmd.item_price;
				uint16_t itemNumber = cmd.item_number;

				// If price is 0, generate random price
				if (itemPrice == 0) {
					float randomPrice = (float)((esp_random() % 5) + 1) + (float)((esp_random() % 9) + 1) / 10.0f;
					itemPrice = TO_SCALE_FACTOR(randomPrice, reader->scaleFactor, reader->decimalPlaces);
				}

				if (reader->machineState == IDLE_STATE) {
					ESP_LOGI(TAG, "%s Vend request item=%d price=%d", name, itemNumber, itemPrice);

					mdb_payload_tx[0] = (addr & BIT_ADD_SET) | (VEND & BIT_CMD_SET);
					mdb_payload_tx[1] = 0x00; // Vend Request
					mdb_payload_tx[2] = itemPrice >> 8;
					mdb_payload_tx[3] = itemPrice;
					mdb_payload_tx[4] = itemNumber >> 8;
					mdb_payload_tx[5] = itemNumber;

					write_payload_9(mdb_payload_tx, 6);
					mdb_log_add('T', "%s VEND Req item=%d price=%d", name, itemNumber, itemPrice);

					len = uart_read_bytes(UART_NUM_2, mdb_payload_rx, 1, pdMS_TO_TICKS(30)); // ACK*

					reader->machineState = VEND_STATE;
					mdb_log_add('I', "%s State -> VEND", name);
				}

			} else if (has_cmd && cmd.type == CMD_CASH_SALE) {

				uint16_t itemPrice = cmd.item_price;
				uint16_t itemNumber = cmd.item_number;

				if (itemPrice == 0) {
					float randomPrice = (float)((esp_random() % 5) + 1) + (float)((esp_random() % 9) + 1) / 10.0f;
					itemPrice = TO_SCALE_FACTOR(randomPrice, reader->scaleFactor, reader->decimalPlaces);
				}

				if (reader->machineState == ENABLED_STATE || reader->machineState == IDLE_STATE) {
					ESP_LOGI(TAG, "%s Cash sale item=%d price=%d", name, itemNumber, itemPrice);

					mdb_payload_tx[0] = (addr & BIT_ADD_SET) | (VEND & BIT_CMD_SET);
					mdb_payload_tx[1] = 0x05; // Cash Sale
					mdb_payload_tx[2] = itemPrice >> 8;
					mdb_payload_tx[3] = itemPrice;
					mdb_payload_tx[4] = itemNumber >> 8;
					mdb_payload_tx[5] = itemNumber;

					write_payload_9(mdb_payload_tx, 6);
					mdb_log_add('T', "%s CASH Sale item=%d price=%d", name, itemNumber, itemPrice);

					len = uart_read_bytes(UART_NUM_2, mdb_payload_rx, 1, pdMS_TO_TICKS(30)); // ACK*
				}

			} else if (has_cmd && cmd.type == CMD_SESSION_COMPLETE) {

				if (reader->machineState == IDLE_STATE) {
					mdb_payload_tx[0] = (addr & BIT_ADD_SET) | (VEND & BIT_CMD_SET);
					mdb_payload_tx[1] = 0x04; // Session Complete

					write_payload_9(mdb_payload_tx, 2);
					mdb_log_add('T', "%s Session Complete", name);

					len = uart_read_bytes(UART_NUM_2, mdb_payload_rx, 1, pdMS_TO_TICKS(30)); // ACK*
				}
			}

		} else {
			if (++reader->poll_fail_count >= 3) {
				mdb_log_add('I', "%s Poll fail -> INACTIVE", name);
				reader->machineState = INACTIVE_STATE;
			}
		}
	}
}

void mdb_vmc_loop(void *pvParameters) {

	uint8_t mdb_payload_tx[36];
	uint8_t mdb_payload_rx[36];

	for (;;) {

		// Poll Cashless Device #1 (0x10)
		mdb_poll_cashless(0x10, &reader0x10, mdb_payload_tx, mdb_payload_rx);

		// Poll Cashless Device #2 (0x60)
		mdb_poll_cashless(0x60, &reader0x60, mdb_payload_tx, mdb_payload_rx);
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
	uart_set_pin( UART_NUM_1, pin_dex_tx, pin_dex_rx, -1, -1);
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
			sprintf(pa1, "PA1*%d*%d****\x0D\x0A", c, (uint16_t) TO_SCALE_FACTOR(coils[c].pa102, 1, 2) );

			uart_write_bytes(UART_NUM_1, &pa1, strlen(pa1));

			char pa2[100];
			sprintf(pa2, "PA2*%d*0*0*0\x0D\x0A", coils[c].pa201);

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

    //--------------- Strip LED configuration ---------------//
    led_strip_handle_t led_strip;
    led_strip_config_t strip_config = {
        .strip_gpio_num = pin_mdb_led,
        .max_leds = 1,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, // 10 MHz -> good precision
        .mem_block_symbols = 64,
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));

    //--------------- MDB ---------------//
	uart_config_t uart_config_2 = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_EVEN,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE };

	uart_param_config(UART_NUM_2, &uart_config_2);
	uart_set_pin( UART_NUM_2, pin_mdb_tx, pin_mdb_rx, -1, -1);
	uart_driver_install(UART_NUM_2, 256, 256, 0, (void*) 0, 0);

	// Pull-up on RX pin: keeps line HIGH when slave TX is high-Z (tri-state)
	// MDB bus idles HIGH; without pull-up the floating line causes framing errors
	gpio_pullup_en(pin_mdb_rx);

	//--------------- Command Queue ---------------//
	command_queue = xQueueCreate(4, sizeof(vmc_command_t));

	//--------------- Button ISR ---------------//
	gpio_config_t io_conf = {
			.intr_type = GPIO_INTR_NEGEDGE,
			.mode = GPIO_MODE_INPUT,
			.pin_bit_mask = (1ULL << GPIO_NUM_0),
			.pull_up_en = GPIO_PULLUP_ENABLE,
			.pull_down_en = GPIO_PULLDOWN_DISABLE, };

	gpio_config(&io_conf);

	gpio_install_isr_service(0 /*default*/);
	gpio_isr_handler_add(GPIO_NUM_0, button0_isr_handler, (void*) 0);

	//--------------- WiFi + WebUI ---------------//
	start_wifi();
	start_webui_server();

	//--------------- Tasks ---------------//
	xTaskCreate(mdb_vmc_loop, "vmc_loop", 6765, (void*) 0, 1, (void*) 0);

	xTaskCreate(eva_dts_loop, "dex_loop", 6765, (void*) 0, 1, (void*) 0);
}
