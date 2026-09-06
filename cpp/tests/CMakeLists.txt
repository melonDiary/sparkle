add_executable(tst_yaml_store tst_yaml_store.cpp)
target_link_libraries(tst_yaml_store PRIVATE Qt6::Test sparkle_core)
add_test(NAME tst_yaml_store COMMAND tst_yaml_store)

add_executable(tst_latest_sender tst_latest_sender.cpp)
target_link_libraries(tst_latest_sender PRIVATE Qt6::Test sparkle_core)
add_test(NAME tst_latest_sender COMMAND tst_latest_sender)

add_executable(tst_log_list_model tst_log_list_model.cpp)
target_link_libraries(tst_log_list_model PRIVATE Qt6::Test sparkle_ui)
add_test(NAME tst_log_list_model COMMAND tst_log_list_model)

add_executable(tst_rule_list_model tst_rule_list_model.cpp)
target_link_libraries(tst_rule_list_model PRIVATE Qt6::Test sparkle_ui)
add_test(NAME tst_rule_list_model COMMAND tst_rule_list_model)

add_executable(tst_core_process_controller tst_core_process_controller.cpp)
target_link_libraries(tst_core_process_controller PRIVATE Qt6::Test sparkle_core)
add_test(NAME tst_core_process_controller COMMAND tst_core_process_controller)

add_executable(tst_http_ws tst_http_ws.cpp)
target_link_libraries(tst_http_ws PRIVATE Qt6::Test sparkle_core)
add_test(NAME tst_http_ws COMMAND tst_http_ws)

add_executable(tst_js_engine tst_js_engine.cpp)
target_link_libraries(tst_js_engine PRIVATE Qt6::Test sparkle_core)
add_test(NAME tst_js_engine COMMAND tst_js_engine)

add_executable(tst_script_engine tst_script_engine.cpp)
target_link_libraries(tst_script_engine PRIVATE Qt6::Test sparkle_core)
add_test(NAME tst_script_engine COMMAND tst_script_engine)

add_executable(tst_config_plugin tst_config_plugin.cpp)
target_link_libraries(tst_config_plugin PRIVATE Qt6::Test sparkle_core)
add_test(NAME tst_config_plugin COMMAND tst_config_plugin)

add_executable(tst_system_proxy_manager tst_system_proxy_manager.cpp)
target_link_libraries(tst_system_proxy_manager PRIVATE Qt6::Test sparkle_core)
add_test(NAME tst_system_proxy_manager COMMAND tst_system_proxy_manager)

add_executable(tst_mitm tst_mitm.cpp)
target_link_libraries(tst_mitm PRIVATE Qt6::Test sparkle_core)
add_test(NAME tst_mitm COMMAND tst_mitm)
