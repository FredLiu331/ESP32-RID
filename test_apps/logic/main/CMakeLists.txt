idf_component_register(SRCS "test_main.cpp" "test_smoke.cpp" "test_model_identity.cpp"
                            "test_config.cpp" "test_trajectory.cpp" "test_odid_encoder.cpp" "test_shell.cpp"
                            "test_gb_encoder.cpp" "test_scheduler.cpp"
                       PRIV_REQUIRES unity rid_model rid_identity rid_config rid_trajectory rid_odid
                                     rid_gb rid_scheduler rid_shell
                       WHOLE_ARCHIVE)
