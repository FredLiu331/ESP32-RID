idf_component_register(SRCS "radio_probe.cpp"
                       PRIV_REQUIRES bt esp_event esp_netif esp_wifi nvs_flash)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
