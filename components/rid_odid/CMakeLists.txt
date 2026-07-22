idf_component_register(SRCS "odid_encoder.cpp" "vendor/opendroneid.c" "vendor/opendroneid_wifi.c"
                       INCLUDE_DIRS "include" "vendor"
                       REQUIRES rid_model)

target_compile_definitions(${COMPONENT_LIB} PRIVATE ODID_AUTH_MAX_PAGES=1)
set_source_files_properties("vendor/opendroneid.c" PROPERTIES COMPILE_OPTIONS "-Wno-format")
