idf_component_register(SRCS "test_main.cpp" "test_smoke.cpp" "test_model_identity.cpp"
                       PRIV_REQUIRES unity rid_model rid_identity
                       WHOLE_ARCHIVE)
