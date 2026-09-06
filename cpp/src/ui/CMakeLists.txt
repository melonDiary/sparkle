add_library(sparkle_ui STATIC
  theme_manager.h theme_manager.cpp
  page_base.h
  app_model.h app_model.cpp
  script_bridge.h script_bridge.cpp
  sidebar.h sidebar.cpp
  main_window.h main_window.cpp
  models/log_list_model.h models/log_list_model.cpp
  models/proxy_list_model.h models/proxy_list_model.cpp
  models/rule_list_model.h models/rule_list_model.cpp
  pages/proxies_page.h pages/proxies_page.cpp
  pages/rules_page.h pages/rules_page.cpp
  pages/logs_page.h pages/logs_page.cpp
  pages/settings_page.h pages/settings_page.cpp
  pages/mihomo_page.h pages/mihomo_page.cpp
)

target_include_directories(sparkle_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sparkle_ui PUBLIC Qt6::Widgets sparkle_core)
sparkle_enable_warnings(sparkle_ui)