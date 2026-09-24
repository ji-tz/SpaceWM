cmake_minimum_required(VERSION 3.21)
project(SpaceWM VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Gui Test)

if(NOT WIN32)
  message(FATAL_ERROR "SpaceWM targets Windows only")
endif()

# ---------------------------------------------------------------------------
# Core library (no main) — shared by the app and the test binaries.
# ---------------------------------------------------------------------------
add_library(spacewm_core STATIC
  src/core/space/Space.h
  src/core/space/SpaceManager.h
  src/core/space/SpaceManager.cpp
  src/core/monitor/MonitorInfo.h
  src/core/monitor/MonitorInfo.cpp
  src/core/window/CloakController.h
  src/core/window/CloakController.cpp
  src/core/window/WindowTracker.h
  src/core/window/WindowTracker.cpp
  src/core/capture/ThumbnailCapture.h
  src/core/capture/ThumbnailCapture.cpp
  src/hotkeys/HotkeyManager.h
  src/hotkeys/HotkeyManager.cpp
  src/ui/overview/OverviewWindow.h
  src/ui/overview/OverviewWindow.cpp
  src/ui/overview/OverviewHost.h
  src/ui/overview/OverviewHost.cpp
  src/ui/preview/SpaceCardWidget.h
  src/ui/preview/SpaceCardWidget.cpp
  src/ui/preview/WindowPreviewWidget.h
  src/ui/preview/WindowPreviewWidget.cpp
  src/ui/effects/SwitchFlashOverlay.h
  src/ui/effects/SwitchFlashOverlay.cpp
  src/ui/tray/TrayIcon.h
  src/ui/tray/TrayIcon.cpp
)

target_include_directories(spacewm_core PUBLIC src)

target_link_libraries(spacewm_core PUBLIC
  Qt6::Widgets
  Qt6::Gui
  dwmapi
  user32
  gdi32
  shell32
  ole32
  shcore
)

target_compile_definitions(spacewm_core PUBLIC
  UNICODE
  _UNICODE
  NOMINMAX
  WIN32_LEAN_AND_MEAN
)

if(MSVC)
  target_compile_options(spacewm_core PRIVATE /W4 /permissive- /Zc:__cplusplus)
endif()

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
add_executable(SpaceWM WIN32
  src/main.cpp
)

target_link_libraries(SpaceWM PRIVATE spacewm_core)

# ---------------------------------------------------------------------------
# Runtime deployment:
#   SpaceWM/lib/     staging for Qt DLLs, plugins, MSVC CRT
#   SpaceWM/build/   SpaceWM.exe + copy of lib/
# ---------------------------------------------------------------------------
set(SPACEWM_LIB_DIR "${CMAKE_SOURCE_DIR}/lib")

find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${Qt6_DIR}/../../../bin")

set(SPACEWM_CRT_DIR "")
if(MSVC)
  file(GLOB _crt_candidates
    "$ENV{VCINSTALLDIR}/Redist/MSVC/*/x64/Microsoft.VC*.CRT"
    "C:/Program Files/Microsoft Visual Studio/*/*/VC/Redist/MSVC/*/x64/Microsoft.VC*.CRT"
    "C:/Program Files (x86)/Microsoft Visual Studio/*/*/VC/Redist/MSVC/*/x64/Microsoft.VC*.CRT"
  )
  if(_crt_candidates)
    list(SORT _crt_candidates)
    list(REVERSE _crt_candidates)
    list(GET _crt_candidates 0 SPACEWM_CRT_DIR)
  endif()
endif()

if(WINDEPLOYQT_EXECUTABLE)
  add_custom_command(TARGET SpaceWM POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SPACEWM_LIB_DIR}"
    COMMAND "${WINDEPLOYQT_EXECUTABLE}"
      --release
      --dir "${SPACEWM_LIB_DIR}"
      --no-translations
      --no-system-d3d-compiler
      --no-opengl-sw
      $<TARGET_FILE:SpaceWM>
    COMMAND "${CMAKE_COMMAND}"
      -DCRT_DIR=${SPACEWM_CRT_DIR}
      -DDEST_DIR=${SPACEWM_LIB_DIR}
      -P "${CMAKE_SOURCE_DIR}/cmake/CopyCrt.cmake"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${SPACEWM_LIB_DIR}"
      "$<TARGET_FILE_DIR:SpaceWM>"
    COMMENT "Deploy runtime libs: windeployqt -> lib/ -> copy to build/"
    VERBATIM
  )
else()
  message(WARNING "windeployqt not found; runtime libs will not be staged into lib/")
endif()

# ---------------------------------------------------------------------------
# Tests (Qt Test)
# ---------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)
