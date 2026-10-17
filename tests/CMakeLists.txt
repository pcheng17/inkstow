add_executable(inkstow-tests EXCLUDE_FROM_ALL
             DoctestMain.cpp VaultTests.cpp SessionTests.cpp GuardedReplaceTests.cpp)
target_link_libraries(inkstow-tests PRIVATE portable-core doctest::doctest inkstow-warnings)
target_sources(inkstow-tests PRIVATE WebviewBridgeTests.cpp WebviewWatchTests.cpp
                                     WebviewNavigationTests.cpp)
target_link_libraries(inkstow-tests PRIVATE webview-bridge webview-watch)
target_include_directories(inkstow-tests PRIVATE ${PROJECT_SOURCE_DIR}/src/desktop)
target_compile_definitions(inkstow-tests PRIVATE SAMPLE_VAULT="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sample")
add_test(NAME inkstow-tests COMMAND inkstow-tests --test-case-exclude=NavigationPolicy*,WebviewWindow*)

# Build the single C++ test runner and its frontend assets before CTest.
add_custom_target(unit-tests DEPENDS inkstow-tests)
# CTest runs the JS test too; the CI test-build path must generate its assets.
add_dependencies(unit-tests webview-assets)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_program(XVFB_RUN_EXECUTABLE xvfb-run)
  if(XVFB_RUN_EXECUTABLE)
    target_sources(inkstow-tests PRIVATE WebviewNativeNavigationTests.cpp
                                        ${NAVIGATION_POLICY_SOURCE})
    target_link_libraries(inkstow-tests PRIVATE webview::core_static)
    add_test(NAME inkstow-native-navigation-tests
             COMMAND "${XVFB_RUN_EXECUTABLE}" -a $<TARGET_FILE:inkstow-tests>
                     --test-case=NavigationPolicy*)
  endif()
endif()

if(APPLE OR WIN32 OR XVFB_RUN_EXECUTABLE)
  target_sources(inkstow-tests PRIVATE WebviewConflictWindowTests.cpp ${DESKTOP_WINDOW_SOURCE})
  add_dependencies(inkstow-tests webview-assets)
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(inkstow-tests PRIVATE ${NAVIGATION_POLICY_SOURCE})
  endif()
  target_include_directories(inkstow-tests PRIVATE "${PROJECT_BINARY_DIR}/generated")
  target_link_libraries(inkstow-tests PRIVATE webview::core_static ${DESKTOP_WINDOW_LIBRARIES})
  if(APPLE)
    target_sources(inkstow-tests PRIVATE WebviewDesktopMac.mm
                                         ${PROJECT_SOURCE_DIR}/src/desktop/DesktopServices.cpp
                                         ${PROJECT_SOURCE_DIR}/src/desktop/DesktopServicesMac.mm)
    target_link_libraries(inkstow-tests PRIVATE "-framework AppKit")
    set_target_properties(inkstow-tests PROPERTIES OBJCXX_STANDARD 20
                                               OBJCXX_STANDARD_REQUIRED ON)
  endif()
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME webview-conflict-window-tests
             COMMAND "${XVFB_RUN_EXECUTABLE}" -a $<TARGET_FILE:inkstow-tests>
                     --test-case=WebviewWindow*)
  else()
    add_test(NAME webview-conflict-window-tests
             COMMAND inkstow-tests --test-case=WebviewWindow*)
  endif()
endif()
