idf_component_register(
    SRCS "main.c" "display.c" "ui.c" "jstream.c" "metrics.c" "config.c" "wifi.c" "console.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES nvs_flash esp_timer esp_system lwip esp_wifi esp_netif esp_event esp_http_client
                  esp_driver_spi esp_driver_gpio esp_driver_ledc esp_driver_uart)
