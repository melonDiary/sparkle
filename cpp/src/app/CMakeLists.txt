add_executable(sparkle
  main.cpp
  app_controller.h app_controller.cpp
)
target_sources(sparkle PRIVATE ${SPARKLE_QML_RESOURCE})
target_link_libraries(sparkle PRIVATE sparkle_ui sparkle_core Qt6::Quick Qt6::Qml Qt6::QuickControls2)

if(WIN32)
  set_target_properties(sparkle PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
if(APPLE)
  set_target_properties(sparkle PROPERTIES MACOSX_BUNDLE TRUE)
endif()

sparkle_enable_warnings(sparkle)