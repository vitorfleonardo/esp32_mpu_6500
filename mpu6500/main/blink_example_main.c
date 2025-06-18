#include <stdio.h>
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_err.h"

#define I2C_MASTER_NUM         I2C_NUM_0
#define I2C_MASTER_SCL_IO      15
#define I2C_MASTER_SDA_IO      14
#define I2C_MASTER_FREQ_HZ     400000

#define MPU6500_ADDR           0x68
#define MPU6500_PWR_MGMT_1     0x6B
#define MPU6500_SMPLRT_DIV     0x19
#define MPU6500_CONFIG         0x1A
#define MPU6500_GYRO_CONFIG    0x1B
#define MPU6500_ACCEL_CONFIG   0x1C
#define MPU6500_ACCEL_XOUT_H   0x3B
#define MPU6500_GYRO_XOUT_H    0x43

static const char *TAG = "MPU6500";

void i2c_master_init() {
    ESP_LOGI(TAG, "Iniciando I2C...");
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
    ESP_LOGI(TAG, "I2C iniciado com sucesso.");
}

esp_err_t mpu6500_write_register(uint8_t reg, uint8_t value) {
    ESP_LOGI(TAG, "Escrevendo 0x%02X em reg 0x%02X", value, reg);
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MPU6500_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, value, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao escrever reg 0x%02X: 0x%x", reg, ret);
    } else {
        ESP_LOGI(TAG, "Registro 0x%02X configurado com sucesso.", reg);
    }
    return ret;
}

void mpu6500_init() {
    ESP_LOGI(TAG, "Inicializando MPU6500...");
    if (mpu6500_write_register(MPU6500_PWR_MGMT_1, 0x00) != ESP_OK) return;
    if (mpu6500_write_register(MPU6500_SMPLRT_DIV, 0x00) != ESP_OK) return;
    if (mpu6500_write_register(MPU6500_CONFIG, 0x01) != ESP_OK) return;
    if (mpu6500_write_register(MPU6500_GYRO_CONFIG, 0x00) != ESP_OK) return;
    if (mpu6500_write_register(MPU6500_ACCEL_CONFIG, 0x00) != ESP_OK) return;
    ESP_LOGI(TAG, "MPU6500 inicializado com sucesso.");
}

esp_err_t mpu6500_read_data(uint8_t start_reg, int16_t *data, uint8_t len) {
    uint8_t raw[14];
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MPU6500_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, start_reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MPU6500_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, raw, len * 2, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    if (ret == ESP_OK) {
        for (int i = 0; i < len; ++i) {
            data[i] = ((int16_t)raw[2*i] << 8) | raw[2*i+1];
        }
    } else {
        ESP_LOGE(TAG, "Falha ao ler dados do registrador 0x%02X: 0x%x", start_reg, ret);
    }
    return ret;
}

void app_main(void) {
    int16_t accel[3], gyro[3];
    i2c_master_init();
    mpu6500_init();

    while (1) {
        if (mpu6500_read_data(MPU6500_ACCEL_XOUT_H, accel, 3) == ESP_OK &&
            mpu6500_read_data(MPU6500_GYRO_XOUT_H, gyro, 3) == ESP_OK) {
            ESP_LOGI(TAG, "Accel [raw]: X=%d Y=%d Z=%d | Gyro [raw]: X=%d Y=%d Z=%d",
                     accel[0], accel[1], accel[2], gyro[0], gyro[1], gyro[2]);
        } else {
            ESP_LOGE(TAG, "Erro na leitura dos dados do MPU6500");
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
