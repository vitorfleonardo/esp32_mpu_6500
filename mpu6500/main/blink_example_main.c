#include <stdio.h>
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define I2C_MASTER_NUM         I2C_NUM_0
#define I2C_MASTER_SCL_IO      15
#define I2C_MASTER_SDA_IO      14
#define I2C_MASTER_FREQ_HZ     25000

#define MPU6500_ADDR           0x68

#define MPU6500_PWR_MGMT_1     0x6B
#define MPU6500_SMPLRT_DIV     0x19
#define MPU6500_CONFIG         0x1A
#define MPU6500_GYRO_CONFIG    0x1B
#define MPU6500_ACCEL_CONFIG   0x1C

#define MPU6500_ACCEL_XOUT_H   0x3B
#define MPU6500_GYRO_XOUT_H    0x43

#define ACCEL_SCALE_FACTOR     16384.0f  // ±2g
#define GYRO_SCALE_FACTOR      131.0f    // ±250°/s

static const char *TAG = "MPU6500_DBG";

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
    if (i2c_param_config(I2C_MASTER_NUM, &conf) != ESP_OK) {
        ESP_LOGE(TAG, "Erro na configuração I2C");
    }
    if (i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0) != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao instalar driver I2C");
    }
    ESP_LOGI(TAG, "I2C configurado.");
}

esp_err_t mpu6500_write(uint8_t reg, uint8_t value) {
    ESP_LOGI(TAG, "Escrevendo 0x%02X no registrador 0x%02X", value, reg);
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
        ESP_LOGI(TAG, "Registrador 0x%02X configurado com sucesso.", reg);
    }
    return ret;
}

esp_err_t mpu6500_read_burst(uint8_t reg, uint8_t *data, size_t len) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MPU6500_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MPU6500_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, data, len, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao ler %d bytes a partir do registrador 0x%02X: 0x%x", len, reg, ret);
    }
    return ret;
}

void mpu6500_init() {
    ESP_LOGI(TAG, "Inicializando MPU6500...");
    if (mpu6500_write(MPU6500_PWR_MGMT_1, 0x00) != ESP_OK) return;
    if (mpu6500_write(MPU6500_SMPLRT_DIV, 0x00) != ESP_OK) return;
    if (mpu6500_write(MPU6500_CONFIG, 0x01) != ESP_OK) return;
    if (mpu6500_write(MPU6500_GYRO_CONFIG, 0x00) != ESP_OK) return;
    if (mpu6500_write(MPU6500_ACCEL_CONFIG, 0x00) != ESP_OK) return;
    ESP_LOGI(TAG, "MPU6500 inicializado.");
}

void mpu6500_task(void *arg) {
    uint8_t raw[14];
    int16_t ax, ay, az, gx, gy, gz;

    while (1) {
        esp_err_t ret = mpu6500_read_burst(MPU6500_ACCEL_XOUT_H, raw, 14);
        if (ret == ESP_OK) {
            ax = (raw[0] << 8) | raw[1];
            ay = (raw[2] << 8) | raw[3];
            az = (raw[4] << 8) | raw[5];
            gx = (raw[8] << 8) | raw[9];
            gy = (raw[10] << 8) | raw[11];
            gz = (raw[12] << 8) | raw[13];

            float ax_g = ax / ACCEL_SCALE_FACTOR;
            float ay_g = ay / ACCEL_SCALE_FACTOR;
            float az_g = az / ACCEL_SCALE_FACTOR;
            float gx_dps = gx / GYRO_SCALE_FACTOR;
            float gy_dps = gy / GYRO_SCALE_FACTOR;
            float gz_dps = gz / GYRO_SCALE_FACTOR;

            ESP_LOGI(TAG, "Accel[g] X=%.3f Y=%.3f Z=%.3f | Gyro[°/s] X=%.2f Y=%.2f Z=%.2f",
                     ax_g, ay_g, az_g, gx_dps, gy_dps, gz_dps);
        }
        vTaskDelay(pdMS_TO_TICKS(5)); // 200 Hz de amostragem
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "app_main iniciado");
    i2c_master_init();
    mpu6500_init();
    xTaskCreatePinnedToCore(mpu6500_task, "mpu6500_task", 4096, NULL, 5, NULL, 0);
}
