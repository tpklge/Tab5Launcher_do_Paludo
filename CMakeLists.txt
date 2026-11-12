cmake_minimum_required(VERSION 3.16)

# Extra component directories: our own components plus any from managed-components
set(EXTRA_COMPONENT_DIRS "components")

# The IDF 5.4 component manager serializes local override paths as absolute.
# Rebase that one generated lock entry when this checkout moves machines.
set(_tab5_lock "${CMAKE_CURRENT_SOURCE_DIR}/dependencies.lock")
if(EXISTS "${_tab5_lock}")
    file(READ "${_tab5_lock}" _tab5_lock_text)
    string(REGEX REPLACE "path: [^\n]+/components/espressif__usb_host_msc"
        "path: ${CMAKE_CURRENT_SOURCE_DIR}/components/espressif__usb_host_msc"
        _tab5_lock_rebased "${_tab5_lock_text}")
    if(NOT _tab5_lock_text STREQUAL _tab5_lock_rebased)
        file(WRITE "${_tab5_lock}" "${_tab5_lock_rebased}")
    endif()
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(PROJECT_VER "0.8.0-rc.2")
project(tab5_launcher VERSION 0.8.0)

# One FatFs ABI for every component. IDF 5.4.4 ships exFAT code but disables it
# unconditionally in ffconf.h; do not edit the SDK or managed dependencies.
set(TAB5_FAT_CONFIG "${CMAKE_BINARY_DIR}/tab5_fatfs_config.h")
file(WRITE "${TAB5_FAT_CONFIG}" "#ifndef __ASSEMBLER__\n#include \"$ENV{IDF_PATH}/components/fatfs/src/ffconf.h\"\n#ifndef CONFIG_FATFS_USE_LABEL\n#define CONFIG_FATFS_USE_LABEL 0\n#endif\n#undef FF_FS_EXFAT\n#define FF_FS_EXFAT 1\n#endif\n")
idf_build_set_property(COMPILE_OPTIONS "-include${TAB5_FAT_CONFIG}" APPEND)
