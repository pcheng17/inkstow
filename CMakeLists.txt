cmake_minimum_required(VERSION 3.21)
project(Inkstow VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/CompilerOptions.cmake)

# Homebrew pkg-config on Linux may hide apt's WebKitGTK and GIO .pc files.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND EXISTS "/usr/bin/pkg-config")
  set(PKG_CONFIG_EXECUTABLE "/usr/bin/pkg-config" CACHE FILEPATH "System pkg-config" FORCE)
endif()

# Vault and session rules shared by the WebView shell and inspection tool.
add_library(portable-core src/core/vault/Vault.cpp src/core/vault/VaultActions.cpp
                          src/core/vault/Trash.cpp src/core/vault/NoteInput.cpp
                          src/core/vault/UnicodeCase.cpp src/core/vault/Session.cpp)
target_include_directories(portable-core PUBLIC src/core/vault)
target_link_libraries(portable-core PRIVATE inkstow-warnings)
if(APPLE)
  enable_language(OBJCXX)
  target_sources(portable-core PRIVATE src/core/vault/TrashMac.mm)
  target_link_libraries(portable-core PRIVATE "-framework Foundation" "-framework CoreFoundation")
elseif(WIN32)
  target_link_libraries(portable-core PRIVATE shell32 ole32 normaliz)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
  target_link_libraries(portable-core PRIVATE PkgConfig::GIO)
endif()

add_executable(vault-inspect EXCLUDE_FROM_ALL src/VaultInspect.cpp)
target_link_libraries(vault-inspect PRIVATE portable-core inkstow-warnings)

# Pinned archives are downloaded only on first configure; builds use cached sources.
include(FetchContent)
FetchContent_Declare(webview
  URL https://github.com/webview/webview/archive/refs/tags/0.12.0.tar.gz
  URL_HASH SHA256=e2c8d0bed3fcd13e624074448b7d3b1d0b0d03546a15839f17ee0013f2cba1db
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/archive/refs/tags/v3.12.0.tar.gz
  URL_HASH SHA256=4b92eb0c06d10683f7447ce9406cb97cd4b453be18d7279320f7b2f025c10187
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(WEBVIEW_BUILD_STATIC_LIBRARY ON CACHE BOOL "Build webview for Inkstow" FORCE)
set(WEBVIEW_BUILD_SHARED_LIBRARY OFF CACHE BOOL "No shared webview library" FORCE)
FetchContent_MakeAvailable(webview nlohmann_json)

# Build the offline frontend before compiling the shell; keep generated assets out of the tree.
if(WIN32)
  find_program(NPM_EXECUTABLE npm.cmd REQUIRED)
  # MSBuild runs custom commands in a batch file; call returns to it after npm.cmd.
  set(NPM_COMMAND cmd /c call "${NPM_EXECUTABLE}")
else()
  find_program(NPM_EXECUTABLE npm REQUIRED)
  set(NPM_COMMAND "${NPM_EXECUTABLE}")
endif()
find_program(NODE_EXECUTABLE node REQUIRED)
set(FRONTEND_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/frontend")
set(WEBVIEW_ASSET_DIR "${CMAKE_CURRENT_BINARY_DIR}/webview-assets")
file(MAKE_DIRECTORY "${WEBVIEW_ASSET_DIR}")
configure_file("${FRONTEND_SOURCE_DIR}/package.json" "${WEBVIEW_ASSET_DIR}/package.json" COPYONLY)
configure_file("${FRONTEND_SOURCE_DIR}/package-lock.json" "${WEBVIEW_ASSET_DIR}/package-lock.json" COPYONLY)
configure_file("${FRONTEND_SOURCE_DIR}/build.mjs" "${WEBVIEW_ASSET_DIR}/build.mjs" COPYONLY)
configure_file("${FRONTEND_SOURCE_DIR}/editor.test.mjs" "${WEBVIEW_ASSET_DIR}/editor.test.mjs" COPYONLY)
add_custom_command(OUTPUT "${WEBVIEW_ASSET_DIR}/installed.stamp"
  COMMAND ${NPM_COMMAND} ci --ignore-scripts --no-audit --no-fund
  COMMAND "${CMAKE_COMMAND}" -E touch "${WEBVIEW_ASSET_DIR}/installed.stamp"
  WORKING_DIRECTORY "${WEBVIEW_ASSET_DIR}"
  DEPENDS "${FRONTEND_SOURCE_DIR}/package.json" "${FRONTEND_SOURCE_DIR}/package-lock.json"
  VERBATIM)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/LocalHtml.h"
  COMMAND "${NODE_EXECUTABLE}" "${WEBVIEW_ASSET_DIR}/build.mjs"
          "${FRONTEND_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/generated/LocalHtml.h"
  DEPENDS "${WEBVIEW_ASSET_DIR}/installed.stamp" "${FRONTEND_SOURCE_DIR}/build.mjs"
          "${FRONTEND_SOURCE_DIR}/index.html" "${FRONTEND_SOURCE_DIR}/main.ts"
          "${FRONTEND_SOURCE_DIR}/editor.ts" "${FRONTEND_SOURCE_DIR}/ui.ts"
          "${FRONTEND_SOURCE_DIR}/preview.js" "${FRONTEND_SOURCE_DIR}/style.css"
  VERBATIM)
add_custom_target(webview-assets DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/generated/LocalHtml.h")
add_subdirectory(src/desktop)

include(CTest)
if(BUILD_TESTING)
  include(FetchContent)
  FetchContent_Declare(doctest
    URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.12.tar.gz
    URL_HASH SHA256=73381c7aa4dee704bd935609668cf41880ea7f19fa0504a200e13b74999c2d70
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(doctest)
  add_test(NAME webview-js-tests COMMAND "${NODE_EXECUTABLE}" --test editor.test.mjs
           WORKING_DIRECTORY "${WEBVIEW_ASSET_DIR}")
  add_subdirectory(tests)
endif()
