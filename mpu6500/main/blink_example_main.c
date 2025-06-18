#include <stdio.h>
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_err.h"

#define I2C_MASTER_NUM         I2C_NUM_0
#define I2C_MASTER_SCL_IO      15
#define I2C_MASTER_SDA_IO      14
#define I2C_MASTER_FREQ_HZ     400000

#define MPU6500_ADDR           0x68
#define MPU6500_WHO_AM_I       0x75

static const char *TAG = "MPU6500_ID";

void i2c_master_init() {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_NUM, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0));
}

esp_err_t mpu6500_read_whoami(uint8_t *who_am_i) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MPU6500_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, MPU6500_WHO_AM_I, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MPU6500_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, who_am_i, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    return ret;
}

void app_main(void) {
    uint8_t who_am_i = 0;
    i2c_master_init();
    ESP_LOGI(TAG, "I2C initialized");

    esp_err_t ret = mpu6500_read_whoami(&who_am_i);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WHO_AM_I = 0x%02X", who_am_i);
        if (who_am_i == 0x70) {
            ESP_LOGI(TAG, "MPU-6500 detected successfully!");
        } else {
            ESP_LOGW(TAG, "Unexpected WHO_AM_I value");
        }
    } else {
        ESP_LOGE(TAG, "Failed to communicate with MPU-6500");
    }
}
