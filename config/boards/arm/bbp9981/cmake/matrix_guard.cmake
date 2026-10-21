# SPDX-License-Identifier: MIT
# Included from the board directory; preserve SDK source scope and ordering.
# Compile final-DT assertions for this controlled column-drive experiment.
zephyr_library_sources(custom_driver/matrix_isolation_guard.c)
execute_process(COMMAND git -C "${ZEPHYR_BASE}" rev-parse HEAD
    RESULT_VARIABLE _iso_git_result OUTPUT_VARIABLE _iso_zephyr_rev
    OUTPUT_STRIP_TRAILING_WHITESPACE)
string(LENGTH "${_iso_zephyr_rev}" _iso_rev_len)
if(NOT _iso_git_result EQUAL 0 OR NOT _iso_rev_len EQUAL 40)
    message(FATAL_ERROR "Cannot record actual Zephyr revision for matrix experiment")
endif()
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/custom_driver/matrix_isolation_guard.c"
    APPEND PROPERTY COMPILE_DEFINITIONS "BBP_ISO_ZEPHYR_REV=\"${_iso_zephyr_rev}\"")
message(STATUS "MATRIX_ISO col_hiz_20261017 Zephyr=${_iso_zephyr_rev}")
