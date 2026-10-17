# A missing bundle identifier causes NSOpenPanel to return Cancel without displaying
# when Inkstow runs from its .app bundle, even though the picker works in a CLI test.
execute_process(
  COMMAND /usr/libexec/PlistBuddy -c "Print :CFBundleIdentifier" "${INFO_PLIST}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE identifier
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT result EQUAL 0 OR NOT identifier STREQUAL "io.github.pcheng17.inkstow")
  message(FATAL_ERROR "Inkstow.app needs CFBundleIdentifier io.github.pcheng17.inkstow for the macOS folder picker")
endif()
