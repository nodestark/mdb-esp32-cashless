set(srcs "mdb-slave-esp32s3.c" "nimble.c" "eva-dts.c" "rpc-auth.c")

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS "."
                    EMBED_FILES "")
