#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_http_server.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include <esp_log.h>
#include "lwip/udp.h"
#include "lwip/ip_addr.h"
#include "webui_server.h"

#define TAG "webui"

#define WIFI_MAX_RETRY 5

static httpd_handle_t rest_server = NULL;
static struct udp_pcb *dns_pcb = NULL;
static int wifi_retry_num = 0;
static bool ap_running = false;

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

/* -----------     DNS captive portal     ---------- */

static void dns_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {

    if (!p) return;

    uint8_t *data = (uint8_t *)p->payload;
    data[2] |= 0x80;  // QR = response
    data[3] |= 0x80;  // RA = recursion available
    data[7]  = 1;     // ANCOUNT = 1

    uint8_t response[] = {
        0xC0, 0x0C,             // pointer to domain name
        0x00, 0x01,             // type A
        0x00, 0x01,             // class IN
        0x00, 0x00, 0x00, 0x3C, // TTL 60s
        0x00, 0x04,             // data length
        192, 168, 4, 1          // AP IP
    };

    struct pbuf *resp = pbuf_alloc(PBUF_TRANSPORT, p->len + sizeof(response), PBUF_RAM);
    memcpy(resp->payload, data, p->len);
    memcpy((uint8_t *)resp->payload + p->len, response, sizeof(response));
    udp_sendto(pcb, resp, addr, port);
    pbuf_free(resp);
    pbuf_free(p);
}

/* ---------- Helper: read POST body ---------- */

static int read_post_body(httpd_req_t *req, char *buf, size_t buf_size) {
    int total_len = req->content_len;
    if (total_len >= (int)buf_size) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Payload too large");
        return -1;
    }
    int cur_len = 0, received;
    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len - cur_len);
        if (received <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
            return -1;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';
    return total_len;
}

/* ---------- Helper: get state name ---------- */

static const char *state_name(machine_state_t s) {
    switch (s) {
        case INACTIVE_STATE: return "INACTIVE";
        case DISABLED_STATE: return "DISABLED";
        case ENABLED_STATE:  return "ENABLED";
        case IDLE_STATE:     return "IDLE";
        case VEND_STATE:     return "VEND";
        default:             return "UNKNOWN";
    }
}

/* ---------- HTTP handlers ---------- */

static esp_err_t index_get_handler(httpd_req_t *req) {
    const size_t html_len = index_html_end - index_html_start;
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)index_html_start, html_len);
    return ESP_OK;
}

static esp_err_t state_get_handler(httpd_req_t *req) {

    // Get WiFi IP
    esp_netif_ip_info_t ip_info;
    char ip_str[16] = "0.0.0.0";
    esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif && esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
    } else {
        esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        if (ap_netif && esp_netif_get_ip_info(ap_netif, &ip_info) == ESP_OK) {
            snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
        }
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "wifi_ip", ip_str);

    // Reader 0x10 state
    cJSON *r10 = cJSON_AddObjectToObject(root, "reader_0x10");
    cJSON_AddStringToObject(r10, "state", state_name(reader0x10.machineState));
    cJSON_AddNumberToObject(r10, "feature_level", reader0x10.featureLevel);
    cJSON_AddNumberToObject(r10, "scale_factor", reader0x10.scaleFactor);
    cJSON_AddNumberToObject(r10, "decimal_places", reader0x10.decimalPlaces);
    cJSON_AddStringToObject(r10, "manufacturer", reader0x10.manufacturer);
    cJSON_AddStringToObject(r10, "serial", reader0x10.serial);
    cJSON_AddStringToObject(r10, "model", reader0x10.model);
    cJSON_AddStringToObject(r10, "sw_version", reader0x10.sw_version);

    // Reader 0x60 state
    cJSON *r60 = cJSON_AddObjectToObject(root, "reader_0x60");
    cJSON_AddStringToObject(r60, "state", state_name(reader0x60.machineState));
    cJSON_AddNumberToObject(r60, "feature_level", reader0x60.featureLevel);
    cJSON_AddNumberToObject(r60, "scale_factor", reader0x60.scaleFactor);
    cJSON_AddNumberToObject(r60, "decimal_places", reader0x60.decimalPlaces);
    cJSON_AddStringToObject(r60, "manufacturer", reader0x60.manufacturer);
    cJSON_AddStringToObject(r60, "serial", reader0x60.serial);
    cJSON_AddStringToObject(r60, "model", reader0x60.model);
    cJSON_AddStringToObject(r60, "sw_version", reader0x60.sw_version);

    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    cJSON_free(json_str);
    cJSON_Delete(root);

    return ESP_OK;
}

static esp_err_t vend_post_handler(httpd_req_t *req) {
    char buf[256];
    if (read_post_body(req, buf, sizeof(buf)) < 0) return ESP_FAIL;

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"Invalid JSON\"}");
        return ESP_OK;
    }

    cJSON *j_item = cJSON_GetObjectItem(root, "item_number");
    cJSON *j_price = cJSON_GetObjectItem(root, "item_price");
    cJSON *j_addr = cJSON_GetObjectItem(root, "address");

    vmc_command_t cmd = {
        .type = CMD_VEND_REQUEST,
        .target_addr = (j_addr && cJSON_IsNumber(j_addr)) ? (uint8_t)j_addr->valueint : 0,
        .item_number = (j_item && cJSON_IsNumber(j_item)) ? (uint16_t)j_item->valueint : 1,
        .item_price = (j_price && cJSON_IsNumber(j_price)) ? (uint16_t)j_price->valueint : 0,
    };

    cJSON_Delete(root);

    if (xQueueSend(command_queue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":true}");
    } else {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"Queue full\"}");
    }

    return ESP_OK;
}

static esp_err_t vend_cancel_post_handler(httpd_req_t *req) {
    char buf[128];
    if (req->content_len > 0) read_post_body(req, buf, sizeof(buf));

    cJSON *root = (req->content_len > 0) ? cJSON_Parse(buf) : NULL;
    cJSON *j_addr = root ? cJSON_GetObjectItem(root, "address") : NULL;

    vmc_command_t cmd = {
        .type = CMD_VEND_CANCEL,
        .target_addr = (j_addr && cJSON_IsNumber(j_addr)) ? (uint8_t)j_addr->valueint : 0,
    };

    if (root) cJSON_Delete(root);

    xQueueSend(command_queue, &cmd, pdMS_TO_TICKS(100));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t cashsale_post_handler(httpd_req_t *req) {
    char buf[256];
    if (read_post_body(req, buf, sizeof(buf)) < 0) return ESP_FAIL;

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"Invalid JSON\"}");
        return ESP_OK;
    }

    cJSON *j_item = cJSON_GetObjectItem(root, "item_number");
    cJSON *j_price = cJSON_GetObjectItem(root, "item_price");
    cJSON *j_addr = cJSON_GetObjectItem(root, "address");

    vmc_command_t cmd = {
        .type = CMD_CASH_SALE,
        .target_addr = (j_addr && cJSON_IsNumber(j_addr)) ? (uint8_t)j_addr->valueint : 0,
        .item_number = (j_item && cJSON_IsNumber(j_item)) ? (uint16_t)j_item->valueint : 1,
        .item_price = (j_price && cJSON_IsNumber(j_price)) ? (uint16_t)j_price->valueint : 0,
    };

    cJSON_Delete(root);

    xQueueSend(command_queue, &cmd, pdMS_TO_TICKS(100));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t reset_post_handler(httpd_req_t *req) {
    char buf[128];
    if (req->content_len > 0) read_post_body(req, buf, sizeof(buf));

    cJSON *root = (req->content_len > 0) ? cJSON_Parse(buf) : NULL;
    cJSON *j_addr = root ? cJSON_GetObjectItem(root, "address") : NULL;

    vmc_command_t cmd = {
        .type = CMD_RESET,
        .target_addr = (j_addr && cJSON_IsNumber(j_addr)) ? (uint8_t)j_addr->valueint : 0,
    };

    if (root) cJSON_Delete(root);

    xQueueSend(command_queue, &cmd, pdMS_TO_TICKS(100));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t reader_post_handler(httpd_req_t *req) {
    char buf[256];
    if (read_post_body(req, buf, sizeof(buf)) < 0) return ESP_FAIL;

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"Invalid JSON\"}");
        return ESP_OK;
    }

    cJSON *j_enabled = cJSON_GetObjectItem(root, "enabled");
    cJSON *j_addr = cJSON_GetObjectItem(root, "address");

    vmc_command_t cmd = {
        .type = (j_enabled && cJSON_IsTrue(j_enabled)) ? CMD_READER_ENABLE : CMD_READER_DISABLE,
        .target_addr = (j_addr && cJSON_IsNumber(j_addr)) ? (uint8_t)j_addr->valueint : 0,
    };

    cJSON_Delete(root);

    xQueueSend(command_queue, &cmd, pdMS_TO_TICKS(100));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t log_get_handler(httpd_req_t *req) {

    int count = mdb_log_count;
    int head = mdb_log_head;
    int start = (count < MDB_LOG_SIZE) ? 0 : (head % MDB_LOG_SIZE);

    cJSON *root = cJSON_CreateArray();

    for (int i = 0; i < count; i++) {
        int idx = (start + i) % MDB_LOG_SIZE;
        mdb_log_entry_t *e = &mdb_log[idx];

        cJSON *entry = cJSON_CreateObject();
        cJSON_AddNumberToObject(entry, "ts", e->timestamp_ms);
        char dir[2] = { e->direction, '\0' };
        cJSON_AddStringToObject(entry, "dir", dir);
        cJSON_AddStringToObject(entry, "msg", e->description);
        cJSON_AddItemToArray(root, entry);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    cJSON_free(json_str);
    cJSON_Delete(root);

    return ESP_OK;
}

static esp_err_t products_get_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateArray();

    for (int i = 0; i < MAX_PRODUCTS; i++) {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddNumberToObject(p, "slot", products[i].slot);
        cJSON_AddStringToObject(p, "name", products[i].name);
        cJSON_AddNumberToObject(p, "price", products[i].price * 100); // cents
        cJSON_AddItemToArray(root, p);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    cJSON_free(json_str);
    cJSON_Delete(root);

    return ESP_OK;
}

static esp_err_t wifi_scan_get_handler(httpd_req_t *req) {
    wifi_scan_config_t scan_cfg = {
        .ssid        = NULL,
        .bssid       = NULL,
        .channel     = 0,
        .show_hidden = false,
        .scan_type   = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time   = { .active = { .min = 100, .max = 300 } },
    };

    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
    if (err != ESP_OK) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"networks\":[]}");
        return ESP_OK;
    }

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count > 20) ap_count = 20;

    wifi_ap_record_t *ap_records = calloc(ap_count, sizeof(wifi_ap_record_t));
    if (!ap_records) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"networks\":[]}");
        return ESP_OK;
    }
    esp_wifi_scan_get_ap_records(&ap_count, ap_records);

    cJSON *root = cJSON_CreateObject();
    cJSON *networks = cJSON_AddArrayToObject(root, "networks");

    for (int i = 0; i < ap_count; i++) {
        const char *ssid = (const char *)ap_records[i].ssid;
        if (strlen(ssid) == 0) continue;

        cJSON *net = cJSON_CreateObject();
        cJSON_AddStringToObject(net, "ssid", ssid);
        cJSON_AddNumberToObject(net, "rssi", ap_records[i].rssi);
        cJSON_AddBoolToObject(net, "secure", ap_records[i].authmode != WIFI_AUTH_OPEN);
        cJSON_AddItemToArray(networks, net);
    }

    free(ap_records);

    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    cJSON_free(json_str);
    cJSON_Delete(root);

    return ESP_OK;
}

static esp_err_t wifi_set_handler(httpd_req_t *req) {
    char buf[512];
    if (read_post_body(req, buf, sizeof(buf)) < 0) return ESP_FAIL;

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"Invalid JSON\"}");
        return ESP_OK;
    }

    cJSON *j_ssid = cJSON_GetObjectItem(root, "ssid");
    cJSON *j_password = cJSON_GetObjectItem(root, "password");

    if (!j_ssid || !cJSON_IsString(j_ssid) || strlen(j_ssid->valuestring) == 0) {
        cJSON_Delete(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"ssid is required\"}");
        return ESP_OK;
    }

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, j_ssid->valuestring, sizeof(wifi_config.sta.ssid) - 1);
    if (j_password && cJSON_IsString(j_password)) {
        strncpy((char *)wifi_config.sta.password, j_password->valuestring, sizeof(wifi_config.sta.password) - 1);
    }
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);

    cJSON_Delete(root);

    esp_wifi_connect();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t captive_handler(httpd_req_t *req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* ---------- DNS ---------- */

void stop_dns_server(void) {
    if (dns_pcb == NULL) return;
    udp_recv(dns_pcb, NULL, NULL);
    udp_remove(dns_pcb);
    dns_pcb = NULL;
    ESP_LOGI(TAG, "DNS stopped");
}

void start_dns_server(void) {
    if (dns_pcb) return;
    dns_pcb = udp_new();
    udp_bind(dns_pcb, IP_ADDR_ANY, 53);
    udp_recv(dns_pcb, dns_recv, NULL);
    ESP_LOGI(TAG, "DNS started");
}

/* ---------- HTTP server ---------- */

void stop_webui_server(void) {
    if (rest_server == NULL) return;
    httpd_stop(rest_server);
    rest_server = NULL;
}

void start_webui_server(void) {
    if (rest_server != NULL) return;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    config.stack_size = 8192;

    if (httpd_start(&rest_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }

    static const httpd_uri_t uris[] = {
        { .uri = "/",                .method = HTTP_GET,  .handler = index_get_handler        },
        { .uri = "/api/state",       .method = HTTP_GET,  .handler = state_get_handler         },
        { .uri = "/api/vend",        .method = HTTP_POST, .handler = vend_post_handler          },
        { .uri = "/api/vend-cancel", .method = HTTP_POST, .handler = vend_cancel_post_handler   },
        { .uri = "/api/cashsale",    .method = HTTP_POST, .handler = cashsale_post_handler      },
        { .uri = "/api/reset",       .method = HTTP_POST, .handler = reset_post_handler         },
        { .uri = "/api/reader",      .method = HTTP_POST, .handler = reader_post_handler        },
        { .uri = "/api/log",         .method = HTTP_GET,  .handler = log_get_handler            },
        { .uri = "/api/products",    .method = HTTP_GET,  .handler = products_get_handler       },
        { .uri = "/api/wifi/scan",   .method = HTTP_GET,  .handler = wifi_scan_get_handler      },
        { .uri = "/api/wifi/set",    .method = HTTP_POST, .handler = wifi_set_handler           },
        { .uri = "/generate_204",    .method = HTTP_GET,  .handler = captive_handler            },
        { .uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = captive_handler         },
    };

    for (int i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(rest_server, &uris[i]);
    }

    ESP_LOGI(TAG, "HTTP server started");
}

/* ---------- SoftAP ---------- */

static void start_softap(void) {
    if (ap_running) return;

    wifi_config_t wifi_config = {
        .ap = {
            .ssid           = CONFIG_AP_SSID,
            .ssid_len       = strlen(CONFIG_AP_SSID),
            .password       = CONFIG_AP_PASSWORD,
            .max_connection = 4,
            .authmode       = WIFI_AUTH_WPA_WPA2_PSK,
        },
    };
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    ap_running = true;
    ESP_LOGI(TAG, "SoftAP configured: SSID=\"%s\"", CONFIG_AP_SSID);
}

/* ---------- WiFi event handler ---------- */

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START: {
            wifi_retry_num = 0;
            ESP_LOGI(TAG, "WiFi STA started");

            wifi_config_t sta_cfg = {0};
            esp_wifi_get_config(WIFI_IF_STA, &sta_cfg);

            if (strlen((char *)sta_cfg.sta.ssid) == 0) {
                ESP_LOGI(TAG, "No saved WiFi — SoftAP only");
                start_softap();
                start_dns_server();
            } else {
                esp_err_t conn_err = esp_wifi_connect();
                if (conn_err != ESP_OK) {
                    ESP_LOGW(TAG, "Connect failed — starting SoftAP");
                    start_softap();
                    start_dns_server();
                }
            }
            break;
        }
        case WIFI_EVENT_AP_START:
            ESP_LOGI(TAG, "AP started — SSID \"%s\"", CONFIG_AP_SSID);
            break;
        case WIFI_EVENT_AP_STACONNECTED:
            ESP_LOGI(TAG, "Client connected to SoftAP");
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            if (wifi_retry_num++ < WIFI_MAX_RETRY) {
                esp_wifi_connect();
            } else {
                start_softap();
                start_dns_server();
            }
            break;
        }
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event_ip = (ip_event_got_ip_t *)event_data;
        ESP_LOGW(TAG, "GOT IP: " IPSTR, IP2STR(&event_ip->ip_info.ip));

        wifi_retry_num = 0;

        // Keep AP running so the WebUI stays accessible from both networks
        stop_dns_server();
    }
}

/* ---------- WiFi init ---------- */

void start_wifi(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    // Set STA config from Kconfig if provided
#ifdef CONFIG_WIFI_SSID
    if (strlen(CONFIG_WIFI_SSID) > 0) {
        wifi_config_t sta_config = {0};
        strncpy((char *)sta_config.sta.ssid, CONFIG_WIFI_SSID, sizeof(sta_config.sta.ssid) - 1);
#ifdef CONFIG_WIFI_PASSWORD
        strncpy((char *)sta_config.sta.password, CONFIG_WIFI_PASSWORD, sizeof(sta_config.sta.password) - 1);
#endif
        esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    }
#endif

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi initialized (APSTA mode)");
}
