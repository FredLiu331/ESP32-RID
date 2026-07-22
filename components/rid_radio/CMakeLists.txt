idf_component_register(SRCS "ble_transport.cpp" "wifi_transport.cpp" "radio_coordinator.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES bt rid_scheduler esp_event esp_netif esp_wifi)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
