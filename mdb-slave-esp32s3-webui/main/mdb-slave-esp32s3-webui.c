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

#include "esp_system.h"

#include "esp_chip_info.h"

#define TAG "mdb-target"

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");

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
        nvs_get_str(handle, "mqtt_server", mqtt_server, &s_len);
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

static void start_webserver(void) {

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    httpd_start(&server, &config);

    httpd_uri_t index = {
        .uri      = "/",
        .method   = HTTP_GET,
        .handler  = index_get_handler
    };
    httpd_register_uri_handler(server, &index);

    httpd_uri_t system_info_get_uri = {
        .uri = "/api/v1/system/info",
        .method = HTTP_GET,
        .handler = system_info_get_handler
    };
    httpd_register_uri_handler(server, &system_info_get_uri);

    httpd_uri_t settings_set_uri = {
        .uri = "/api/v1/settings/set",
        .method = HTTP_POST,
        .handler = wifi_set_handler,
    };
    httpd_register_uri_handler(server, &settings_set_uri);
}

/* ---------- WIFI SOFTAP ---------- */
static void wifi_init_softap(void) {

    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "ESP32-HELLO",
            .ssid_len = strlen("ESP32-HELLO"),
            .password = "12345678",
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    esp_wifi_start();
}

/* ---------- MAIN ---------- */

void app_main(void) {

    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();

    wifi_init_softap();
    start_webserver();
}