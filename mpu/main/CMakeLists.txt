idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES unity driver esp_timer fatfs freertos sdmmc
)
