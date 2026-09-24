# SpaceWM automated tests — one binary per feature area, registered with CTest.

set(SPACEWM_TEST_NAMES
  test_cloak
  test_monitors
  test_space_manager
  test_window_tracker
  test_thumbnail
  test_hotkeys
  test_space_card
  test_overview
  test_overview_host
  test_window_placement
  test_flash_overlay
)

set(SPACEWM_TEST_EXES "")

foreach(name IN LISTS SPACEWM_TEST_NAMES)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE spacewm_core Qt6::Test Qt6::Widgets Qt6::Gui)
  target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
  if(MSVC)
    target_compile_options(${name} PRIVATE /W4 /permissive-)
  endif()
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES TIMEOUT 60)
  list(APPEND SPACEWM_TEST_EXES $<TARGET_FILE:${name}>)
endforeach()

# Deploy Qt runtime ONCE into tests/ after all test binaries link.
# Per-target windeployqt races on the same plugin files under parallel Ninja.
if(WINDEPLOYQT_EXECUTABLE)
  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/.tests_deployed
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}"
    COMMAND "${WINDEPLOYQT_EXECUTABLE}"
      --release --no-translations --no-system-d3d-compiler --no-opengl-sw
      ${SPACEWM_TEST_EXES}
    COMMAND "${CMAKE_COMMAND}" -E touch ${CMAKE_CURRENT_BINARY_DIR}/.tests_deployed
    DEPENDS ${SPACEWM_TEST_NAMES}
    COMMENT "Deploy Qt runtime for tests"
    VERBATIM
  )
  add_custom_target(spacewm_test_deploy ALL
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/.tests_deployed
  )
endif()
