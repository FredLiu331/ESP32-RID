idf_component_register(SRCS "runtime.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES rid_config rid_trajectory rid_odid rid_gb rid_radio rid_scheduler)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
