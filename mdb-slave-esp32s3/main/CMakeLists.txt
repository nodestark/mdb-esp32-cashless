set(srcs "mdb-slave-esp32s3.c"
        "webui_server.c"
        "nimble.c")

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS "."
                    EMBED_FILES "../webui/index.html"
                    REQUIRES esp_wifi esp_http_client esp_http_server esp-tls mbedtls
                             mqtt driver nvs_flash lwip esp_adc json bt)