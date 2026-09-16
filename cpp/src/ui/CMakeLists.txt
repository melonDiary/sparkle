add_library(sparkle_ui STATIC
  app_model.h app_model.cpp
  script_bridge.h script_bridge.cpp
)

target_include_directories(sparkle_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sparkle_ui PUBLIC Qt6::Core sparkle_core)
sparkle_enable_warnings(sparkle_ui)