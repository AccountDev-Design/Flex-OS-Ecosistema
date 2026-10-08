idf_component_register(
    SRCS "src/flex_passcode.c" "src/flex_passcode_async.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES flex_core flex_storage mbedtls esp_hw_support log freertos
)
