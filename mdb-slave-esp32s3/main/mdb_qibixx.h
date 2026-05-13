/*
 * VMflow.xyz
 *
 * mdb_qibixx.h - Qibixx MDB-USB compatible serial command layer
 *
 * Parses ASCII line commands over a UART port and dispatches them to a
 * pluggable MDB physical layer (typically the bit-bang 9-bit driver in
 * mdb-slave-esp32s3.c). Output replies are written back to the same UART.
 *
 * Supported subset of the Qibixx MDB-USB protocol:
 *   V                 -> version banner
 *   D,<hh>            -> set device mode (00=off,01=master,02=slave,03=sniffer)
 *   M,<hh>[,<hh>...]  -> master transmit raw MDB frame (driver appends CHK*)
 *   R,<hh>[,<hh>...]  -> queue peripheral reply data for next slave POLL
 *   K,<hh>            -> set cashless slave address (5-bit MDB address)
 *
 * Asynchronous output (sniffer / slave / master responses) is prefixed:
 *   v,<text>          -> version
 *   d,<status>        -> mode change ack (ACK/NAK)
 *   p,<hh>...         -> peripheral data (slave-direction bytes)
 *   m,<hh>...         -> master data (master-direction bytes, sniffer only)
 *   e,<text>          -> error
 */

#ifndef MDB_QIBIXX_H
#define MDB_QIBIXX_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <driver/uart.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MDB_QIBIXX_MODE_OFF     = 0x00,
    MDB_QIBIXX_MODE_MASTER  = 0x01,
    MDB_QIBIXX_MODE_SLAVE   = 0x02,
    MDB_QIBIXX_MODE_SNIFFER = 0x03,
} mdb_qibixx_mode_t;

/* Physical-layer callbacks supplied by the application.
 *
 * tx_byte_9: write one 9-bit MDB symbol (bit 8 == MDB mode bit).
 * rx_byte_9: read one 9-bit MDB symbol, blocking up to timeout_ms.
 *            Returns ESP_OK on success, ESP_ERR_TIMEOUT otherwise.
 */
typedef struct {
    void      (*tx_byte_9)(uint16_t nth9);
    esp_err_t (*rx_byte_9)(uint16_t *out, uint32_t timeout_ms);
} mdb_qibixx_phy_t;

typedef struct {
    uart_port_t       uart_num;        /* UART used for ASCII bridge */
    int               tx_pin;          /* -1 to keep current */
    int               rx_pin;          /* -1 to keep current */
    int               baudrate;        /* default 115200 */
    uint8_t           default_address; /* 5-bit cashless address */
    mdb_qibixx_phy_t  phy;
} mdb_qibixx_config_t;

esp_err_t mdb_qibixx_init(const mdb_qibixx_config_t *cfg);

mdb_qibixx_mode_t mdb_qibixx_get_mode(void);
uint8_t           mdb_qibixx_get_address(void);

/* Sniffer hooks: call from the bit-bang ISR / TX path when in sniffer mode.
 * The peripheral variant marks bytes received from the bus; the master
 * variant marks bytes we (or another master) emitted onto the bus. The
 * functions are no-ops outside sniffer mode and are ISR-safe.
 */
void mdb_qibixx_sniffer_on_rx(uint16_t nth9);
void mdb_qibixx_sniffer_on_tx(uint16_t nth9);

/* Slave reply queue: in MODE_SLAVE the cashless task asks this layer for
 * the next reply payload previously queued by an R,<hh>... command. The
 * checksum byte must NOT be included; the cashless transmitter adds it.
 *
 * Returns the number of bytes copied into buf (0 if no reply pending,
 * negative on overflow). The reply is consumed on success.
 */
int mdb_qibixx_slave_pop_reply(uint8_t *buf, size_t buf_len);

#ifdef __cplusplus
}
#endif

#endif /* MDB_QIBIXX_H */
