idf_component_register(SRCS "test_main.cpp" "test_smoke.cpp" "test_model_identity.cpp"
                            "test_config.cpp" "test_trajectory.cpp"
                       PRIV_REQUIRES unity rid_model rid_identity rid_config rid_trajectory
                       WHOLE_ARCHIVE)
