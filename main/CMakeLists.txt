idf_component_register(SRCS "main.cpp" "config.cpp" "ha_client.cpp" "display.cpp" "ui_assets.c" "backlight.cpp"
  INCLUDE_DIRS "." "../include"
  REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_http_server esp_timer app_update
           esp_lcd driver json mbedtls esp_websocket_client esp_lcd_gc9a01 esp_lvgl_port lvgl
  EMBED_TXTFILES "portal.html")
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
