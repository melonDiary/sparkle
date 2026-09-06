cmake_minimum_required(VERSION 3.20)
project(sparkle VERSION 1.26.7 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Qt 元对象/资源/ui 自动处理
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

option(SPARKLE_BUILD_TESTS "Build Sparkle unit tests" ON)

include(cmake/SparkleFunctions.cmake)

# ---- 依赖 ----
# Qt 建议走系统安装（官方在线安装器 / apt / homebrew），不放进 vcpkg（见 README）。
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Quick Qml QuickControls2 Network Concurrent)
include(cmake/Dependencies.cmake)

add_subdirectory(src/platform)
add_subdirectory(src/core)
add_subdirectory(src/ui)
set(SPARKLE_QML_RESOURCE "${CMAKE_CURRENT_SOURCE_DIR}/qml/qml.qrc")
add_subdirectory(src/app)

if(SPARKLE_BUILD_TESTS)
  enable_testing()
  find_package(Qt6 REQUIRED COMPONENTS Test)
  add_subdirectory(tests)
endif()