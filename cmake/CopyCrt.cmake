# Copy MSVC CRT DLLs from the redistributable folder into DEST_DIR.
# Invoked as: cmake -DCRT_DIR=... -DDEST_DIR=... -P CopyCrt.cmake
# Safe no-op when CRT_DIR is empty or missing.

if(NOT DEFINED DEST_DIR OR DEST_DIR STREQUAL "")
  return()
endif()

if(NOT DEFINED CRT_DIR OR CRT_DIR STREQUAL "" OR NOT EXISTS "${CRT_DIR}")
  message(STATUS "CopyCrt: no CRT dir (${CRT_DIR}); skipping")
  return()
endif()

file(GLOB _dlls "${CRT_DIR}/*.dll")
foreach(_dll IN LISTS _dlls)
  file(COPY "${_dll}" DESTINATION "${DEST_DIR}")
endforeach()

list(LENGTH _dlls _n)
message(STATUS "CopyCrt: copied ${_n} DLL(s) from ${CRT_DIR} -> ${DEST_DIR}")
