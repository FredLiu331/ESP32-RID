idf_component_register(SRCS "identity.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES rid_model
                       PRIV_REQUIRES mbedtls)
