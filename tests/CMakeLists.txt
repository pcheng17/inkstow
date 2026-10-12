add_executable(core-tests CoreTests.cpp)
target_link_libraries(core-tests PRIVATE core inkstow-warnings)
target_compile_definitions(core-tests
                           PRIVATE SAMPLE_VAULT="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sample"
                                   LINKS_VAULT="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/links")
add_test(NAME core-tests COMMAND core-tests)

add_executable(ui-tests UiTests.cpp ${PROJECT_SOURCE_DIR}/src/NoteEditor.cpp
                        ${PROJECT_SOURCE_DIR}/src/NoteTree.cpp
                        ${PROJECT_SOURCE_DIR}/src/SettingsStore.cpp
                        ${PROJECT_SOURCE_DIR}/src/Theme.cpp ${PROJECT_SOURCE_DIR}/src/Window.cpp)
target_link_libraries(ui-tests PRIVATE core Qt6::Widgets inkstow-warnings)
add_test(NAME ui-tests COMMAND ui-tests)
set_tests_properties(ui-tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
