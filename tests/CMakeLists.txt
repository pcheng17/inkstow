add_executable(vault-tests VaultTests.cpp)
target_link_libraries(vault-tests PRIVATE vault-core inkstow-warnings)
target_compile_definitions(vault-tests
                           PRIVATE SAMPLE_VAULT="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sample"
                                   LINKS_VAULT="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/links")
add_test(NAME vault-core COMMAND vault-tests)

add_executable(ui-tests UiTests.cpp ${PROJECT_SOURCE_DIR}/src/Theme.cpp
                        ${PROJECT_SOURCE_DIR}/src/Window.cpp)
target_link_libraries(ui-tests PRIVATE vault-core Qt6::Widgets inkstow-warnings)
add_test(NAME ui-smoke COMMAND ui-tests)
set_tests_properties(ui-smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
