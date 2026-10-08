# Flex OS Ultra · proyecto ESP-IDF (ESP32-P4, GUITION JC4880P443C)
cmake_minimum_required(VERSION 3.16)

# Revision del silicio. Los binarios para P4 < v3 y para v3.x son
# INCOMPATIBLES entre si (ESP-IDF los trata como chips distintos): hay que
# compilar para la revision real de la placa. Ver docs/REVISION_CHIP.md.
#   lt_v3  -> v0.1 .. v1.99 (lo que indica que la placa funcione con el core
#             Arduino 3.2.1; PENDIENTE de confirmar con esptool)
#   v3     -> v3.0 .. v3.99
set(FLEX_P4_REV "lt_v3" CACHE STRING "Revision del ESP32-P4: lt_v3 o v3")
set_property(CACHE FLEX_P4_REV PROPERTY STRINGS lt_v3 v3)
set(FLEX_BUILD "dev" CACHE STRING "dev o release")
set_property(CACHE FLEX_BUILD PROPERTY STRINGS dev release)

if(NOT FLEX_P4_REV MATCHES "^(lt_v3|v3)$")
    message(FATAL_ERROR "FLEX_P4_REV debe ser lt_v3 o v3 (es '${FLEX_P4_REV}')")
endif()
if(NOT FLEX_BUILD MATCHES "^(dev|release)$")
    message(FATAL_ERROR "FLEX_BUILD debe ser dev o release (es '${FLEX_BUILD}')")
endif()

# Si quien compila no pasa su propia lista de defaults, se compone aqui.
# Ojo: un sdkconfig que ya existe manda sobre los defaults; por eso
# tools/build.sh usa un directorio y un sdkconfig por variante.
if(NOT DEFINED SDKCONFIG_DEFAULTS AND NOT DEFINED ENV{SDKCONFIG_DEFAULTS})
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.defaults.rev_${FLEX_P4_REV}")
    if(FLEX_BUILD STREQUAL "release")
        list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.defaults.release")
    endif()
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(flexos_ultra_idf)

# El sdkconfig en uso manda sobre los defaults: si no corresponde a la variante
# pedida, el binario saldria para otra revision o con ajustes de otra variante
# sin que nada lo dijera. Se aborta con la instruccion para corregirlo.
set(_flex_fix "Borra ese sdkconfig o compila con tools/build.sh (un directorio y un sdkconfig por variante).")
if(FLEX_P4_REV STREQUAL "lt_v3" AND NOT CONFIG_ESP32P4_SELECTS_REV_LESS_V3)
    message(FATAL_ERROR "FLEX_P4_REV=lt_v3 pero el sdkconfig existente es para chips v3.x. ${_flex_fix}")
elseif(FLEX_P4_REV STREQUAL "v3" AND CONFIG_ESP32P4_SELECTS_REV_LESS_V3)
    message(FATAL_ERROR "FLEX_P4_REV=v3 pero el sdkconfig existente es para chips < v3. ${_flex_fix}")
endif()
if(FLEX_BUILD STREQUAL "release" AND (CONFIG_FLEX_DEBUG_METRICS OR NOT CONFIG_LOG_DEFAULT_LEVEL_WARN))
    message(FATAL_ERROR "FLEX_BUILD=release pero el sdkconfig existente es de desarrollo. ${_flex_fix}")
elseif(FLEX_BUILD STREQUAL "dev" AND CONFIG_LOG_DEFAULT_LEVEL_WARN AND NOT CONFIG_FLEX_DEBUG_METRICS)
    message(FATAL_ERROR "FLEX_BUILD=dev pero el sdkconfig existente es de release. ${_flex_fix}")
endif()
