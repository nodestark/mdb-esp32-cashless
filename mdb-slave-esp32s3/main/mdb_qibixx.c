/*
 * VMflow.xyz
 *
 * mdb_qibixx.c - Qibixx MDB-USB compatible serial command layer.
 *
 * See mdb_qibixx.h for protocol details.
 */

#include "mdb_qibixx.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <esp_log.h>

#define TAG "mdb_qibixx"

#define QX_LINE_MAX     128
#define QX_FRAME_MAX    36          /* MDB max payload */
#define QX_VERSION_STR  "VMflow Qibixx-compat 0.1"

#define QX_BIT_MODE     0x100u
#define QX_ADDR_MASK    0xF8u
#define QX_ACK          0x00u
#define QX_NAK          0xFFu
#define QX_RET          0xAAu

/* ----- internal state -------------------------------------------------- */

static mdb_qibixx_mode_t s_mode = MDB_QIBIXX_MODE_OFF;
static uint8_t           s_address = 0x10;
static uart_port_t       s_uart = UART_NUM_0;
static mdb_qibixx_phy_t  s_phy;

static SemaphoreHandle_t s_tx_mtx;   /* serialises UART writes */
static SemaphoreHandle_t s_slave_mtx;

static uint8_t  s_slave_reply[QX_FRAME_MAX];
static size_t   s_slave_reply_len;
static bool     s_slave_reply_pending;

/* ----- low-level UART helpers ----------------------------------------- */

static void qx_lock_tx(void)   { xSemaphoreTake(s_tx_mtx, portMAX_DELAY); }
static void qx_unlock_tx(void) { xSemaphoreGive(s_tx_mtx); }

static void qx_uart_write(const char *buf, size_t len)
{
    uart_write_bytes(s_uart, buf, len);
}

static void qx_printf(const char *fmt, ...)
{
    char buf[QX_LINE_MAX];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t) n >= sizeof(buf)) n = sizeof(buf) - 1;
    qx_lock_tx();
    qx_uart_write(buf, (size_t) n);
    qx_unlock_tx();
}

static void qx_emit_bytes(char prefix, const uint8_t *data, size_t len)
{
    char line[QX_LINE_MAX];
    size_t pos = 0;
    line[pos++] = prefix;
    for (size_t i = 0; i < len && pos + 4 < sizeof(line); i++) {
        pos += snprintf(line + pos, sizeof(line) - pos, ",%02x", data[i]);
    }
    if (pos + 1 < sizeof(line)) line[pos++] = '\n';
    qx_lock_tx();
    qx_uart_write(line, pos);
    qx_unlock_tx();
}

static void qx_emit_error(const char *msg)
{
    qx_printf("e,%s\n", msg);
}

/* ----- hex parsing ----------------------------------------------------- */

static int qx_hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Parse a sequence of comma-separated hex bytes starting at *p.
 * Accepts forms like "10", "10,01", "1001" (paired digits). Returns the
 * number of bytes parsed, or -1 on malformed input. */
static int qx_parse_hex_bytes(const char *p, uint8_t *out, size_t out_max)
{
    size_t count = 0;
    while (*p && count < out_max) {
        while (*p == ',' || *p == ' ' || *p == '\t') p++;
        if (!*p) break;
        int hi = qx_hex_nibble(*p++);
        if (hi < 0) return -1;
        int lo = qx_hex_nibble(*p++);
        if (lo < 0) return -1;
        out[count++] = (uint8_t) ((hi << 4) | lo);
    }
    return (int) count;
}

/* ----- master mode ----------------------------------------------------- */

static void qx_cmd_master(const char *args)
{
    if (s_mode != MDB_QIBIXX_MODE_MASTER) {
        qx_emit_error("not in master mode");
        return;
    }
    if (!s_phy.tx_byte_9 || !s_phy.rx_byte_9) {
        qx_emit_error("phy not configured");
        return;
    }

    uint8_t frame[QX_FRAME_MAX];
    int n = qx_parse_hex_bytes(args, frame, sizeof(frame));
    if (n <= 0) {
        qx_emit_error("bad hex");
        return;
    }

    /* First byte carries the MDB mode bit (ADDRESS + CMD). Remaining bytes
     * are data. Driver computes and appends the checksum byte. */
    uint8_t checksum = 0;
    s_phy.tx_byte_9(QX_BIT_MODE | frame[0]);
    checksum += frame[0];
    for (int i = 1; i < n; i++) {
        s_phy.tx_byte_9(frame[i]);
        checksum += frame[i];
    }
    s_phy.tx_byte_9(checksum);

    /* Collect peripheral reply until either a single ACK/NAK/RET (with
     * mode bit) or a data stream terminated by CHK (no mode bit). */
    uint8_t reply[QX_FRAME_MAX];
    size_t  reply_len = 0;
    uint8_t reply_sum = 0;
    bool    got_response = false;

    for (;;) {
        uint16_t word;
        esp_err_t err = s_phy.rx_byte_9(&word, 50);
        if (err != ESP_OK) break;

        got_response = true;
        if (word & QX_BIT_MODE) {
            uint8_t v = (uint8_t) word;
            if (reply_len == 0) {
                /* Bare ACK/NAK/RET response. */
                if (v == QX_ACK) { qx_printf("p,ACK\n");  return; }
                if (v == QX_NAK) { qx_printf("p,NAK\n");  return; }
                if (v == QX_RET) { qx_printf("p,RET\n");  return; }
            }
            /* CHK* terminates a data stream. */
            if (v == (uint8_t) reply_sum) {
                qx_emit_bytes('p', reply, reply_len);
                /* Acknowledge to peripheral. */
                s_phy.tx_byte_9(QX_BIT_MODE | QX_ACK);
            } else {
                qx_emit_error("checksum");
                s_phy.tx_byte_9(QX_BIT_MODE | QX_NAK);
            }
            return;
        }

        if (reply_len < sizeof(reply)) {
            reply[reply_len++] = (uint8_t) word;
            reply_sum += (uint8_t) word;
        } else {
            qx_emit_error("overflow");
            return;
        }
    }

    if (!got_response) qx_printf("p,TIMEOUT\n");
}

/* ----- slave-reply queue ---------------------------------------------- */

static void qx_cmd_slave_reply(const char *args)
{
    if (s_mode != MDB_QIBIXX_MODE_SLAVE) {
        qx_emit_error("not in slave mode");
        return;
    }
    uint8_t frame[QX_FRAME_MAX];
    int n = qx_parse_hex_bytes(args, frame, sizeof(frame));
    if (n < 0) {
        qx_emit_error("bad hex");
        return;
    }
    xSemaphoreTake(s_slave_mtx, portMAX_DELAY);
    memcpy(s_slave_reply, frame, (size_t) n);
    s_slave_reply_len = (size_t) n;
    s_slave_reply_pending = true;
    xSemaphoreGive(s_slave_mtx);
    qx_printf("r,ACK\n");
}

int mdb_qibixx_slave_pop_reply(uint8_t *buf, size_t buf_len)
{
    if (!buf) return 0;
    int ret = 0;
    xSemaphoreTake(s_slave_mtx, portMAX_DELAY);
    if (s_slave_reply_pending) {
        if (s_slave_reply_len > buf_len) {
            ret = -1;
        } else {
            memcpy(buf, s_slave_reply, s_slave_reply_len);
            ret = (int) s_slave_reply_len;
            s_slave_reply_pending = false;
            s_slave_reply_len = 0;
        }
    }
    xSemaphoreGive(s_slave_mtx);
    return ret;
}

/* ----- mode / address commands ---------------------------------------- */

static void qx_cmd_set_mode(const char *args)
{
    uint8_t v;
    if (qx_parse_hex_bytes(args, &v, 1) != 1 || v > MDB_QIBIXX_MODE_SNIFFER) {
        qx_emit_error("bad mode");
        return;
    }
    s_mode = (mdb_qibixx_mode_t) v;
    qx_printf("d,ACK,%02x\n", v);
    ESP_LOGI(TAG, "mode -> %u", v);
}

static void qx_cmd_set_address(const char *args)
{
    uint8_t v;
    if (qx_parse_hex_bytes(args, &v, 1) != 1) {
        qx_emit_error("bad addr");
        return;
    }
    s_address = v & QX_ADDR_MASK;
    qx_printf("k,ACK,%02x\n", s_address);
}

/* ----- line dispatcher ------------------------------------------------ */

static void qx_dispatch(char *line)
{
    /* Trim trailing CR/LF/space. */
    size_t len = strlen(line);
    while (len && (line[len - 1] == '\r' || line[len - 1] == '\n' ||
                   line[len - 1] == ' ' || line[len - 1] == '\t')) {
        line[--len] = '\0';
    }
    if (len == 0) return;

    char cmd = (char) toupper((unsigned char) line[0]);
    const char *args = line + 1;
    if (*args == ',') args++;

    switch (cmd) {
    case 'V':
        qx_printf("v,%s\n", QX_VERSION_STR);
        break;
    case 'D':
        qx_cmd_set_mode(args);
        break;
    case 'M':
        qx_cmd_master(args);
        break;
    case 'R':
        qx_cmd_slave_reply(args);
        break;
    case 'K':
        qx_cmd_set_address(args);
        break;
    default:
        qx_emit_error("unknown");
        break;
    }
}

/* ----- parser task ---------------------------------------------------- */

static void qx_parser_task(void *arg)
{
    (void) arg;
    char    line[QX_LINE_MAX];
    size_t  pos = 0;

    for (;;) {
        uint8_t c;
        int n = uart_read_bytes(s_uart, &c, 1, pdMS_TO_TICKS(200));
        if (n != 1) continue;

        if (c == '\n' || c == '\r') {
            if (pos == 0) continue;
            line[pos] = '\0';
            qx_dispatch(line);
            pos = 0;
            continue;
        }
        if (pos + 1 >= sizeof(line)) {
            pos = 0;     /* drop overflowing line */
            qx_emit_error("line too long");
            continue;
        }
        line[pos++] = (char) c;
    }
}

/* ----- sniffer hooks -------------------------------------------------- */

void mdb_qibixx_sniffer_on_rx(uint16_t nth9)
{
    if (s_mode != MDB_QIBIXX_MODE_SNIFFER) return;
    uint8_t b = (uint8_t) nth9;
    /* Peripheral-direction bytes carry no mode bit; mode-bit bytes are
     * either VMC address frames or ACK/NAK from VMC. We tag the direction
     * heuristically from the mode bit. */
    qx_emit_bytes((nth9 & QX_BIT_MODE) ? 'm' : 'p', &b, 1);
}

void mdb_qibixx_sniffer_on_tx(uint16_t nth9)
{
    if (s_mode != MDB_QIBIXX_MODE_SNIFFER) return;
    uint8_t b = (uint8_t) nth9;
    qx_emit_bytes('p', &b, 1);
}

/* ----- public API ----------------------------------------------------- */

mdb_qibixx_mode_t mdb_qibixx_get_mode(void)    { return s_mode; }
uint8_t           mdb_qibixx_get_address(void) { return s_address; }

esp_err_t mdb_qibixx_init(const mdb_qibixx_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;

    s_uart    = cfg->uart_num;
    s_phy     = cfg->phy;
    s_address = cfg->default_address & QX_ADDR_MASK;

    s_tx_mtx    = xSemaphoreCreateMutex();
    s_slave_mtx = xSemaphoreCreateMutex();
    if (!s_tx_mtx || !s_slave_mtx) return ESP_ERR_NO_MEM;

    uart_config_t uc = {
        .baud_rate = cfg->baudrate ? cfg->baudrate : 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_param_config(s_uart, &uc));
    if (cfg->tx_pin >= 0 || cfg->rx_pin >= 0) {
        ESP_ERROR_CHECK(uart_set_pin(s_uart,
                                     cfg->tx_pin >= 0 ? cfg->tx_pin : UART_PIN_NO_CHANGE,
                                     cfg->rx_pin >= 0 ? cfg->rx_pin : UART_PIN_NO_CHANGE,
                                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    }
    if (!uart_is_driver_installed(s_uart)) {
        ESP_ERROR_CHECK(uart_driver_install(s_uart, 512, 512, 0, NULL, 0));
    }

    BaseType_t ok = xTaskCreate(qx_parser_task, "qibixx", 4096, NULL, 5, NULL);
    if (ok != pdPASS) return ESP_ERR_NO_MEM;

    qx_printf("v,%s\n", QX_VERSION_STR);
    return ESP_OK;
}
