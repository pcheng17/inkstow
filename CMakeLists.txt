cmake_minimum_required(VERSION 3.21)
project(Inkstow VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE)
  include(cmake/Apple.cmake)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets)
include(cmake/CompilerOptions.cmake)

# Standalone Qt-free note path; the Qt core remains available during migration.
add_library(portable-core src/core/portable/Vault.cpp src/core/portable/VaultActions.cpp
                          src/core/portable/Trash.cpp src/core/portable/NoteInput.cpp
                          src/core/portable/UnicodeCase.cpp src/core/portable/Session.cpp)
target_include_directories(portable-core PUBLIC src/core/portable)
target_link_libraries(portable-core PRIVATE inkstow-warnings)
if(APPLE)
  enable_language(OBJCXX)
  target_sources(portable-core PRIVATE src/core/portable/TrashMac.mm)
  target_link_libraries(portable-core PRIVATE "-framework Foundation" "-framework CoreFoundation")
elseif(WIN32)
  target_link_libraries(portable-core PRIVATE shell32 ole32 normaliz)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
  target_link_libraries(portable-core PRIVATE PkgConfig::GIO)
endif()

add_library(core src/core/Vault.cpp src/core/Session.cpp)
target_include_directories(core PUBLIC src/core)
target_link_libraries(core PUBLIC Qt6::Core PRIVATE inkstow-warnings)

add_executable(vault-inspect EXCLUDE_FROM_ALL src/VaultInspect.cpp)
target_link_libraries(vault-inspect PRIVATE core inkstow-warnings)

add_executable(inkstow src/main.cpp src/ui/MarkdownHighlighter.cpp src/ui/NoteEditor.cpp
                       src/ui/NoteHeader.cpp src/ui/NoteTree.cpp src/ui/SettingsStore.cpp
                       src/ui/Theme.cpp src/ui/Window.cpp)
target_include_directories(inkstow PRIVATE src/ui)
target_link_libraries(inkstow PRIVATE core Qt6::Widgets inkstow-warnings)
if(APPLE)
  set_target_properties(inkstow PROPERTIES MACOSX_BUNDLE TRUE OUTPUT_NAME Inkstow)
endif()

# Qt-only builds can opt out before any WebKitGTK, npm or FetchContent discovery.
option(INKSTOW_BUILD_WEBVIEW "Build the offline system-webview shell" ON)
if(INKSTOW_BUILD_WEBVIEW)
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
  # Homebrew pkg-config on Linux may hide apt's WebKitGTK .pc files.
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND EXISTS "/usr/bin/pkg-config")
    set(PKG_CONFIG_EXECUTABLE "/usr/bin/pkg-config" CACHE FILEPATH "System pkg-config" FORCE)
  endif()
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
            "${FRONTEND_SOURCE_DIR}/preview.js" "${FRONTEND_SOURCE_DIR}/style.css"
    VERBATIM)
  add_custom_target(webview-assets DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/generated/LocalHtml.h")
  add_library(webview-bridge src/webview/Bridge.cpp)
  target_include_directories(webview-bridge PUBLIC src/webview)
  target_link_libraries(webview-bridge PUBLIC portable-core nlohmann_json::nlohmann_json
                                        PRIVATE inkstow-warnings)
  if(APPLE)
    enable_language(OBJCXX)
    set(NAVIGATION_POLICY_SOURCE src/webview/NavigationPolicyMac.mm)
  elseif(WIN32)
    set(NAVIGATION_POLICY_SOURCE src/webview/NavigationPolicyWindows.cpp)
  else()
    set(NAVIGATION_POLICY_SOURCE src/webview/NavigationPolicyLinux.cpp)
  endif()
  add_library(webview-watch src/webview/VaultWatch.cpp)
  target_include_directories(webview-watch PUBLIC src/webview)
  target_link_libraries(webview-watch PRIVATE inkstow-warnings)
  if(APPLE)
    set(ATTACHMENT_SHELL_SOURCE src/webview/AttachmentShellMac.mm)
  else()
    set(ATTACHMENT_SHELL_SOURCE src/webview/AttachmentShell.cpp)
  endif()
  add_executable(inkstow-webview src/webview/Main.cpp src/webview/DesktopServices.cpp
                                  ${NAVIGATION_POLICY_SOURCE} ${ATTACHMENT_SHELL_SOURCE})
  if(APPLE)
    set(DESKTOP_WINDOW_SOURCE src/webview/DesktopWindowMac.mm)
    set(DESKTOP_WINDOW_LIBRARIES "-framework AppKit")
    target_sources(inkstow-webview PRIVATE src/webview/DesktopServicesMac.mm)
  elseif(WIN32)
    set(DESKTOP_WINDOW_SOURCE src/webview/DesktopWindowWindows.cpp)
    set(DESKTOP_WINDOW_LIBRARIES shell32 ole32 uuid)
  else()
    set(DESKTOP_WINDOW_SOURCE src/webview/DesktopWindowLinux.cpp)
    pkg_check_modules(GTK REQUIRED IMPORTED_TARGET gtk+-3.0)
    set(DESKTOP_WINDOW_LIBRARIES PkgConfig::GTK)
  endif()
  target_sources(inkstow-webview PRIVATE ${DESKTOP_WINDOW_SOURCE})
  target_link_libraries(inkstow-webview PRIVATE ${DESKTOP_WINDOW_LIBRARIES})
  if(NOT WIN32 AND NOT APPLE)
    target_link_libraries(inkstow-webview PRIVATE PkgConfig::GIO)
  endif()
  add_dependencies(inkstow-webview webview-assets)
  target_include_directories(inkstow-webview PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_link_libraries(inkstow-webview PRIVATE webview-bridge webview-watch webview::core_static inkstow-warnings)
  if(APPLE)
    set_target_properties(inkstow-webview PROPERTIES MACOSX_BUNDLE TRUE
                                                 OBJCXX_STANDARD 20
                                                 OBJCXX_STANDARD_REQUIRED ON)
  endif()
endif() # INKSTOW_BUILD_WEBVIEW

include(CTest)
if(BUILD_TESTING)
  if(INKSTOW_BUILD_WEBVIEW)
    add_test(NAME webview-js-tests COMMAND "${NODE_EXECUTABLE}" --test editor.test.mjs
             WORKING_DIRECTORY "${WEBVIEW_ASSET_DIR}")
  endif()
  add_subdirectory(tests)
endif()
