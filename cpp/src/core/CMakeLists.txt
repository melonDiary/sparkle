add_library(sparkle_core STATIC
  models.h models.cpp
  ProxyNode.h
  paths.h paths.cpp
  log_manager.h log_manager.cpp
  json_yaml.h yaml_store.h
  config_manager.h config_manager.cpp
  runtime_config_factory.h runtime_config_factory.cpp
  core_process_controller.h core_process_controller.cpp
  http_client.h http_client.cpp
  ws_client.h ws_client.cpp
  latest_sender.h
  mihomo_api_client.h mihomo_api_client.cpp
  core_manager.h core_manager.cpp
  SystemProxyManager.h system_proxy_manager.h system_proxy_manager.cpp
  pac_server.h pac_server.cpp
  socket_transport.h socket_transport.cpp
  js_engine.h js_engine.cpp
  script_engine.h script_engine.cpp
  IPlugin.h PluginSandbox.h PluginSandbox.cpp plugin_sandbox.h
  MITMRequest.h MITMResponse.h MITMManager.h MITMManager.cpp
  PluginManager.h PluginManager.cpp plugin_manager.h
  single_instance.h single_instance.cpp
)

target_include_directories(sparkle_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(sparkle_core PUBLIC
  Qt6::Core Qt6::Network Qt6::Concurrent
  nlohmann_json::nlohmann_json
  spdlog::spdlog
  yaml-cpp::yaml-cpp
  sparkle_platform
  ${QUICKJS_TARGET}
)

if(OpenSSL_FOUND)
  target_compile_definitions(sparkle_core PRIVATE SPARKLE_HAS_OPENSSL=1)
  target_link_libraries(sparkle_core PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()
sparkle_enable_warnings(sparkle_core)