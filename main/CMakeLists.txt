idf_component_register(SRCS "main.c" "wifi.c" "can.c" "web_server.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_wifi esp_http_server nvs_flash esp_netif esp_event driver mdns)
