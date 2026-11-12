cmake_minimum_required(VERSION 3.16)
project(tab5_ui_regression C CXX)
set(CMAKE_C_STANDARD 99)
set(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(LV_CONF_SKIP ON CACHE BOOL "" FORCE)
set(LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
add_compile_definitions(LV_CONF_SKIP=1 LV_USE_LODEPNG=1 LV_USE_TJPGD=1 LV_COLOR_DEPTH=32 LV_MEM_SIZE=8388608
  LV_USE_FS_MEMFS=1 LV_FS_MEMFS_LETTER=77 LV_USE_FS_STDIO=1 LV_FS_STDIO_LETTER=83 LV_FS_STDIO_PATH="" LV_FS_STDIO_CACHE_SIZE=0
  LV_FONT_MONTSERRAT_10=1 LV_FONT_MONTSERRAT_14=1 LV_FONT_MONTSERRAT_18=1 LV_FONT_MONTSERRAT_20=1 LV_FONT_MONTSERRAT_22=1 LV_FONT_MONTSERRAT_16=1 LV_FONT_MONTSERRAT_24=1 LV_FONT_MONTSERRAT_32=1)
add_compile_options(-fsanitize=address,undefined -g -O1)
add_link_options(-fsanitize=address,undefined)
add_subdirectory("${ROOT}/managed_components/lvgl__lvgl" lvgl)
add_executable(test_wifi test_wifi.c "${ROOT}/components/input/input_keymap.c")
target_include_directories(test_wifi PRIVATE stubs "${ROOT}/components/ui/include"
 "${ROOT}/components/wifi/include" "${ROOT}/components/input/include")
target_link_libraries(test_wifi PRIVATE lvgl m)

add_executable(test_sd test_sd.c "${ROOT}/components/storage/sd_files.c")
target_include_directories(test_sd PRIVATE stubs "${ROOT}/components/ui/include"
 "${ROOT}/components/storage/include" "${ROOT}/components/firmware/include"
 "${ROOT}/components/installer/include" "${ROOT}/components/boot_mgr/include"
 "${ROOT}/components/input/include")
target_link_libraries(test_sd PRIVATE lvgl m)

foreach(target test_wifi test_sd)
 target_sources(${target} PRIVATE "${ROOT}/components/ui/ui_nav.c" "${ROOT}/components/ui/ui_progress.c")
endforeach()
add_executable(test_nav test_nav.c "${ROOT}/components/ui/ui_nav.c")
target_include_directories(test_nav PRIVATE "${ROOT}/components/ui/include")
target_link_libraries(test_nav PRIVATE lvgl m)
add_executable(test_home test_home.c "${ROOT}/components/ui/ui_nav.c" "${ROOT}/components/ui/ui_progress.c")
target_include_directories(test_home PRIVATE stubs "${ROOT}/components/ui/include" "${ROOT}/components/installer/include" "${ROOT}/components/firmware/include" "${ROOT}/components/storage/include" "${ROOT}/components/boot_mgr/include")
target_link_libraries(test_home PRIVATE lvgl m)

foreach(target test_home test_sd)
 target_sources(${target} PRIVATE "${ROOT}/components/ui/ui_form.c" "${ROOT}/components/ui/ui_theme.c")
endforeach()

target_sources(test_home PRIVATE "${ROOT}/components/storage/sd_files.c")
add_executable(test_assets test_assets.c "${ROOT}/components/storage/sd_files.c")
target_include_directories(test_assets PRIVATE stubs "${ROOT}/components/ui/include" "${ROOT}/components/storage/include")
target_link_libraries(test_assets PRIVATE lvgl m)

target_include_directories(test_assets PRIVATE "${ROOT}/managed_components/lvgl__lvgl/src")
add_executable(test_cfg test_cfg.c "${ROOT}/components/ui/ui_theme.c" "${ROOT}/components/ui/ui_form.c" "${ROOT}/components/ui/ui_nav.c" "${ROOT}/components/storage/sd_files.c")
target_include_directories(test_cfg PRIVATE stubs "${ROOT}/components/ui/include" "${ROOT}/components/storage/include" "${ROOT}/components/boot_mgr/include" "${ROOT}/components/installer/include" "${ROOT}/components/firmware/include" "${ROOT}/components/wifi/include" "${ROOT}/components/time_mgr/include")
target_link_libraries(test_cfg PRIVATE lvgl m)
add_executable(test_visual test_visual.c "${ROOT}/components/ui/ui_nav.c" "${ROOT}/components/ui/fonts/ui_latin20.c" "${ROOT}/components/time_mgr/date_pt.c")
target_include_directories(test_visual PRIVATE stubs "${ROOT}/components/ui/include" "${ROOT}/components/storage/include" "${ROOT}/components/installer/include" "${ROOT}/components/firmware/include" "${ROOT}/components/time_mgr/include")
target_link_libraries(test_visual PRIVATE lvgl m)
add_executable(test_status test_status.c "${ROOT}/components/ui/ui_theme.c")
target_include_directories(test_status PRIVATE stubs "${ROOT}/components/ui/include" "${ROOT}/components/wifi/include" "${ROOT}/components/time_mgr/include" "${ROOT}/components/input/include")
target_link_libraries(test_status PRIVATE lvgl m)

add_executable(test_services test_services.c "${ROOT}/components/ui/ui_form.c" "${ROOT}/components/ui/ui_nav.c")
target_include_directories(test_services PRIVATE services_stubs stubs
 "${ROOT}/components/ui/include" "${ROOT}/components/catalog/include" "${ROOT}/components/web/include"
 "${ROOT}/components/pi_link/include" "${ROOT}/components/backup/include" "${ROOT}/components/installer/include"
 "${ROOT}/components/firmware/include" "${ROOT}/components/storage/include" "${ROOT}/components/wifi/include")
target_link_libraries(test_services PRIVATE lvgl m)

foreach(target test_home test_cfg)
 target_sources(${target} PRIVATE "${ROOT}/components/ui/ui_picker.c")
endforeach()
foreach(target test_home test_sd test_cfg test_status)
 target_sources(${target} PRIVATE "${ROOT}/components/ui/fonts/ui_latin20.c")
endforeach()
target_include_directories(test_services PRIVATE "${ROOT}/components/partitions_mgr/include")

foreach(target test_sd test_home test_cfg test_services test_assets test_visual test_status)
 target_include_directories(${target} PRIVATE "${ROOT}/components/partitions_mgr/include" "${ROOT}/components/catalog/include")
endforeach()
add_executable(test_fonts test_fonts.c)
target_link_libraries(test_fonts PRIVATE lvgl m)

foreach(target test_home test_sd test_cfg test_assets)
 target_sources(${target} PRIVATE "${ROOT}/components/storage/storage_guard.c")
endforeach()

foreach(target test_sd test_home test_cfg test_status test_assets test_visual test_services test_fonts)
 target_include_directories(${target} PRIVATE "${ROOT}/components/storage/include")
endforeach()
