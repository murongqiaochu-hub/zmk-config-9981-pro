#!/usr/bin/env python3
"""Run actual daily CMake, not just source-text checks. No firmware/SDK writes.

The removed controller guard is not a keyboard/mouse SDK pin. The retained
matrix-revision and diagnostic-target gates are tested as they actually exist.
Local lack of CMake is SKIP, never PASS; CI must have it.
"""
import itertools
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from daily_scope import ROOT, BOARD, blob, paths
from test_refactor_cmake import HARNESS, run


def trace(result, board, sdk):
    # Unlike the historic helper, do NOT hide mode/adapter/runtime events.
    return [line.replace(str(board), 'BOARD').replace(str(sdk), 'SDK')
            for line in result.stdout.splitlines() if line.startswith('-- EVENT')]


def diagnostics(temp):
    """Configure the real deferred matrix-prefix path; no ARM compilation."""
    project = temp / 'diag-project'
    project.mkdir()
    matrix = project / 'kscan_gpio_matrix.c'
    matrix.write_text('void daily_matrix_fixture(void) {}\n')
    # Override execute_process ONLY for board revision lookup. Compiler probing
    # occurs before this wrapper is installed. set_property remains the real API.
    text = r'''cmake_minimum_required(VERSION 3.20)
project(daily_diag C)
set(CONFIG_BOARD_BBP9981 ON)
set(CONFIG_BBP9981_MENU_DIAG ON)
set(CONFIG_A320_TAP_OUTPUT ON)
set(CONFIG_ZMK_USB ON)
set(CONFIG_ZMK_BLE ON)
set(CONFIG_A320_MICROTAP ON)
set(CONFIG_BBP9981_CURSOR_SPEED ON)
set(APPLICATION_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(ZEPHYR_BASE "${CMAKE_CURRENT_SOURCE_DIR}/zephyr")
function(zephyr_library)
endfunction()
function(zephyr_library_sources)
endfunction()
function(zephyr_library_include_directories)
endfunction()
function(execute_process)
  set(_iso_git_result 0 PARENT_SCOPE)
  set(_iso_zephyr_rev "dacab4875df72109b96cc8977547a0dc04875bcd" PARENT_SCOPE)
endfunction()
if(NOT FAILURE STREQUAL "missing_matrix")
  if(FAILURE STREQUAL "relative_matrix")
    add_library(matrix OBJECT kscan_gpio_matrix.c)
  else()
    add_library(matrix OBJECT "${CMAKE_CURRENT_SOURCE_DIR}/kscan_gpio_matrix.c")
  endif()
  set_property(GLOBAL PROPERTY ZEPHYR_LIBS matrix)
endif()
set(CMAKE_CURRENT_SOURCE_DIR "${BOARD_ROOT}")
include("${BOARD_ROOT}/CMakeLists.txt")
set(CMAKE_CURRENT_SOURCE_DIR "${CMAKE_SOURCE_DIR}")
function(check_deferred)
  get_source_file_property(options "${CMAKE_SOURCE_DIR}/kscan_gpio_matrix.c" COMPILE_OPTIONS)
  if(NOT "${options}" MATCHES "menu_diag_matrix.h")
    message(FATAL_ERROR "daily matrix diagnostic prefix was not applied")
  endif()
  get_source_file_property(endpoints "${CMAKE_SOURCE_DIR}/src/endpoints.c" COMPILE_OPTIONS)
  if(NOT "${endpoints}" MATCHES "menu_diag_endpoints.h")
    message(FATAL_ERROR "daily endpoints diagnostic prefix was not applied")
  endif()
  message(STATUS "DAILY actual diagnostic prefixes applied")
endfunction()
cmake_language(DEFER CALL check_deferred)
'''
    (project / 'CMakeLists.txt').write_text(text)
    for failure in ('', 'missing_matrix', 'relative_matrix'):
        result = subprocess.run(['cmake', '-S', str(project), '-B', str(temp / ('diag-build-' + (failure or 'ok'))),
                                 '-DBOARD_ROOT=' + str(ROOT / BOARD), '-DFAILURE=' + failure],
                                text=True, capture_output=True)
        if failure:
            message = 'not found' if failure == 'missing_matrix' else 'absolute source path'
            assert result.returncode != 0 and message in result.stderr, (failure, result.stdout, result.stderr)
        else:
            assert result.returncode == 0 and 'DAILY actual diagnostic prefixes applied' in result.stdout, (result.stdout, result.stderr)


def main():
    if shutil.which('cmake') is None:
        assert not os.environ.get('CI'), 'CI must execute actual daily CMake; cmake unavailable'
        print('SKIP DAILY CMake locally: cmake unavailable; actual matrix/diagnostic gates NOT executed (mandatory in CI)', flush=True)
        return False
    with tempfile.TemporaryDirectory(prefix='daily-cmake-') as directory:
        temp = Path(directory); old = temp / 'frozen-board'; sdk = temp / 'sdk'
        for name in paths(BOARD):
            target = old / Path(name).relative_to(BOARD)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(blob(name))
        harness = temp / 'contract.cmake'; harness.write_text(HARNESS)
        # Each current source/prefix/order event must equal actual 11ee89e
        # keyboard/mouse configuration, without dropping any event from the trace.
        for tap, usb, ble, micro, speed in itertools.product((0, 1), repeat=5):
            flags = dict(CONFIG_A320_TAP_OUTPUT=tap, CONFIG_ZMK_USB=usb, CONFIG_ZMK_BLE=ble,
                         CONFIG_A320_MICROTAP=micro, CONFIG_BBP9981_CURSOR_SPEED=speed)
            before = run(harness, old, sdk, dict(flags, CONFIG_BBP9981_GAMEPAD=0))
            after = run(harness, ROOT / BOARD, sdk, flags)
            assert before.returncode == after.returncode == 0, (flags, before.stderr, after.stderr)
            expected = trace(before, old, sdk)
            actual = trace(after, ROOT / BOARD, sdk)
            assert expected == actual, (flags, expected, actual)
            assert not any('gamepad' in line.lower() for line in actual), flags
            assert actual.count('-- EVENT source custom_driver/behavior_cursor_speed.c') == 1
            assert actual.count('-- EVENT source custom_driver/trackpad_cursor_speed.c') == speed
            # A stale external define cannot resurrect a deleted source/interface.
            poison = run(harness, ROOT / BOARD, sdk, dict(flags, CONFIG_BBP9981_GAMEPAD=1))
            assert poison.returncode == 0 and trace(poison, ROOT / BOARD, sdk) == actual, poison.stderr
        flags = dict(CONFIG_A320_TAP_OUTPUT=1, CONFIG_ZMK_USB=1, CONFIG_ZMK_BLE=1,
                     CONFIG_A320_MICROTAP=1, CONFIG_BBP9981_CURSOR_SPEED=1)
        bad = run(harness, ROOT / BOARD, sdk, flags, 'missing_revision')
        assert bad.returncode != 0 and 'Cannot record actual Zephyr revision' in bad.stderr, bad.stderr
        diagnostics(temp)
    print('PASS DAILY actual CMake32: complete current source/prefix traces equal frozen no-controller case; stale flag cannot revive removed code; actual deferred diagnostics +3 negative gates', flush=True)
    return True


if __name__ == '__main__':
    main()
