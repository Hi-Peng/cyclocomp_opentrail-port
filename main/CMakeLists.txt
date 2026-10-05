idf_component_register(
    SRCS "main.cpp" "board_display.cpp" "ui_port.cpp" "ui_app.cpp" "app_settings.cpp" "ble_companion.cpp"
         "opentrail/epd_compat.cpp" "opentrail/vfont.cpp" "opentrail/ui_render.cpp"
         "opentrail/dash_layout.cpp" "opentrail/radar.cpp" "opentrail/workout.cpp"
    INCLUDE_DIRS "." "opentrail"
    REQUIRES esp_driver_gpio esp_driver_spi esp_driver_ledc esp_rom esp_timer esp_system nvs_flash bt
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE VFONT=1)
set_source_files_properties(
    opentrail/epd_compat.cpp opentrail/vfont.cpp opentrail/ui_render.cpp
    opentrail/dash_layout.cpp opentrail/radar.cpp opentrail/workout.cpp
    PROPERTIES COMPILE_OPTIONS "-Wno-error;-Wno-address;-Wno-extra")
