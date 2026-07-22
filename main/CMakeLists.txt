idf_component_register(SRCS "app_main.cpp"
                       PRIV_REQUIRES log nvs_flash esp_system esp_timer freertos
                                     rid_runtime rid_config rid_radio rid_shell)
