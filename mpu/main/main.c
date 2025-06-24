#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"

// ==== CONFIGURAÇÕES ====
#define I2C_MASTER_NUM         I2C_NUM_0
#define I2C_SDA_IO             14
#define I2C_SCL_IO             15
#define I2C_FREQ_HZ            200000
#define MPU_ADDR               0x68
#define BUTTON_GPIO            13
#define DEBOUNCE_TIME_MS       50
#define BUFFER_SIZE_BYTES      1024

// ==== REGISTRADORES MPU-6500 ====
#define MPU_PWR_MGMT_1         0x6B
#define MPU_SMPLRT_DIV         0x19
#define MPU_CONFIG             0x1A
#define MPU_GYRO_CONFIG        0x1B
#define MPU_ACCEL_CONFIG       0x1C
#define MPU_FIFO_EN            0x23
#define MPU_USER_CTRL          0x6A
#define MPU_FIFO_COUNTH        0x72
#define MPU_FIFO_COUNTL        0x73
#define MPU_FIFO_RW            0x74

// ==== DEFINIÇÕES DE ESTADO ====
typedef enum {
    ESTADO_IDLE,
    ESTADO_COLETANDO,
    ESTADO_FINALIZADO
} estado_t;

static estado_t estado_atual = ESTADO_IDLE;
static const char *TAG = "MPU_FOGUETE";

// ==== BUFFER PARA DADOS ====
static uint8_t buffer_dados[BUFFER_SIZE_BYTES];
static size_t buffer_index = 0;
static SemaphoreHandle_t mutex_buffer;

// ==== FUNÇÕES AUXILIARES ====

esp_err_t mpu_write(uint8_t reg, uint8_t data) {
    return i2c_master_write_to_device(I2C_MASTER_NUM, MPU_ADDR, (uint8_t[]){reg, data}, 2, pdMS_TO_TICKS(10));
}

esp_err_t mpu_read(uint8_t reg, uint8_t *data, size_t len) {
    return i2c_master_write_read_device(I2C_MASTER_NUM, MPU_ADDR, &reg, 1, data, len, pdMS_TO_TICKS(10));
}

void inicializar_mpu() {
    mpu_write(MPU_PWR_MGMT_1, 0x00);
    mpu_write(MPU_SMPLRT_DIV, 4);         // 200 Hz
    mpu_write(MPU_CONFIG, 0x03);          // DLPF 44Hz
    mpu_write(MPU_GYRO_CONFIG, 0x00);     // ±250°/s
    mpu_write(MPU_ACCEL_CONFIG, 0x00);    // ±2g
    mpu_write(MPU_FIFO_EN, 0x78);         // gyro+accel
    mpu_write(MPU_USER_CTRL, 0x04);       // reset FIFO
    mpu_write(MPU_USER_CTRL, 0x40);       // enable FIFO
}

void ler_fifo_e_salvar() {
    uint8_t contagem[2];
    mpu_read(MPU_FIFO_COUNTH, contagem, 2);
    uint16_t tamanho = (contagem[0] << 8) | contagem[1];

    if (tamanho < 12) return; // mínimo 1 leitura

    tamanho = (tamanho / 12) * 12;
    uint8_t dados[tamanho];
    mpu_read(MPU_FIFO_RW, dados, tamanho);

    int64_t timestamp = esp_timer_get_time(); // micros()

    xSemaphoreTake(mutex_buffer, portMAX_DELAY);
    for (int i = 0; i < tamanho; i += 12) {
        if (buffer_index + 64 > BUFFER_SIZE_BYTES) break;

        int16_t ax = (dados[i] << 8) | dados[i+1];
        int16_t ay = (dados[i+2] << 8) | dados[i+3];
        int16_t az = (dados[i+4] << 8) | dados[i+5];
        int16_t gx = (dados[i+6] << 8) | dados[i+7];
        int16_t gy = (dados[i+8] << 8) | dados[i+9];
        int16_t gz = (dados[i+10] << 8) | dados[i+11];

        buffer_index += snprintf((char*)&buffer_dados[buffer_index],
                                 BUFFER_SIZE_BYTES - buffer_index,
                                 "%lld,%d,%d,%d,%d,%d,%d\n",
                                 timestamp, ax, ay, az, gx, gy, gz);
    }
    xSemaphoreGive(mutex_buffer);
}

void salvar_sd_card() {
    FILE *f = fopen("/sdcard/dados.csv", "a");
    if (f) {
        xSemaphoreTake(mutex_buffer, portMAX_DELAY);
        fwrite(buffer_dados, 1, buffer_index, f);
        buffer_index = 0;
        xSemaphoreGive(mutex_buffer);
        fclose(f);
    } else {
        ESP_LOGE(TAG, "Erro ao abrir dados.csv");
    }
}

void tarefa_coleta(void *arg) {
    while (1) {
        if (estado_atual == ESTADO_COLETANDO) {
            ler_fifo_e_salvar();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void tarefa_gravacao(void *arg) {
    while (1) {
        if (estado_atual == ESTADO_COLETANDO) {
            salvar_sd_card();
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void IRAM_ATTR isr_botao(void *arg) {
    static int64_t ultimo_press = 0;
    int64_t agora = esp_timer_get_time();
    if ((agora - ultimo_press) > DEBOUNCE_TIME_MS * 1000) {
        ultimo_press = agora;
        if (estado_atual == ESTADO_IDLE) {
            ESP_LOGI(TAG, "Botão pressionado → INICIAR");
            estado_atual = ESTADO_COLETANDO;
            inicializar_mpu();
        } else if (estado_atual == ESTADO_COLETANDO) {
            ESP_LOGI(TAG, "Botão pressionado → FINALIZAR");
            estado_atual = ESTADO_FINALIZADO;
            salvar_sd_card();
            mpu_write(MPU_USER_CTRL, 0x00); // desativa FIFO
        }
    }
}

void inicializar_sd() {
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 1,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_card_t *card;
    const char mount_point[] = "/sdcard";
    esp_err_t ret = esp_vfs_fat_sdmmc_mount(mount_point, &sdmmc_host_default(), &sdmmc_slot_config(), &mount_config, &card);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erro montando SD: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "SD montado com sucesso.");
    }
}

void app_main() {
    mutex_buffer = xSemaphoreCreateMutex();

    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_IO,
        .scl_io_num = I2C_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };
    i2c_param_config(I2C_MASTER_NUM, &i2c_conf);
    i2c_driver_install(I2C_MASTER_NUM, i2c_conf.mode, 0, 0, 0);

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_POSEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .pull_up_en = 1,
    };
    gpio_config(&io_conf);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_GPIO, isr_botao, NULL);

    inicializar_sd();

    xTaskCreate(tarefa_coleta, "tarefa_coleta", 4096, NULL, 2, NULL);
    xTaskCreate(tarefa_gravacao, "tarefa_gravacao", 4096, NULL, 1, NULL);
}
