#
# Copyright (c) 2026 Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

if(CONFIG_ZEGO_WIFI)
        get_filename_component(_BANNER_APP_NAME ${APPLICATION_SOURCE_DIR} NAME)
        zephyr_library()
        zephyr_library_sources(src/wifi.c src/wifi_mode_selector.c)
        zephyr_include_directories(src)
        zephyr_library_compile_definitions(ZEGO_BANNER_APP_NAME="${_BANNER_APP_NAME}")

        # -----------------------------------------------------------------------
        # APP_VERSION_STRING — release tag if on an exact tag, else <NCS>.99
        # -----------------------------------------------------------------------
        if(DEFINED APP_VERSION_STRING)
                zephyr_compile_definitions(APP_VERSION_STRING="${APP_VERSION_STRING}")
        else()
                # Read NCS version from the application's west.yml
                file(STRINGS ${APPLICATION_SOURCE_DIR}/west.yml _WEST_NCS_LINE
                        REGEX "revision: v[0-9]+\\.[0-9]+\\.[0-9]+")
                list(GET _WEST_NCS_LINE 0 _WEST_NCS_LINE)
                string(REGEX REPLACE ".*revision: (v[0-9]+\\.[0-9]+\\.[0-9]+).*" "\\1"
                        _NCS_VERSION "${_WEST_NCS_LINE}")

                execute_process(
                        COMMAND git describe --tags --exact-match
                        WORKING_DIRECTORY ${APPLICATION_SOURCE_DIR}
                        OUTPUT_VARIABLE _GIT_EXACT_TAG
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET
                        RESULT_VARIABLE _GIT_EXACT_TAG_RESULT
                )
                if(_GIT_EXACT_TAG_RESULT EQUAL 0 AND NOT _GIT_EXACT_TAG STREQUAL "")
                        set(_APP_VERSION_STRING "${_GIT_EXACT_TAG}")
                else()
                        set(_APP_VERSION_STRING "${_NCS_VERSION}.99")
                endif()
                zephyr_compile_definitions(APP_VERSION_STRING="${_APP_VERSION_STRING}")
        endif()

        # -----------------------------------------------------------------------
        # PRD_VERSION — from docs/pm-prd/PRD.md (graceful fallback to "unknown")
        # -----------------------------------------------------------------------
        file(STRINGS ${APPLICATION_SOURCE_DIR}/docs/pm-prd/PRD.md _PRD_LINES
                REGEX "\\| Version +\\|")
        if(_PRD_LINES)
                list(GET _PRD_LINES 0 _PRD_LINE)
                string(REGEX REPLACE ".*\\| Version +\\| ([^ |]+) \\|.*" "\\1"
                        _PRD_VERSION "${_PRD_LINE}")
        else()
                set(_PRD_VERSION "unknown")
        endif()
        zephyr_compile_definitions(PRD_VERSION="${_PRD_VERSION}")

        # -----------------------------------------------------------------------
        # SPECS_VERSION — from docs/dev-specs/overview.md (graceful fallback)
        # -----------------------------------------------------------------------
        file(STRINGS ${APPLICATION_SOURCE_DIR}/docs/dev-specs/overview.md _SPECS_LINES
                REGEX "\\| Version +\\|")
        if(_SPECS_LINES)
                list(GET _SPECS_LINES 0 _SPECS_LINE)
                string(REGEX REPLACE ".*\\| Version +\\| ([^ |]+) \\|.*" "\\1"
                        _SPECS_VERSION "${_SPECS_LINE}")
        else()
                set(_SPECS_VERSION "unknown")
        endif()
        zephyr_compile_definitions(SPECS_VERSION="${_SPECS_VERSION}")
endif()
