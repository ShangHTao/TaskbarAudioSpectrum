if(NOT DEFINED INPUT_ROOT OR NOT DEFINED OUTPUT_FILE OR
   NOT DEFINED TAS_RELEASE_VERSION)
  message(FATAL_ERROR
    "INPUT_ROOT, OUTPUT_FILE and TAS_RELEASE_VERSION are required")
endif()

set(parts
  src/windhawk/metadata.cpp
  src/windhawk/includes.h
  include/taskbar_audio_spectrum/platform.h
  include/taskbar_audio_spectrum/settings.h
  include/taskbar_audio_spectrum/spectrum_analysis.h
  include/taskbar_audio_spectrum/host.h
  include/taskbar_audio_spectrum/audio_capture.h
  include/taskbar_audio_spectrum/search_locator.h
  include/taskbar_audio_spectrum/overlay_window.h
  include/taskbar_audio_spectrum/application.h
  src/runtime_context.h
  src/registry_change_watcher.h
  src/settings.cpp
  src/spectrum_analysis.cpp
  src/audio_capture.cpp
  src/search_locator.cpp
  src/overlay_window.cpp
  src/application.cpp
  src/windhawk/windhawk_host.cpp
  src/windhawk/tool_process.cpp)

# Shared runtime sources keep the standalone Log() API. Only the generated
# Windhawk source uses Wh_Log() directly so Windhawk can preserve each call
# site's line and function information without changing standalone file logs.
set(windhawk_log_parts
  src/settings.cpp
  src/spectrum_analysis.cpp
  src/audio_capture.cpp
  src/search_locator.cpp
  src/overlay_window.cpp
  src/application.cpp)

set(output "")
foreach(part IN LISTS parts)
  file(READ "${INPUT_ROOT}/${part}" part_content ENCODING UTF-8)
  string(REPLACE "\r\n" "\n" part_content "${part_content}")
  set(skip_line FALSE)
  while(NOT part_content STREQUAL "")
    string(FIND "${part_content}" "\n" line_end)
    if(line_end EQUAL -1)
      set(line "${part_content}")
      set(part_content "")
    else()
      string(SUBSTRING "${part_content}" 0 ${line_end} line)
      math(EXPR next_line "${line_end} + 1")
      string(SUBSTRING "${part_content}" ${next_line} -1 part_content)
    endif()

    if("${line}" MATCHES "^[ \\t]*// TAS_WINDHAWK_EXCLUDE_BEGIN$")
      if(skip_line)
        message(FATAL_ERROR "Nested Windhawk exclusion in ${part}")
      endif()
      set(skip_line TRUE)
    elseif("${line}" MATCHES "^[ \\t]*// TAS_WINDHAWK_EXCLUDE_END$")
      if(NOT skip_line)
        message(FATAL_ERROR "Unmatched Windhawk exclusion end in ${part}")
      endif()
      set(skip_line FALSE)
    elseif(skip_line)
      continue()
    elseif("${line}" STREQUAL "#pragma once")
      continue()
    elseif(NOT part STREQUAL "src/windhawk/includes.h" AND
           "${line}" MATCHES "^#include[ \\t]")
      continue()
    else()
      if(part IN_LIST windhawk_log_parts)
        string(REPLACE "Log(" "Wh_Log(" line "${line}")
      endif()
      string(APPEND output "${line}\n")
    endif()
  endwhile()
  if(skip_line)
    message(FATAL_ERROR "Unclosed Windhawk exclusion in ${part}")
  endif()
  string(APPEND output "\n")
endforeach()

string(REPLACE "__TAS_VERSION__" "${TAS_RELEASE_VERSION}" output "${output}")
string(REGEX REPLACE "\n+$" "" output "${output}")
string(APPEND output "\n")
get_filename_component(output_directory "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${output_directory}")
file(WRITE "${OUTPUT_FILE}" "${output}")
