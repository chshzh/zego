#
# Copyright (c) 2026 Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

if(CONFIG_ZEGO_WIFI)
        get_filename_component(_BANNER_APP_NAME ${APPLICATION_SOURCE_DIR} NAME)
        zephyr_library()
        zephyr_library_sources(src/wifi.c)
        if(CONFIG_ZEGO_WIFI_MODE_SELECTOR)
                zephyr_library_sources(src/wifi_mode_selector.c)
        endif()
        zephyr_include_directories(src)
        zephyr_library_compile_definitions(ZEGO_BANNER_APP_NAME="${_BANNER_APP_NAME}")

        # -----------------------------------------------------------------------
        # APP_VERSION_STRING — release tag if on an exact tag, else <NCS>.99
        # -----------------------------------------------------------------------
        if(DEFINED APP_VERSION_STRING)
                zephyr_compile_definitions(APP_VERSION_STRING="${APP_VERSION_STRING}")
        elseif(DEFINED CONFIG_MEMFAULT_NCS_FW_VERSION AND NOT CONFIG_MEMFAULT_NCS_FW_VERSION STREQUAL "")
                zephyr_compile_definitions(APP_VERSION_STRING="${CONFIG_MEMFAULT_NCS_FW_VERSION}")
        else()
                # Read NCS version from west.yml — app dir first, then repo root
                if(EXISTS ${APPLICATION_SOURCE_DIR}/west.yml)
                        set(_WEST_YML ${APPLICATION_SOURCE_DIR}/west.yml)
                else()
                        set(_WEST_YML ${APPLICATION_SOURCE_DIR}/../west.yml)
                endif()
                file(STRINGS ${_WEST_YML} _WEST_NCS_LINE
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

endif()
