#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include <esp_log.h>
#include "lwip/udp.h"
#include "lwip/ip_addr.h"
#include "esp_chip_info.h"
#include "webui_server.h"

#define TAG "webui"


static httpd_handle_t rest_server = NULL;

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");
static struct udp_pcb *dns_pcb;

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

/* ---------- HTTP handlers ---------- */

static esp_err_t index_get_handler(httpd_req_t *req) {
    const size_t html_len = index_html_end - index_html_start;
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)index_html_start, html_len);
    return ESP_OK;
}

static esp_err_t wifi_set_handler(httpd_req_t *req) {

    char buf[512];
    int total_len = req->content_len;

    if (total_len >= (int)sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Payload too large");
        return ESP_FAIL;
    }

    int cur_len = 0, received;
    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len - cur_len);
        if (received <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';

    ESP_LOGI(TAG, "Settings payload: %s", buf);

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"Invalid JSON\"}");
        return ESP_OK;
    }

    cJSON *j_ssid      = cJSON_GetObjectItem(root, "ssid");
    cJSON *j_password  = cJSON_GetObjectItem(root, "password");
    cJSON *j_prov_code = cJSON_GetObjectItem(root, "prov_code");
    cJSON *j_srv_url   = cJSON_GetObjectItem(root, "srv_url");
    cJSON *j_mqtt_host = cJSON_GetObjectItem(root, "mqtt_host");
    cJSON *j_mqtt_port = cJSON_GetObjectItem(root, "mqtt_port");
    cJSON *j_mqtt_user = cJSON_GetObjectItem(root, "mqtt_user");
    cJSON *j_mqtt_pass = cJSON_GetObjectItem(root, "mqtt_pass");

    if (!j_ssid || !cJSON_IsString(j_ssid) || strlen(j_ssid->valuestring) == 0) {
        cJSON_Delete(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"ssid is required\"}");
        return ESP_OK;
    }

    /* Apply WiFi config — ESP-IDF persists STA config to its own NVS namespace automatically */
    wifi_config_t wifi_config = {0};
    esp_wifi_get_config(WIFI_IF_STA, &wifi_config);
    strncpy((char *)wifi_config.sta.ssid,     j_ssid->valuestring,     sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, j_password && cJSON_IsString(j_password)
                                                ? j_password->valuestring : "",
                                              sizeof(wifi_config.sta.password) - 1);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);

    /* Save provisioning code and server URL to NVS */
    nvs_handle_t handle;
    if (nvs_open("vmflow", NVS_READWRITE, &handle) == ESP_OK) {

        if (j_prov_code && cJSON_IsString(j_prov_code) && strlen(j_prov_code->valuestring) > 0) {
            nvs_set_str(handle, "prov_code", j_prov_code->valuestring);
            ESP_LOGI(TAG, "Provisioning code saved: %s", j_prov_code->valuestring);
        }

        if (j_srv_url && cJSON_IsString(j_srv_url) && strlen(j_srv_url->valuestring) > 0) {
            nvs_set_str(handle, "srv_url", j_srv_url->valuestring);
            ESP_LOGI(TAG, "Server URL saved: %s", j_srv_url->valuestring);
        }

        if (j_mqtt_host && cJSON_IsString(j_mqtt_host) && strlen(j_mqtt_host->valuestring) > 0) {
            nvs_set_str(handle, "mqtt_host", j_mqtt_host->valuestring);
            ESP_LOGI(TAG, "MQTT host saved: %s", j_mqtt_host->valuestring);
        }

        if (j_mqtt_port && cJSON_IsString(j_mqtt_port) && strlen(j_mqtt_port->valuestring) > 0) {
            nvs_set_str(handle, "mqtt_port", j_mqtt_port->valuestring);
            ESP_LOGI(TAG, "MQTT port saved: %s", j_mqtt_port->valuestring);
        }

        if (j_mqtt_user && cJSON_IsString(j_mqtt_user) && strlen(j_mqtt_user->valuestring) > 0) {
            nvs_set_str(handle, "mqtt_user", j_mqtt_user->valuestring);
            ESP_LOGI(TAG, "MQTT username saved");
        }

        if (j_mqtt_pass && cJSON_IsString(j_mqtt_pass) && strlen(j_mqtt_pass->valuestring) > 0) {
            nvs_set_str(handle, "mqtt_pass", j_mqtt_pass->valuestring);
            ESP_LOGI(TAG, "MQTT password saved");
        }

        nvs_commit(handle);
        nvs_close(handle);
    }

    cJSON_Delete(root);

    /* Connect — the IP_EVENT_STA_GOT_IP handler picks up from here */
    esp_wifi_connect();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t system_info_get_handler(httpd_req_t *req) {

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    wifi_config_t wifi_cfg = {0};
    esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg);

    char srv_url[128] = "";
    char mqtt_host[128] = "";
    char mqtt_port[8] = "";
    char mqtt_user[64] = "";
    char mqtt_pass[64] = "";
    nvs_handle_t handle;
    if (nvs_open("vmflow", NVS_READONLY, &handle) == ESP_OK) {
        size_t s_len = sizeof(srv_url);
        nvs_get_str(handle, "srv_url", srv_url, &s_len);
        size_t h_len = sizeof(mqtt_host);
        nvs_get_str(handle, "mqtt_host", mqtt_host, &h_len);
        size_t p_len = sizeof(mqtt_port);
        nvs_get_str(handle, "mqtt_port", mqtt_port, &p_len);
        size_t u_len = sizeof(mqtt_user);
        nvs_get_str(handle, "mqtt_user", mqtt_user, &u_len);
        size_t pw_len = sizeof(mqtt_pass);
        nvs_get_str(handle, "mqtt_pass", mqtt_pass, &pw_len);
        nvs_close(handle);
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "version",       IDF_VER);
    cJSON_AddNumberToObject(root, "cores",         chip_info.cores);
    cJSON_AddNumberToObject(root, "model",         chip_info.model);
    cJSON_AddStringToObject(root, "wifi_ssid",     (char *)wifi_cfg.sta.ssid);
    cJSON_AddStringToObject(root, "wifi_password", (char *)wifi_cfg.sta.password);
    cJSON_AddStringToObject(root, "srv_url",       srv_url);
    cJSON_AddStringToObject(root, "mqtt_host",     mqtt_host);
    cJSON_AddStringToObject(root, "mqtt_port",     mqtt_port);
    cJSON_AddStringToObject(root, "mqtt_user",     mqtt_user);
    cJSON_AddStringToObject(root, "mqtt_pass",     mqtt_pass);

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
        ESP_LOGW(TAG, "WiFi scan failed: %s", esp_err_to_name(err));
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"networks\":[],\"error\":\"scan_failed\"}");
        return ESP_OK;
    }

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count > 20) ap_count = 20;

    wifi_ap_record_t *ap_records = calloc(ap_count, sizeof(wifi_ap_record_t));
    if (!ap_records) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"networks\":[],\"error\":\"out_of_memory\"}");
        return ESP_OK;
    }
    esp_wifi_scan_get_ap_records(&ap_count, ap_records);

    /* Sort by RSSI descending (simple bubble sort, n<=20) */
    for (int i = 0; i < ap_count - 1; i++) {
        for (int j = 0; j < ap_count - i - 1; j++) {
            if (ap_records[j].rssi < ap_records[j + 1].rssi) {
                wifi_ap_record_t tmp = ap_records[j];
                ap_records[j] = ap_records[j + 1];
                ap_records[j + 1] = tmp;
            }
        }
    }

    /* Build deduplicated JSON array, filtering own AP and hidden networks */
    cJSON *root = cJSON_CreateObject();
    cJSON *networks = cJSON_AddArrayToObject(root, "networks");

    char seen_ssids[20][33];
    int seen_count = 0;

    for (int i = 0; i < ap_count; i++) {
        const char *ssid = (const char *)ap_records[i].ssid;

        /* Skip empty SSIDs (hidden networks) */
        if (strlen(ssid) == 0) continue;

        /* Skip own AP */
        if (strcmp(ssid, AP_SSID) == 0) continue;

        /* Skip duplicates (already sorted by RSSI, first occurrence is strongest) */
        bool dup = false;
        for (int s = 0; s < seen_count; s++) {
            if (strcmp(seen_ssids[s], ssid) == 0) { dup = true; break; }
        }
        if (dup) continue;

        if (seen_count < 20) {
            strncpy(seen_ssids[seen_count], ssid, 32);
            seen_ssids[seen_count][32] = '\0';
            seen_count++;
        }

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

static esp_err_t captive_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Captive portal redirect: %s", req->uri);
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
    dns_pcb = udp_new();
    udp_bind(dns_pcb, IP_ADDR_ANY, 53);
    udp_recv(dns_pcb, dns_recv, NULL);
    ESP_LOGI(TAG, "DNS started");
}

/* ---------- HTTP server ---------- */

void stop_rest_server(void) {
    if (rest_server == NULL) return;
    httpd_stop(rest_server);
    rest_server = NULL;
}

void start_rest_server(void) {
    if (rest_server != NULL) return;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_start(&rest_server, &config);

    static const httpd_uri_t uris[] = {
        { .uri = "/",                    .method = HTTP_GET,  .handler = index_get_handler        },
        { .uri = "/api/v1/system/info",  .method = HTTP_GET,  .handler = system_info_get_handler  },
        { .uri = "/api/v1/settings/set", .method = HTTP_POST, .handler = wifi_set_handler         },
        { .uri = "/api/v1/wifi/scan",    .method = HTTP_GET,  .handler = wifi_scan_get_handler    },
        { .uri = "/generate_204",        .method = HTTP_GET,  .handler = captive_handler          },
        { .uri = "/hotspot-detect.html", .method = HTTP_GET,  .handler = captive_handler          },
    };

    for (int i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(rest_server, &uris[i]);
    }
}

/* ---------- SoftAP ---------- */

void start_softap(void) {
    wifi_config_t wifi_config = {
        .ap = {
            .ssid           = AP_SSID,
            .ssid_len       = strlen(AP_SSID),
            .password       = AP_PASS,
            .max_connection = 4,
            .authmode       = WIFI_AUTH_WPA_WPA2_PSK,
        },
    };
    esp_err_t err = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    ESP_LOGI(TAG, "SoftAP config set (SSID=\"%s\") → %s", AP_SSID, esp_err_to_name(err));
}
