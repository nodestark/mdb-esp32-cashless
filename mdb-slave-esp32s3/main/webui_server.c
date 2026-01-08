#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include <esp_log.h>

#include "lwip/err.h"
#include "lwip/udp.h"
#include "lwip/ip_addr.h"

#include "esp_system.h"

#include "esp_chip_info.h"

#define TAG "webui"

#define AP_SSID  "VMflow"
#define AP_PASS  "12345678"

static httpd_handle_t rest_server = NULL;

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");

static struct udp_pcb *dns_pcb;

/* -----------     DNS     ---------- */

static void dns_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {

    if (!p) return;

    // resposta básica DNS (eco + IP do AP)
    uint8_t *data = (uint8_t *)p->payload;

    // QR = response
    data[2] |= 0x80;
    // RA = recursion available
    data[3] |= 0x80;
    // ANCOUNT = 1
    data[7] = 1;

    // append resposta A (IPv4)
    uint8_t response[] = {
        0xC0, 0x0C,     // pointer to domain name
        0x00, 0x01,     // type A
        0x00, 0x01,     // class IN
        0x00, 0x00, 0x00, 0x3C, // TTL
        0x00, 0x04,     // data length
        192, 168, 4, 1  // IP do ESP32 AP
    };

    struct pbuf *resp = pbuf_alloc(PBUF_TRANSPORT, p->len + sizeof(response), PBUF_RAM);

    memcpy(resp->payload, data, p->len);
    memcpy((uint8_t *)resp->payload + p->len, response, sizeof(response));

    udp_sendto(pcb, resp, addr, port);

    pbuf_free(resp);
    pbuf_free(p);
}

/* ---------- HTTP HANDLER ---------- */

static esp_err_t index_get_handler(httpd_req_t *req) {

    const size_t html_len = index_html_end - index_html_start;

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *) index_html_start, html_len);
    return ESP_OK;
}

static esp_err_t wifi_set_handler(httpd_req_t *req) {

    char buf[512];   // ajuste se necessário
    int total_len = req->content_len;
    int cur_len = 0;
    int received;

    if (total_len >= sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Payload too large");
        return ESP_FAIL;
    }

    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len - cur_len);
        if (received <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
            return ESP_FAIL;
        }
        cur_len += received;
    }

    buf[total_len] = '\0';  // garante string válida

    /* ---- PRINT DO JSON RECEBIDO ---- */
    ESP_LOGI(TAG, "JSON recebido: %s", buf);

    /* ---- Parse opcional (exemplo) ---- */
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
    cJSON *password = cJSON_GetObjectItem(root, "password");
    cJSON *mqtt = cJSON_GetObjectItem(root, "mqtt_server");

    if (ssid && password && mqtt) {
        ESP_LOGI(TAG, "SSID=%s", ssid->valuestring);
        ESP_LOGI(TAG, "PASSWORD=%s", password->valuestring);
        ESP_LOGI(TAG, "MQTT=%s", mqtt->valuestring);

        //
        wifi_config_t wifi_config = {0};
        esp_wifi_get_config(WIFI_IF_STA, &wifi_config);

        strncpy((char*) wifi_config.sta.ssid, ssid->valuestring, sizeof(wifi_config.sta.ssid) - 1);
        strncpy((char*) wifi_config.sta.password, password->valuestring, sizeof(wifi_config.sta.password) - 1);

        esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
        esp_wifi_connect();
        //

        nvs_handle_t handle;
        if (nvs_open("vmflow", NVS_READWRITE, &handle) == ESP_OK) {

            nvs_set_str(handle, "mqtt", mqtt->valuestring);
            nvs_commit(handle);

            nvs_close(handle);
            ESP_LOGI(TAG, "MQTT Server updated to: %s", buf);
        }
    }

    cJSON_Delete(root);

    return ESP_OK;
}

static esp_err_t system_info_get_handler(httpd_req_t *req) {

    char json[512] = {0};

    /* Chip info */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    /* Wi-Fi config */
    wifi_config_t wifi_cfg = {0};
    esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg);

    /* MQTT server (NVS) */
    char mqtt_server[32] = "";
    nvs_handle_t handle;
    size_t s_len = sizeof(mqtt_server);

    if (nvs_open("vmflow", NVS_READONLY, &handle) == ESP_OK) {
        nvs_get_str(handle, "mqtt", mqtt_server, &s_len);
        nvs_close(handle);
    }

    cJSON *root = cJSON_CreateObject();

     /* Populate JSON */
    cJSON_AddStringToObject(root, "version", IDF_VER);
    cJSON_AddNumberToObject(root, "cores", chip_info.cores);
    cJSON_AddNumberToObject(root, "model", chip_info.model);
    cJSON_AddStringToObject(root, "wifi_ssid", (char *)wifi_cfg.sta.ssid);
    cJSON_AddStringToObject(root, "wifi_password", (char *)wifi_cfg.sta.password);
    cJSON_AddStringToObject(root, "mqtt_server", mqtt_server);

    char *json_str = cJSON_PrintUnformatted(root);

    /* HTTP response */
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);

    cJSON_free(json_str);
    cJSON_Delete(root);

    return ESP_OK;
}

static esp_err_t generate_204_handler(httpd_req_t *req) {

    ESP_LOGI(TAG, "Captive portal redirect: %s", req->uri);

    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);

    return ESP_OK;
}

static void stop_dns_server(void) {

    if (dns_pcb == NULL) {
        ESP_LOGW(TAG, "DNS captive portal não está ativo");
        return;
    }

    /* Remove callback */
    udp_recv(dns_pcb, NULL, NULL);

    /* Fecha e libera o PCB */
    udp_remove(dns_pcb);
    dns_pcb = NULL;

    ESP_LOGI(TAG, "DNS captive portal parado");
}

static void start_dns_server(void) {

    dns_pcb = udp_new();
    udp_bind(dns_pcb, IP_ADDR_ANY, 53);
    udp_recv(dns_pcb, dns_recv, NULL);

    ESP_LOGI(TAG, "DNS captive portal iniciado");
}

static void stop_rest_server(void) {

    if (rest_server == NULL) {
        ESP_LOGW(TAG, "REST server not running");
        return;
    }
    httpd_stop(rest_server);

    rest_server = NULL;
}

static void start_rest_server(void) {

    if (rest_server != NULL) {
        ESP_LOGW(TAG, "REST server already running");
        return;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_start(&rest_server, &config);

    httpd_uri_t index = {
        .uri      = "/",
        .method   = HTTP_GET,
        .handler  = index_get_handler
    };
    httpd_register_uri_handler(rest_server, &index);

    httpd_uri_t system_info_get_uri = {
        .uri = "/api/v1/system/info",
        .method = HTTP_GET,
        .handler = system_info_get_handler
    };
    httpd_register_uri_handler(rest_server, &system_info_get_uri);

    httpd_uri_t settings_set_uri = {
        .uri = "/api/v1/settings/set",
        .method = HTTP_POST,
        .handler = wifi_set_handler,
    };
    httpd_register_uri_handler(rest_server, &settings_set_uri);

    httpd_uri_t generate_204 = {
        .uri = "/generate_204",
        .method = HTTP_GET,
        .handler = generate_204_handler
    };
    httpd_register_uri_handler(rest_server, &generate_204);
}

/* ---------- WIFI SOFTAP ---------- */
static void start_softap(void) {

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .password = AP_PASS,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
}