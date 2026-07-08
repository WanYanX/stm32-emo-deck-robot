set(LUCY_ENV_FILE "${CMAKE_SOURCE_DIR}/.env")
set(LUCY_ENV_DEFINITIONS "")

if(EXISTS "${LUCY_ENV_FILE}")
    file(STRINGS "${LUCY_ENV_FILE}" LUCY_ENV_LINES ENCODING UTF-8)
    foreach(LUCY_ENV_LINE IN LISTS LUCY_ENV_LINES)
        string(STRIP "${LUCY_ENV_LINE}" LUCY_ENV_LINE)
        if(LUCY_ENV_LINE STREQUAL "" OR LUCY_ENV_LINE MATCHES "^#")
            continue()
        endif()

        if(LUCY_ENV_LINE MATCHES "^([A-Za-z_][A-Za-z0-9_]*)=(.*)$")
            set(LUCY_ENV_KEY "${CMAKE_MATCH_1}")
            set(LUCY_ENV_VALUE "${CMAKE_MATCH_2}")
            string(STRIP "${LUCY_ENV_VALUE}" LUCY_ENV_VALUE)

            if(LUCY_ENV_KEY MATCHES "^LUCY_|^CMD_WAKE_LUCY$")
                if(LUCY_ENV_KEY MATCHES "SSID|PASSWORD|URI|TOKEN")
                    string(REPLACE "\\" "\\\\" LUCY_ENV_VALUE "${LUCY_ENV_VALUE}")
                    string(REPLACE "\"" "\\\"" LUCY_ENV_VALUE "${LUCY_ENV_VALUE}")
                    list(APPEND LUCY_ENV_DEFINITIONS "${LUCY_ENV_KEY}=\"${LUCY_ENV_VALUE}\"")
                else()
                    list(APPEND LUCY_ENV_DEFINITIONS "${LUCY_ENV_KEY}=${LUCY_ENV_VALUE}")
                endif()
            endif()
        endif()
    endforeach()
endif()

idf_component_register(
    SRCS
        "main.c"
        "lucy_audio.c"
        "lucy_board.c"
        "lucy_sr.c"
        "lucy_wifi.c"
        "lucy_ws_voice.c"
        "lucy_wakeup_pcm.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_driver_gpio
        esp_driver_i2s
        esp_event
        esp_netif
        esp_websocket_client
        esp_wifi
        nvs_flash
)

if(LUCY_ENV_DEFINITIONS)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ${LUCY_ENV_DEFINITIONS})
endif()
