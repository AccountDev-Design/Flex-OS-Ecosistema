idf_component_register(
    SRCS "src/flex_kv.c" "src/flex_fs_path.c" "src/flex_storage.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES flex_core nvs_flash esp_partition joltwallet__littlefs esp_timer esp_system vfs
)
