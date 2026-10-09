cmake_minimum_required(VERSION 3.20)
project(Inkstow VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
  include(cmake/Apple.cmake)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Widgets)

add_library(vault-core src/Vault.cpp src/Session.cpp)
target_include_directories(vault-core PUBLIC src)
target_link_libraries(vault-core PUBLIC Qt6::Core)

add_executable(vault-inspect src/VaultInspect.cpp)
target_link_libraries(vault-inspect PRIVATE vault-core)

add_executable(inkstow src/main.cpp src/Theme.cpp src/Window.cpp)
target_link_libraries(inkstow PRIVATE vault-core Qt6::Widgets)

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
