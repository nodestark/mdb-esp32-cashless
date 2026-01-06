set(srcs "mdb-slave-esp32s3.c"
        "rest_server.c"
        "nimble.c")

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS ".")

set(WEB_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../webui")
if(EXISTS ${WEB_SRC_DIR})
    spiffs_create_partition_image(www ${WEB_SRC_DIR} FLASH_IN_PROJECT)
else()
    message(FATAL_ERROR "${WEB_SRC_DIR} doesn't exit. Please run 'npm run build' in ${WEB_SRC_DIR}")
endif()