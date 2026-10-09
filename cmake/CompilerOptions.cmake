add_library(inkstow-warnings INTERFACE)

target_compile_options(inkstow-warnings INTERFACE
  $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:
    -Wall -Wextra -Wpedantic -Wshadow>
  $<$<CXX_COMPILER_ID:MSVC>:
    /W4>
)
