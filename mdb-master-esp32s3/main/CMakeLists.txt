set(srcs "mdb-master-esp32s3.c"
        "webui_server.c")

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS "."
                    EMBED_FILES "../webui/index.html"
                    REQUIRES esp_wifi esp_http_server nvs_flash lwip json
                             driver esp_netif esp_timer esp_event)
