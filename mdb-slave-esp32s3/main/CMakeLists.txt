set(srcs "mdb-slave-esp32s3.c"
        "webui_server.c"
        "nimble.c")

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS "."
                    EMBED_FILES "../webui/index.html"
                    REQUIRES esp_wifi esp_http_client esp_http_server esp-tls mbedtls
                             mqtt driver nvs_flash lwip esp_adc json bt
                             app_update esp_https_ota)

# Capture the build machine's UTC offset (e.g. "+0100", "-0500") at configure time.
# Passed as a compile definition so the firmware can include timezone info
# in the MQTT status message, allowing correct display regardless of where it was built.
string(TIMESTAMP BUILD_TIMEZONE "%z")
target_compile_definitions(${COMPONENT_LIB} PRIVATE BUILD_TIMEZONE=\"${BUILD_TIMEZONE}\")