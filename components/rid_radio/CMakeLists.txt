idf_component_register(SRCS "ble_transport.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES bt rid_scheduler)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
