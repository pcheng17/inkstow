cmake_minimum_required(VERSION 3.20)
project(Inkstow VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Homebrew's Qt is not on CMake's default search path.
if(APPLE AND NOT Qt6_DIR)
  find_program(BREW_EXECUTABLE brew)
  if(BREW_EXECUTABLE)
    execute_process(COMMAND "${BREW_EXECUTABLE}" --prefix qt
                    OUTPUT_VARIABLE BREW_QT_PREFIX
                    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(IS_DIRECTORY "${BREW_QT_PREFIX}")
      list(APPEND CMAKE_PREFIX_PATH "${BREW_QT_PREFIX}")
    endif()
  endif()
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Widgets)

add_library(vault-core cpp/vault.cpp cpp/session.cpp)
target_include_directories(vault-core PUBLIC cpp)
target_link_libraries(vault-core PUBLIC Qt6::Core)

add_executable(vault-inspect cpp/vault_inspect.cpp)
target_link_libraries(vault-inspect PRIVATE vault-core)

add_executable(inkstow cpp/main.cpp cpp/theme.cpp cpp/window.cpp)
target_link_libraries(inkstow PRIVATE vault-core Qt6::Widgets)

include(CTest)
if(BUILD_TESTING)
  add_executable(vault-tests tests/vault_tests.cpp)
  target_link_libraries(vault-tests PRIVATE vault-core)
  target_compile_definitions(vault-tests PRIVATE SAMPLE_VAULT="${CMAKE_SOURCE_DIR}/tests/fixtures/sample")
  add_test(NAME vault-core COMMAND vault-tests)

  add_executable(ui-tests tests/ui_tests.cpp cpp/theme.cpp cpp/window.cpp)
  target_link_libraries(ui-tests PRIVATE vault-core Qt6::Widgets)
  add_test(NAME ui-smoke COMMAND ui-tests)
  set_tests_properties(ui-smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
