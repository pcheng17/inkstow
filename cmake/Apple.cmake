# Homebrew's Qt is not on CMake's default search path.
if(NOT Qt6_DIR)
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
