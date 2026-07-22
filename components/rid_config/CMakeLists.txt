idf_component_register(SRCS "config.cpp" "nvs_store.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES rid_model rid_identity rid_trajectory nvs_flash esp_hw_support)
