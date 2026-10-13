cmake_minimum_required(VERSION 3.20)
project(Inkstow VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE)
  include(cmake/Apple.cmake)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets)
include(cmake/CompilerOptions.cmake)

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

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
