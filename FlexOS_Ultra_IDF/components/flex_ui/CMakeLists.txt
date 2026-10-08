# Interfaz de Flex OS Ultra: SOLO LVGL (tools/check_lvgl_only.py).
file(GLOB FLEX_UI_SRCS
    "${CMAKE_CURRENT_LIST_DIR}/src/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/diag/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/theme/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/widgets/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/fonts/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/i18n/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/shell/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/apps/*.c")
idf_component_register(
    SRCS ${FLEX_UI_SRCS}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src" "src/theme" "src/widgets" "src/i18n" "src/shell" "src/apps"
    REQUIRES lvgl__lvgl flex_core
    PRIV_REQUIRES flex_display flex_touch flex_i2c flex_storage flex_system flex_security esp_timer heap esp_system
)
