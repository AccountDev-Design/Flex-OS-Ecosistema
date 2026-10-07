# Simulador de la UI de Flex OS Ultra en el PC (sin SDL: dibuja en memoria).
#   cmake -S sim -B sim/build && cmake --build sim/build -j && sim/build/flexos_sim sim/out
cmake_minimum_required(VERSION 3.16)
project(flexos_sim C CXX)

set(CMAKE_C_STANDARD 11)
set(FLEX_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

# LVGL: la misma copia que usa el firmware (fijada en dependencies.lock) si
# ya se ha compilado una vez; si no, la etiqueta v9.6.0 desde GitHub.
set(LVGL_SOURCE_DIR "${FLEX_ROOT}/managed_components/lvgl__lvgl" CACHE PATH "Fuentes de LVGL 9.6.0")
set(LV_BUILD_CONF_PATH ${CMAKE_CURRENT_LIST_DIR}/lv_conf.h CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
set(LV_FETCH_DEPENDENCIES OFF CACHE BOOL "" FORCE)
set(LV_BUILD_INSTALL OFF CACHE BOOL "" FORCE)

if(EXISTS ${LVGL_SOURCE_DIR}/CMakeLists.txt)
    add_subdirectory(${LVGL_SOURCE_DIR} ${CMAKE_BINARY_DIR}/lvgl)
else()
    include(FetchContent)
    FetchContent_Declare(lvgl GIT_REPOSITORY https://github.com/lvgl/lvgl.git GIT_TAG v9.6.0 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(lvgl)
endif()

add_executable(flexos_sim
    sim_main.c
    ${FLEX_ROOT}/components/flex_ui/src/diag/flex_ui_diag.c
    ${FLEX_ROOT}/components/flex_core/src/flex_metrics.c
)
target_include_directories(flexos_sim PRIVATE
    ${FLEX_ROOT}/components/flex_ui/include
    ${FLEX_ROOT}/components/flex_ui/src
    ${FLEX_ROOT}/components/flex_core/include
    ${FLEX_ROOT}/components/flex_display/src
)
target_compile_options(flexos_sim PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
target_link_libraries(flexos_sim PRIVATE lvgl m)
