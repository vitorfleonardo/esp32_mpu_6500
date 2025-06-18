#include <stdio.h>
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_err.h"

#define I2C_MASTER_NUM      I2C_NUM_0
#define I2C_MASTER_SCL_IO   15
#define I2C_MASTER_SDA_IO   14
#define I2C_MASTER_FREQ_HZ  400000

#define MPU6500_ADDR        0x69
#define MPU6500_WHO_AM_I    0x75
#define MPU6500_PWR_MGMT_1  0x6B
#define MPU6500_ACCEL_XOUT_H 0x3B

static const char *TAG = "MPU6500_TEST";

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

esp_err_t mpu6500_wakeup() {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MPU6500_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, MPU6500_PWR_MGMT_1, true);
    i2c_master_write_byte(cmd, 0x00, true);  // Wake up
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
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
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

esp_err_t mpu6500_read_accel(int16_t *ax, int16_t *ay, int16_t *az) {
    uint8_t data[6];
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MPU6500_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, MPU6500_ACCEL_XOUT_H, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MPU6500_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, data, 6, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    if (ret == ESP_OK) {
        *ax = (int16_t)((data[0] << 8) | data[1]);
        *ay = (int16_t)((data[2] << 8) | data[3]);
        *az = (int16_t)((data[4] << 8) | data[5]);
    }
    return ret;
}

void app_main() {
    i2c_master_init();
    ESP_LOGI(TAG, "I2C initialized");

    if (mpu6500_wakeup() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to wake up MPU-6500");
        return;
    }

    uint8_t who_am_i = 0;
    if (mpu6500_read_whoami(&who_am_i) != ESP_OK || who_am_i != 0x70) {
        ESP_LOGE(TAG, "MPU-6500 not found or I2C error");
        return;
    }
    ESP_LOGI(TAG, "MPU-6500 detected! WHO_AM_I = 0x%02X", who_am_i);

    int16_t ax, ay, az;
    while (1) {
        if (mpu6500_read_accel(&ax, &ay, &az) == ESP_OK) {
            ESP_LOGI(TAG, "Accel X: %d, Y: %d, Z: %d", ax, ay, az);
        } else {
            ESP_LOGE(TAG, "Failed to read accel data");
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
