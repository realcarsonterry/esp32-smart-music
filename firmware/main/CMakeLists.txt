idf_component_register(
    SRCS "app_main.c" "audio.c" "config.c" "transport.c" "wire.c"
         "../../simulator/music_json.c"
    INCLUDE_DIRS "." "../../simulator"
    REQUIRES core json esp_driver_i2s esp_driver_gpio esp_http_client
             esp_wifi esp_event esp_netif nvs_flash mbedtls esp_timer)
target_compile_features(${COMPONENT_LIB} PRIVATE c_std_11)
target_compile_options(${COMPONENT_LIB} PRIVATE -fno-fast-math -Wall -Wextra)
