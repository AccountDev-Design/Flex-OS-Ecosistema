# Logica pura de la version Arduino, copiada SIN CAMBIOS (tools/check_portable.py
# lo comprueba en cada build). La cubren las pruebas de host de tests/host.
file(GLOB FLEX_PORTABLE_SRCS "${CMAKE_CURRENT_LIST_DIR}/src/*.cpp")
idf_component_register(
    SRCS ${FLEX_PORTABLE_SRCS}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "include"
    REQUIRES json mbedtls
)
# Codigo heredado: se compila tal cual, con sus propios avisos desactivados.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-function -Wno-unused-variable -Wno-format-truncation)
# Dos avisos de GCC 14 revisados a mano (falsos positivos, se dejan solo en su archivo):
#  * StorageCore mLp(): la etiqueta (<=32) y tres campos (<=1+255) suman 800 =
#    sizeof(Msg::b) como mucho; el recorte de la linea anterior lo garantiza.
#  * ImgEdit drawOverlays(): P[][] se rellena en la rama de LINE/ARROW, la
#    unica que lo lee despues.
set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/src/FlexOS_StorageCore.cpp"
    PROPERTIES COMPILE_OPTIONS "-Wno-stringop-overflow")
set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/src/FlexOS_ImgEdit.cpp"
    PROPERTIES COMPILE_OPTIONS "-Wno-maybe-uninitialized")
