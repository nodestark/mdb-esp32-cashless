set(srcs "mdb-slave-esp32s3.c"
        "nimble.c")

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS ".")
                    