set(srcs "mdb-slave-esp32s3-webui.c"
        "webui_server.c")

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS "."
                    EMBED_FILES "../webui/index.html")