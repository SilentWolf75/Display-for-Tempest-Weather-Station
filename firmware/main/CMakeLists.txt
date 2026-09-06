# Track first creation/removal as well as edits of local credentials.
if(NOT CMAKE_SCRIPT_MODE_FILE)
file(GLOB TEMPEST_CREDENTIALS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/secrets.h")
if(TEMPEST_CREDENTIALS AND NOT TEMPEST_PUBLIC_RELEASE)
    configure_file(secrets.h "${CMAKE_CURRENT_BINARY_DIR}/credentials_config.h" COPYONLY)
else()
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/credentials_config.h"
        "#pragma once\n#define TEMPEST_API_TOKEN \"\"\n#define WIFI_SSID \"\"\n#define WIFI_PASSWORD \"\"\n")
endif()
endif()

idf_component_register(
    SRCS
        "main.c"
        "sdcard.c"
        "wx_state.c"
        "wx_daily.c"
        "tempest_udp.c"
        "tempest_rest.c"
        "net.c"
        "indoor.c"
        "display.c"
        "ui/ui.c"
        "ui/wx_icons.c"
        "ui/weather_icons_data.c"
        "ui/settings.c"
        "ui/graphs.c"
        "ui/page2.c"
        "ui/week.c"
        "ui/alerts.c"
        "ui/wifi_setup.c"
        "history.c"
        "config.c"
        "diag.c"
        "ota.c"
        "ota_health.c"
        "audio.c"
        "nws_alerts.c"
        "alert_policy.c"
        "aqi_poll.c"
        "web_server.c"
        "mqtt_client_app.c"
        "wx_astronomy.c"
        "tempest_ws.c"
        "screenshot.c"
    EMBED_TXTFILES "ota.html" "ota.js"
    INCLUDE_DIRS
        "."
        "ui"
        "${CMAKE_CURRENT_BINARY_DIR}"
    REQUIRES
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        lwip
        esp_http_client
        esp-tls
        json
        esp_lcd
        driver
        esp_timer
        spiffs
        fatfs
        sdmmc
        esp_app_format
        app_update
        spi_flash
        esp_http_server
        mqtt
        mdns
        esp_websocket_client
)
