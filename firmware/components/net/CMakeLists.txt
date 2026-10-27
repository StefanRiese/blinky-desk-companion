idf_component_register(
    SRCS "net.cpp" "client.cpp" "web.cpp"
    INCLUDE_DIRS "include"
    EMBED_FILES "web/index.html"
    REQUIRES app_core esp_wifi esp_netif esp_event esp_http_client esp_http_server esp_timer lwip mbedtls log freertos
)
