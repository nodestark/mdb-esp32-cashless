/* HTTP Restful API Server

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <fcntl.h>

#include <nvs_flash.h>
#include "esp_http_server.h"
#include "esp_chip_info.h"
#include "esp_random.h"
#include "esp_log.h"
#include "esp_vfs.h"
#include "cJSON.h"
#include <esp_wifi.h>

static const char *REST_TAG = "esp-rest";
#define REST_CHECK(a, str, goto_tag, ...)                                              \
    do                                                                                 \
    {                                                                                  \
        if (!(a))                                                                      \
        {                                                                              \
            ESP_LOGE(REST_TAG, "%s(%d): " str, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
            goto goto_tag;                                                             \
        }                                                                              \
    } while (0)

#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + 128)
#define SCRATCH_BUFSIZE (10240)

typedef struct rest_server_context {
    char base_path[ESP_VFS_PATH_MAX + 1];
    char scratch[SCRATCH_BUFSIZE];
} rest_server_context_t;

#define CHECK_FILE_EXTENSION(filename, ext) (strcasecmp(&filename[strlen(filename) - strlen(ext)], ext) == 0)

/* Set HTTP response content type according to file extension */
static esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filepath)
{
    const char *type = "text/plain";
    if (CHECK_FILE_EXTENSION(filepath, ".html")) {
        type = "text/html";
    } else if (CHECK_FILE_EXTENSION(filepath, ".js")) {
        type = "application/javascript";
    } else if (CHECK_FILE_EXTENSION(filepath, ".css")) {
        type = "text/css";
    } else if (CHECK_FILE_EXTENSION(filepath, ".png")) {
        type = "image/png";
    } else if (CHECK_FILE_EXTENSION(filepath, ".ico")) {
        type = "image/x-icon";
    } else if (CHECK_FILE_EXTENSION(filepath, ".svg")) {
        type = "text/xml";
    }
    return httpd_resp_set_type(req, type);
}

/* Send HTTP response with the contents of the requested file */
static esp_err_t rest_common_get_handler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];

    rest_server_context_t *rest_context = (rest_server_context_t *)req->user_ctx;
    strlcpy(filepath, rest_context->base_path, sizeof(filepath));
    if (req->uri[strlen(req->uri) - 1] == '/') {
        strlcat(filepath, "/index.html", sizeof(filepath));
    } else {
        strlcat(filepath, req->uri, sizeof(filepath));
    }
    int fd = open(filepath, O_RDONLY, 0);
    if (fd == -1) {
        ESP_LOGE(REST_TAG, "Failed to open file : %s", filepath);
        char host[64];
        if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) == ESP_OK) {
            // Wenn der Host nicht 192.168.4.1 ist, kommt die Anfrage von einer
            // Erkennungs-URL (wie google.com).
            if (strcmp(host, "192.168.4.1") != 0) {
                ESP_LOGI("HTTP", "Fremder Host [%s] erkannt -> Umleitung auf Captive Portal", host);
                httpd_resp_set_status(req, "302 Found");
                httpd_resp_set_hdr(req, "Location", "/");
                httpd_resp_send(req, NULL, 0);
                return ESP_OK;
            }
        }
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
        return ESP_FAIL;
    }

    set_content_type_from_file(req, filepath);

    char *chunk = rest_context->scratch;
    ssize_t read_bytes;
    do {
        /* Read file in chunks into the scratch buffer */
        read_bytes = read(fd, chunk, SCRATCH_BUFSIZE);
        if (read_bytes == -1) {
            ESP_LOGE(REST_TAG, "Failed to read file : %s", filepath);
        } else if (read_bytes > 0) {
            /* Send the buffer contents as HTTP response chunk */
            if (httpd_resp_send_chunk(req, chunk, read_bytes) != ESP_OK) {
                close(fd);
                ESP_LOGE(REST_TAG, "File sending failed!");
                /* Abort sending file */
                httpd_resp_sendstr_chunk(req, NULL);
                /* Respond with 500 Internal Server Error */
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                return ESP_FAIL;
            }
        }
    } while (read_bytes > 0);
    /* Close file after sending complete */
    close(fd);
    ESP_LOGI(REST_TAG, "File sending complete");
    /* Respond with an empty chunk to signal HTTP response completion */
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/index.html");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* Simple handler for getting system handler */
static esp_err_t system_info_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    wifi_config_t wifi_cfg;
    esp_wifi_get_config(ESP_IF_WIFI_STA, &wifi_cfg);

    nvs_handle_t handle;
    size_t s_len = 0;
    char mqtt_server[32];
    if (nvs_open("vmflow", NVS_READONLY, &handle) == ESP_OK) {
        if (nvs_get_str(handle, "mqtt_server", (void*) 0, &s_len) == ESP_OK) {
            nvs_get_str(handle, "mqtt_server", mqtt_server, &s_len);
        }
    }

    cJSON_AddStringToObject(root, "version", IDF_VER);
    cJSON_AddNumberToObject(root, "cores", chip_info.cores);
    cJSON_AddNumberToObject(root, "model", chip_info.model);
    cJSON_AddStringToObject(root, "wifi_ssid", (char*)wifi_cfg.sta.ssid);
    cJSON_AddStringToObject(root, "wifi_password", (char*)wifi_cfg.sta.password);
    cJSON_AddStringToObject(root, "mqtt_server", mqtt_server);
    const char *sys_info = cJSON_Print(root);
    httpd_resp_sendstr(req, sys_info);
    free((void *)sys_info);
    cJSON_Delete(root);
    return ESP_OK;
}

/* Simplified handler to set WiFi credentials */
static esp_err_t wifi_set_handler(httpd_req_t *req)
{
    char *buf = ((rest_server_context_t *)(req->user_ctx))->scratch;
    int ret = httpd_req_recv(req, buf, req->content_len);
    if (ret <= 0) {
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    const char *ssid = cJSON_GetObjectItem(root, "ssid")->valuestring;
    const char *pass = cJSON_GetObjectItem(root, "password")->valuestring;
    const char *mqtt_server = cJSON_GetObjectItem(root, "mqtt_server")->valuestring;

    ESP_LOGI(REST_TAG, "SSID=%s, PASS=%s", ssid, pass);

    if (ssid && pass && mqtt_server) {
        esp_wifi_disconnect();
        esp_wifi_set_mode(WIFI_MODE_STA);

        wifi_config_t wifi_config = {0};
        esp_wifi_set_storage(WIFI_STORAGE_FLASH);
        esp_wifi_get_config(ESP_IF_WIFI_STA, &wifi_config);

        strcpy((char*) wifi_config.sta.ssid, ssid);
        strcpy((char*) wifi_config.sta.password, pass);

        esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
        nvs_handle_t handle;
        if (nvs_open("vmflow", NVS_READWRITE, &handle) == ESP_OK) {
            nvs_set_str(handle, "mqtt_server", mqtt_server);
            nvs_commit(handle);
            nvs_close(handle);

            ESP_LOGI(REST_TAG, "MQTT Server updated to: %s", buf);
        }
        if (err == ESP_OK) {
            ESP_LOGI(REST_TAG, "Credentials saved to NVS. SSID: %s", ssid);
            httpd_resp_sendstr(req, "OK");

            // Verification log
            wifi_config_t verify_cfg;
            esp_wifi_get_config(WIFI_IF_STA, &verify_cfg);
            ESP_LOGI(REST_TAG, "Verified NVS - SSID: %s", (char*)verify_cfg.sta.ssid);
        } else {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save config");
        }

    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing fields");
    }

    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t start_rest_server(const char *base_path)
{
    REST_CHECK(base_path, "wrong base path", err);
    rest_server_context_t *rest_context = calloc(1, sizeof(rest_server_context_t));
    REST_CHECK(rest_context, "No memory for rest context", err);
    strlcpy(rest_context->base_path, base_path, sizeof(rest_context->base_path));

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_LOGI(REST_TAG, "Starting HTTP Server");
    REST_CHECK(httpd_start(&server, &config) == ESP_OK, "Start server failed", err_start);

    /* URI handler for fetching system info */
    httpd_uri_t system_info_get_uri = {
        .uri = "/api/v1/system/info",
        .method = HTTP_GET,
        .handler = system_info_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &system_info_get_uri);

    httpd_uri_t settings_set_uri = {
        .uri = "/api/v1/settings/set",
        .method = HTTP_POST,
        .handler = wifi_set_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &settings_set_uri);

    /* URI handler for getting web server files */
    httpd_uri_t common_get_uri = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = rest_common_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &common_get_uri);

    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler);

    return ESP_OK;
err_start:
    free(rest_context);
err:
    return ESP_FAIL;
}
