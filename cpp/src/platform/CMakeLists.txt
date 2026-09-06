if(APPLE)
  enable_language(OBJCXX)
  set(SPARKLE_PLATFORM_SRC SystemProxyManager_mac.mm)
elseif(WIN32)
  set(SPARKLE_PLATFORM_SRC SystemProxyManager_win.cpp)
else()
  set(SPARKLE_PLATFORM_SRC SystemProxyManager_linux.cpp)
endif()

add_library(sparkle_platform STATIC
  system_proxy.h
  ${SPARKLE_PLATFORM_SRC}
)

target_include_directories(sparkle_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sparkle_platform PUBLIC Qt6::Core Qt6::Network)
sparkle_enable_warnings(sparkle_platform)
