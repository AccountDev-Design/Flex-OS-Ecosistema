# Juegos: Jumper de Arduino TAL CUAL sobre el motor de dibujo de Arduino extraido (tools/gen_jumper_engine.py); ver include/flex_jumper.h.
# Una sola unidad de traduccion (como en el sketch).
idf_component_register(
    SRCS "src/flex_jumper.cpp"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    PRIV_REQUIRES flex_storage
)
# Codigo heredado de Arduino: se compila tal cual, con los avisos de estilo que el sketch nunca tuvo desactivados.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-misleading-indentation -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter
    -Wno-format-truncation)
