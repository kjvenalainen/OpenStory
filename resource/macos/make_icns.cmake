# Convert the Windows icon art into the ICNS resource used by OpenStory.app.
#
# Usage:
#   cmake -DICON_SRC=<.ico|.png> -DICNS_OUT=<out.icns> -DWORK_DIR=<scratch dir>
#         -P make_icns.cmake

foreach(_required ICON_SRC ICNS_OUT WORK_DIR)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "make_icns.cmake: -D${_required}=... is required")
    endif()
endforeach()

if(NOT EXISTS "${ICON_SRC}")
    message(FATAL_ERROR "make_icns.cmake: source icon not found: ${ICON_SRC}")
endif()

find_program(SIPS_EXECUTABLE sips)
if(NOT SIPS_EXECUTABLE)
    message(FATAL_ERROR "make_icns.cmake: sips is not available")
endif()

get_filename_component(_out_dir "${ICNS_OUT}" DIRECTORY)
file(MAKE_DIRECTORY "${_out_dir}")

execute_process(
    COMMAND "${SIPS_EXECUTABLE}" -s format icns "${ICON_SRC}" --out "${ICNS_OUT}"
    RESULT_VARIABLE _rc
    OUTPUT_QUIET
    ERROR_VARIABLE _err)

if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "make_icns.cmake: sips failed to create ${ICNS_OUT}: ${_err}")
endif()

message(STATUS "make_icns.cmake: wrote ${ICNS_OUT}")
