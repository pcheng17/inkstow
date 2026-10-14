add_executable(core-tests EXCLUDE_FROM_ALL CoreTests.cpp)
target_link_libraries(core-tests PRIVATE core inkstow-warnings)
target_compile_definitions(core-tests
                           PRIVATE SAMPLE_VAULT="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sample"
                                   LINKS_VAULT="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/links"
                                   BYTE_FIDELITY_VAULT="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/byte-fidelity")
add_test(NAME core-tests COMMAND core-tests)

add_executable(highlighter-tests EXCLUDE_FROM_ALL HighlighterTests.cpp
                                      ${PROJECT_SOURCE_DIR}/src/ui/MarkdownHighlighter.cpp
                                      ${PROJECT_SOURCE_DIR}/src/ui/Theme.cpp)
target_include_directories(highlighter-tests PRIVATE ${PROJECT_SOURCE_DIR}/src/ui)
# Theme.cpp also styles the QApplication, so Widgets is linked, but the tests
# run on a QGuiApplication.
target_link_libraries(highlighter-tests PRIVATE Qt6::Gui Qt6::Widgets inkstow-warnings)
add_test(NAME highlighter-tests COMMAND highlighter-tests)
set_tests_properties(highlighter-tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(portable-core-tests EXCLUDE_FROM_ALL PortableCoreTests.cpp)
target_link_libraries(portable-core-tests PRIVATE portable-core inkstow-warnings)
target_compile_definitions(portable-core-tests PRIVATE SAMPLE_VAULT="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sample")
add_test(NAME portable-core-tests COMMAND portable-core-tests)

# Build headless tests without pulling in the widget tests.
add_custom_target(unit-tests DEPENDS core-tests highlighter-tests portable-core-tests)
if(INKSTOW_BUILD_WEBVIEW)
  # CTest runs the JS test too; the CI test-build path must generate its assets.
  add_dependencies(unit-tests webview-assets)
  add_executable(webview-bridge-tests EXCLUDE_FROM_ALL WebviewBridgeTests.cpp)
  target_link_libraries(webview-bridge-tests PRIVATE webview-bridge inkstow-warnings)
  add_test(NAME webview-bridge-tests COMMAND webview-bridge-tests)
  add_dependencies(unit-tests webview-bridge-tests)

  add_executable(webview-navigation-tests EXCLUDE_FROM_ALL WebviewNavigationTests.cpp)
  target_include_directories(webview-navigation-tests PRIVATE ${PROJECT_SOURCE_DIR}/src/webview)
  target_link_libraries(webview-navigation-tests PRIVATE inkstow-warnings)
  add_test(NAME webview-navigation-tests COMMAND webview-navigation-tests)
  add_dependencies(unit-tests webview-navigation-tests)

  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_program(XVFB_RUN_EXECUTABLE xvfb-run)
    if(XVFB_RUN_EXECUTABLE)
      add_executable(webview-native-navigation-tests EXCLUDE_FROM_ALL
                     WebviewNativeNavigationTests.cpp
                     ${PROJECT_SOURCE_DIR}/src/webview/NavigationPolicyLinux.cpp)
      target_include_directories(webview-native-navigation-tests PRIVATE ${PROJECT_SOURCE_DIR}/src/webview)
      target_link_libraries(webview-native-navigation-tests PRIVATE webview::core_static inkstow-warnings)
      add_test(NAME webview-native-navigation-tests
               COMMAND "${XVFB_RUN_EXECUTABLE}" -a $<TARGET_FILE:webview-native-navigation-tests>)
      add_dependencies(unit-tests webview-native-navigation-tests)
    endif()
  endif()

  if(APPLE OR WIN32 OR XVFB_RUN_EXECUTABLE)
    add_executable(webview-conflict-window-tests EXCLUDE_FROM_ALL
                   WebviewConflictWindowTests.cpp
                   ${PROJECT_SOURCE_DIR}/${NAVIGATION_POLICY_SOURCE})
    add_dependencies(webview-conflict-window-tests webview-assets)
    target_include_directories(webview-conflict-window-tests PRIVATE
                               ${PROJECT_SOURCE_DIR}/src/webview
                               "${PROJECT_BINARY_DIR}/generated")
    target_link_libraries(webview-conflict-window-tests PRIVATE webview-bridge
                          webview::core_static inkstow-warnings)
    if(APPLE)
      set_target_properties(webview-conflict-window-tests PROPERTIES OBJCXX_STANDARD 17
                                                                OBJCXX_STANDARD_REQUIRED ON)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      add_test(NAME webview-conflict-window-tests
               COMMAND "${XVFB_RUN_EXECUTABLE}" -a $<TARGET_FILE:webview-conflict-window-tests>)
    else()
      add_test(NAME webview-conflict-window-tests COMMAND webview-conflict-window-tests)
    endif()
    add_dependencies(unit-tests webview-conflict-window-tests)
  endif()
endif()

# QTest delivers key presses through the shortcut map, as real typing does.
find_package(Qt6 6.4 REQUIRED COMPONENTS Test)
add_executable(ui-tests EXCLUDE_FROM_ALL UiTests.cpp NoteActionTests.cpp
                        ${PROJECT_SOURCE_DIR}/src/ui/MarkdownHighlighter.cpp
                        ${PROJECT_SOURCE_DIR}/src/ui/NoteEditor.cpp
                        ${PROJECT_SOURCE_DIR}/src/ui/NoteHeader.cpp
                        ${PROJECT_SOURCE_DIR}/src/ui/NoteTree.cpp
                        ${PROJECT_SOURCE_DIR}/src/ui/SettingsStore.cpp
                        ${PROJECT_SOURCE_DIR}/src/ui/Theme.cpp
                        ${PROJECT_SOURCE_DIR}/src/ui/Window.cpp)
target_include_directories(ui-tests PRIVATE ${PROJECT_SOURCE_DIR}/src/ui)
target_link_libraries(ui-tests PRIVATE core Qt6::Widgets Qt6::Test inkstow-warnings)
target_compile_definitions(ui-tests PRIVATE BYTE_FIDELITY_VAULT="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/byte-fidelity")
add_test(NAME ui-tests COMMAND ui-tests)
set_tests_properties(ui-tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
