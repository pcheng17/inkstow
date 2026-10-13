add_executable(core-tests EXCLUDE_FROM_ALL CoreTests.cpp)
target_link_libraries(core-tests PRIVATE core inkstow-warnings)
target_compile_definitions(core-tests
                           PRIVATE SAMPLE_VAULT="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sample"
                                   LINKS_VAULT="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/links")
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

# Build both headless test executables without pulling in the widget tests.
add_custom_target(unit-tests DEPENDS core-tests highlighter-tests)

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
add_test(NAME ui-tests COMMAND ui-tests)
set_tests_properties(ui-tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
