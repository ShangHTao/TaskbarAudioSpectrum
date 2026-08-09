cmake_minimum_required(VERSION 3.25)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" TAS_VERSION LIMIT_COUNT 1)
string(STRIP "${TAS_VERSION}" TAS_VERSION)
if(NOT TAS_VERSION MATCHES "^[0-9]+\.[0-9]+\.[0-9]+$")
  message(FATAL_ERROR "VERSION must contain major.minor.patch")
endif()

project(TaskbarAudioSpectrum VERSION ${TAS_VERSION} LANGUAGES CXX)

include(CTest)
option(TAS_BUILD_DIAGNOSTICS "Build taskbar diagnostic utilities" OFF)
option(TAS_BUILD_WINDHAWK "Generate and build Windhawk targets" OFF)
set(TAS_WINDHAWK_ROOT "$ENV{WINDHAWK_COMPILER_ROOT}" CACHE PATH
    "Windhawk compiler directory containing bin/clang++.exe")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT MSVC OR MSVC_VERSION LESS 1930)
  message(FATAL_ERROR
    "Taskbar Audio Spectrum requires the Visual Studio 2022 MSVC toolchain")
endif()

set(TAS_RUNTIME_SOURCES
  src/runtime_context.h
  src/registry_change_watcher.h
  src/json_config.cpp
  src/settings.cpp
  src/spectrum_analysis.cpp
  src/audio_capture.cpp
  src/search_locator.cpp
  src/overlay_window.cpp
  src/application.cpp
  src/standalone_host.cpp)

add_library(tas_core STATIC ${TAS_RUNTIME_SOURCES})
target_include_directories(tas_core PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_definitions(tas_core PUBLIC
  UNICODE _UNICODE WINVER=0x0A00 _WIN32_WINNT=0x0A00 _WIN32_IE=0x0A00)
target_link_libraries(tas_core PUBLIC
  ole32 oleaut32 uuid gdi32 advapi32 user32 shell32 dwmapi)

target_compile_options(tas_core PRIVATE /W4 /utf-8 /permissive-)

add_executable(TaskbarAudioSpectrum WIN32 src/standalone_main.cpp)
target_link_libraries(TaskbarAudioSpectrum PRIVATE tas_core)

if(BUILD_TESTING)
  add_executable(tas_self_test tests/self_test.cpp)
  target_link_libraries(tas_self_test PRIVATE tas_core)
  add_test(NAME tas_self_test COMMAND tas_self_test)
  add_executable(tas_json_config_test tests/json_config_test.cpp)
  target_link_libraries(tas_json_config_test PRIVATE tas_core)
  add_test(NAME tas_json_config_test COMMAND tas_json_config_test)
  add_executable(tas_configuration_schema_test
    tests/configuration_schema_test.cpp)
  target_link_libraries(tas_configuration_schema_test PRIVATE tas_core)
  add_test(NAME tas_configuration_schema_test
    COMMAND tas_configuration_schema_test)
  foreach(test_target IN ITEMS tas_self_test tas_json_config_test
                               tas_configuration_schema_test)
    set_tests_properties(${test_target} PROPERTIES
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" TIMEOUT 30)
    target_compile_options(${test_target} PRIVATE /W4 /utf-8 /permissive-)
  endforeach()
endif()

if(TAS_BUILD_DIAGNOSTICS)
  add_executable(tas_window_probe diagnostics/window_probe.cpp)
  add_executable(tas_uia_probe diagnostics/uia_probe.cpp)
  foreach(probe IN ITEMS tas_window_probe tas_uia_probe)
    target_include_directories(${probe} PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_definitions(${probe} PRIVATE
      UNICODE _UNICODE WINVER=0x0A00 _WIN32_WINNT=0x0A00)
  endforeach()
  foreach(probe IN ITEMS tas_window_probe tas_uia_probe)
    target_compile_options(${probe} PRIVATE /W4 /utf-8 /permissive-)
  endforeach()
  target_link_libraries(tas_window_probe PRIVATE user32)
  target_link_libraries(tas_uia_probe PRIVATE ole32 oleaut32 uuid user32)
endif()

set(TAS_WINDHAWK_SOURCE
    "${CMAKE_CURRENT_BINARY_DIR}/generated/taskbar-audio-spectrum.wh.cpp")
add_custom_command(
  OUTPUT "${TAS_WINDHAWK_SOURCE}"
  COMMAND "${CMAKE_COMMAND}"
    -DINPUT_ROOT:PATH=${CMAKE_CURRENT_SOURCE_DIR}
    -DOUTPUT_FILE:FILEPATH=${TAS_WINDHAWK_SOURCE}
    -DTAS_RELEASE_VERSION:STRING=${PROJECT_VERSION}
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateWindhawk.cmake"
  DEPENDS
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
    src/runtime_context.h src/registry_change_watcher.h
    src/settings.cpp src/spectrum_analysis.cpp src/audio_capture.cpp
    src/search_locator.cpp src/overlay_window.cpp src/application.cpp
    src/windhawk/windhawk_host.cpp src/windhawk/tool_process.cpp
    cmake/GenerateWindhawk.cmake
  VERBATIM)
add_custom_target(tas_windhawk_source DEPENDS "${TAS_WINDHAWK_SOURCE}")

if(TAS_BUILD_WINDHAWK)
  if(NOT EXISTS "${TAS_WINDHAWK_ROOT}/bin/clang++.exe")
    message(FATAL_ERROR "TAS_BUILD_WINDHAWK requires TAS_WINDHAWK_ROOT")
  endif()
  get_filename_component(TAS_WINDHAWK_INSTALL_ROOT "${TAS_WINDHAWK_ROOT}" DIRECTORY)
  foreach(arch IN ITEMS x86 x64)
    if(arch STREQUAL "x86")
      set(triple i686-w64-mingw32)
      set(engine_dir 32)
    else()
      set(triple x86_64-w64-mingw32)
      set(engine_dir 64)
    endif()
    file(GLOB windhawk_libraries
      "${TAS_WINDHAWK_INSTALL_ROOT}/Engine/*/${engine_dir}/windhawk.lib")
    if(NOT windhawk_libraries)
      message(FATAL_ERROR "Windhawk ${arch} import library was not found")
    endif()
    list(SORT windhawk_libraries ORDER DESCENDING)
    list(GET windhawk_libraries 0 windhawk_library)
    set(output "${CMAKE_CURRENT_BINARY_DIR}/taskbar-audio-spectrum-${arch}.dll")
    add_custom_command(
      OUTPUT "${output}"
      COMMAND "${TAS_WINDHAWK_ROOT}/bin/clang++.exe"
          -std=c++23 -target ${triple} -shared -static-libgcc -static-libstdc++
        -DUNICODE -D_UNICODE -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00
        -D_WIN32_IE=0x0A00 -DWH_MOD -DWH_EDITING -include windhawk_api.h
        -I "${TAS_WINDHAWK_ROOT}/include" "${TAS_WINDHAWK_SOURCE}"
        "${windhawk_library}" -lole32 -loleaut32 -luuid -lgdi32
        -ladvapi32 -lshell32 -ldwmapi -o "${output}"
      DEPENDS tas_windhawk_source "${TAS_WINDHAWK_SOURCE}"
      VERBATIM)
    add_custom_target(tas_windhawk_${arch} ALL DEPENDS "${output}")
  endforeach()
endif()

install(TARGETS TaskbarAudioSpectrum RUNTIME DESTINATION .)
install(FILES spectrum.example.json DESTINATION . RENAME spectrum.json)
install(FILES README.md LICENSE DESTINATION .)

set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_FILE_NAME "TaskbarAudioSpectrum-v${PROJECT_VERSION}-win64")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
include(CPack)
