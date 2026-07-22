idf_component_register(SRCS "test_main.cpp" "test_ble_transport.cpp" "test_wifi_transport.cpp"
                       PRIV_REQUIRES unity rid_radio rid_odid nvs_flash esp_wifi
                       WHOLE_ARCHIVE)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
