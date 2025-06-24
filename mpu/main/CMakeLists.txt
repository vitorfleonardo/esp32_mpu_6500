idf_component_register(
    SRCS "main.c" "main_test.c"
    INCLUDE_DIRS "."
    REQUIRES unity driver esp_timer fatfs freertos sdmmc
)
