# 依赖解析：优先 find_package（vcpkg manifest / 系统包），缺失时可选 FetchContent。
#
# - 交付/正式构建：使用 vcpkg（CMakePresets.json 的 `vcpkg` 预设）或系统包。
# - 本地无包管理器验证：`cmake -DSPARKLE_FETCH_DEPS=ON ...` 从 GitHub（codeload）拉取。
#   使用 URL + 固定 SHA256（避免 git 全量克隆），哈希对应下方固定 tag。

option(SPARKLE_FETCH_DEPS "Fetch missing 3rd-party deps from GitHub" OFF)

# QuickJS 上游无 CMakeLists.txt，需用 FetchContent_Populate 仅下载后手动建目标；
# 关掉 CMP0169 的弃用告警（此处确有理由保留旧式调用）。
if(POLICY CMP0169)
  cmake_policy(SET CMP0169 OLD)
endif()

find_package(nlohmann_json CONFIG QUIET)
find_package(spdlog CONFIG QUIET)
find_package(yaml-cpp CONFIG QUIET)

set(_sparkle_missing_deps)
if(NOT nlohmann_json_FOUND)
  list(APPEND _sparkle_missing_deps nlohmann_json)
endif()
if(NOT spdlog_FOUND)
  list(APPEND _sparkle_missing_deps spdlog)
endif()
if(NOT yaml-cpp_FOUND)
  list(APPEND _sparkle_missing_deps yaml-cpp)
endif()

if(_sparkle_missing_deps)
  if(NOT SPARKLE_FETCH_DEPS)
    message(FATAL_ERROR "Missing dependencies: ${_sparkle_missing_deps}. "
      "Install them (vcpkg manifest or system package), or configure with "
      "-DSPARKLE_FETCH_DEPS=ON to fetch from GitHub.")
  endif()

  include(FetchContent)

  # 抑制子项目自身的测试/工具构建
  set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
  set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)

  if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(nlohmann_json
      URL https://codeload.github.com/nlohmann/json/tar.gz/refs/tags/v3.11.3
      URL_HASH SHA256=0d8ef5af7f9794e3263480193c491549b2ba6cc74bb018906202ada498a79406
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  endif()
  if(NOT spdlog_FOUND)
    FetchContent_Declare(spdlog
      URL https://codeload.github.com/gabime/spdlog/tar.gz/refs/tags/v1.15.3
      URL_HASH SHA256=15a04e69c222eb6c01094b5c7ff8a249b36bb22788d72519646fb85feb267e67
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  endif()
  if(NOT yaml-cpp_FOUND)
    FetchContent_Declare(yaml-cpp
      URL https://codeload.github.com/jbeder/yaml-cpp/tar.gz/refs/tags/yaml-cpp-0.9.0
      URL_HASH SHA256=25cb043240f828a8c51beb830569634bc7ac603978e0f69d6b63558dadefd49a
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  endif()

  FetchContent_MakeAvailable(${_sparkle_missing_deps})
endif()

# ---- QuickJS（bellard/quickjs，无 CMakeLists 且无 git tag，按 commit 固定；用于 JS 覆写执行）。
# 正式构建可走 vcpkg 的 quickjs 端口（find_package(quickjs)），其目标统一映射到 QUICKJS_TARGET。
set(QUICKJS_TARGET "")
find_package(quickjs CONFIG QUIET)
if(quickjs_FOUND)
  if(TARGET quickjs::libquickjs)
    set(QUICKJS_TARGET quickjs::libquickjs)
  elseif(TARGET quickjs)
    set(QUICKJS_TARGET quickjs)
  endif()
endif()

if(NOT QUICKJS_TARGET)
  if(NOT SPARKLE_FETCH_DEPS)
    message(FATAL_ERROR "QuickJS not found: use the vcpkg 'quickjs' port or -DSPARKLE_FETCH_DEPS=ON.")
  endif()
  enable_language(C)
  include(FetchContent)
  FetchContent_Declare(quickjs_src
    URL https://codeload.github.com/bellard/quickjs/tar.gz/04be246001599f5995fa2f2d8c91a0f198d3f34c
    URL_HASH SHA256=2a87ffcca6c870f764ce70a7736351bd7cff3dc1fb95a8fb059c260979f1e01a
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_GetProperties(quickjs_src)
  if(NOT quickjs_src_POPULATED)
    FetchContent_Populate(quickjs_src)
  endif()

  # 核心库（不含 quickjs-libc 的 std/os 模块；std 由项目自建）：
  # quickjs + dtoa + libregexp + libunicode + cutils。
  add_library(quickjs STATIC
    ${quickjs_src_SOURCE_DIR}/quickjs.c
    ${quickjs_src_SOURCE_DIR}/dtoa.c
    ${quickjs_src_SOURCE_DIR}/libregexp.c
    ${quickjs_src_SOURCE_DIR}/libunicode.c
    ${quickjs_src_SOURCE_DIR}/cutils.c)
  # 仅向消费者暴露 quickjs.h：QuickJS 源目录含无扩展名的 `version` 文件，
  # 若公开到 C++ include 路径会劫持 libc++ 的 `#include <version>`。
  set(_sparkle_qjs_hdr ${CMAKE_BINARY_DIR}/quickjs_headers)
  file(MAKE_DIRECTORY ${_sparkle_qjs_hdr})
  configure_file(${quickjs_src_SOURCE_DIR}/quickjs.h ${_sparkle_qjs_hdr}/quickjs.h COPYONLY)
  target_include_directories(quickjs PRIVATE ${quickjs_src_SOURCE_DIR})
  target_include_directories(quickjs INTERFACE ${_sparkle_qjs_hdr})
  target_compile_definitions(quickjs PRIVATE CONFIG_VERSION="2026-06-04")
  if(MSVC)
    target_compile_options(quickjs PRIVATE /J)
    target_compile_definitions(quickjs PRIVATE _CRT_SECURE_NO_WARNINGS)
  else()
    target_compile_definitions(quickjs PRIVATE _GNU_SOURCE)
    # -funsigned-char / -fwrapv 为 QuickJS 正确性要求；-w 抑制第三方 C 源码告警。
    target_compile_options(quickjs PRIVATE -funsigned-char -fwrapv -w)
  endif()
  set(QUICKJS_TARGET quickjs)
endif()