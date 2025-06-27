#include <stdio.h>                  // Para printf, fprintf, FILE*, etc.
#include "driver/i2c.h"             // I2C functions: write_to_device, read_from_device e i2c_config_t
#include "freertos/FreeRTOS.h"      // Necessário para pdMS_TO_TICKS()
#include "freertos/task.h"          // FreeRTOS functions: vTaskDelay, xTaskCreate
#include "esp_log.h"                // Para ESP_LOGI, ESP_LOGE
#include "esp_err.h"                // Para tipos esp_err_t e esp_err_to_name
#include "freertos/semphr.h"        // Usar semáforo de exclusão mútua
#include "esp_vfs_fat.h"            // Para esp_vfs_fat_sdmmc_mount e config FAT
#include "driver/sdmmc_host.h"      // Para SDMMC_HOST_DEFAULT()
#include "driver/sdmmc_defs.h"      // Definições auxiliares do SDMMC
#include "sdmmc_cmd.h"              // Para tipo sdmmc_card_t e funções do SD

#include "sdkconfig.h"
#include <string.h>
#include <stdlib.h>
#include "driver/gpio.h"
#include "esp_timer.h"

// ==== CONFIGURAÇÕES ====
#define I2C_MASTER_NUM         I2C_NUM_0    // Uso do I2C0 da ESP32
#define I2C_SDA_IO             14           // GPIO 14 da ESP32 -> SDA do MPU-6500 (fio de dados)
#define I2C_SCL_IO             15           // GPIO 15 da ESP32 -> SCL do MPU-6500 (fio de clock)
#define I2C_FREQ_HZ            200000       // Frequência de comunicaçao do barramento I2C (200 kHz)
#define MPU_ADDR               0x68         // pino ADO (address select) do MPU-6500 conectado no GND
#define BUTTON_GPIO            13           // GPIO 13 da ESP32 -> Botão fisico (ativa & desativa coleta de dados)
#define DEBOUNCE_TIME_MS       50           // Tempo de debounce do botão (50 ms)
#define BUFFER_SIZE_BYTES      1024         // Tamanho do buffer de memória RAM interna

// ==== REGISTRADORES MPU-6500 ====
#define MPU_PWR_MGMT_1         0x6B // Função: Controla o estado de energia do chip
#define MPU_SMPLRT_DIV         0x19 // Função: Define o divisor da taxa de amostragem
#define MPU_CONFIG             0x1A // Função: Controla filtros digitais (DLPF) e sincronização
#define MPU_GYRO_CONFIG        0x1B // Função: Configura escala do giroscópio (sensibilidade) e modo self-test
#define MPU_ACCEL_CONFIG       0x1C // Função: Configura escala do acelerômetro e self-test
#define MPU_FIFO_EN            0x23 // Função: Ativa quais dados são enviados à FIFO
#define MPU_USER_CTRL          0x6A // Função: Controla FIFO e I2C
#define MPU_FIFO_COUNTH        0x72 // Função: Indica o número de bytes disponíveis na FIFO - parte alta (bits 15–8)
#define MPU_FIFO_COUNTL        0x73 // Função: Indica o número de bytes disponíveis na FIFO - parte baixa (bits bits 7–0)
#define MPU_FIFO_RW            0x74 // Função: Registrador de leitura e escrita FIFO

// ==== DEFINIÇÕES DE ESTADO ====
typedef enum {
    ESTADO_IDLE,                    // Estado inicial, aguardando o botão ser pressionado
    ESTADO_COLETANDO,               // Estado ativo, coletando dados do MPU-6500
    ESTADO_FINALIZADO               // Estado final, coleta de dados concluída
} estado_t;

static estado_t estado_atual = ESTADO_IDLE;
static const char *TAG = "MPU_FOGUETE";

// ==== BUFFER PARA DADOS ====
static uint8_t buffer_dados[BUFFER_SIZE_BYTES];
static size_t buffer_index = 0;
static SemaphoreHandle_t mutex_buffer; // Ponteiro para o mutex armazenado

// ==== FUNÇÕES AUXILIARES ====

esp_err_t mpu_write(uint8_t reg, uint8_t data) {
    /*
        * Escreve um único byte no registrador do MPU-6500.
        * reg: Endereço do registrador a ser escrito.
        * data: Valor a ser escrito no registrador.
        * Retorna ESP_OK em caso de sucesso ou outro erro em caso de falha.
    */
    return i2c_master_write_to_device(
        I2C_MASTER_NUM,         // Controlador I2C
        MPU_ADDR,               // Endereço do MPU-6500 (0x68)
        (uint8_t[]){reg, data}, // Buffer contendo [registrador, valor]
        2,                      // Quantos bytes serão enviados
        pdMS_TO_TICKS(10)       // Timeout de 10ms
    );
}

esp_err_t mpu_read(uint8_t reg, uint8_t *data, size_t len) {
    /*
        * Lê múltiplos bytes do registrador do MPU-6500.
        * reg: Endereço do registrador a ser lido.
        * data: Buffer onde os dados lidos serão armazenados.
        * len: Quantidade de bytes a serem lidos.
        * Retorna ESP_OK em caso de sucesso ou outro erro em caso de falha.
    */
    return i2c_master_write_read_device(
        I2C_MASTER_NUM,   // Controlador I2C
        MPU_ADDR,         // Endereço do MPU-6500 (0x68)
        &reg, 1,          // Primeiro escreve 1 byte com o registrador de origem
        data, len,        // Depois lê 'len' bytes para 'data'
        pdMS_TO_TICKS(10) // Timeout de 10ms
    );
}

void inicializar_mpu() {
    mpu_write(MPU_PWR_MGMT_1, 0x01);      // Desativa modo SLEEP e ativa o PLL com Gyro X
    mpu_write(MPU_SMPLRT_DIV, 4);         // Setagem para frequencia de 200 amostras por segundo
    mpu_write(MPU_CONFIG, 0x03);          // Setagem para DLPF 44Hz
    mpu_write(MPU_GYRO_CONFIG, 0x00);     // Setagem da a sensibilidade angular para ±250°/s
    mpu_write(MPU_ACCEL_CONFIG, 0x00);    // Setagem da sensibilidade linear para ±2g
    mpu_write(MPU_FIFO_EN, 0x78);         // Setagem para receber dados gyro & accel
    mpu_write(MPU_USER_CTRL, 0x04);       // Reseta antes de ativar FIFO (limpar dados antigos)
    mpu_write(MPU_USER_CTRL, 0x40);       // Ativa a FIFO
}

void ler_fifo_e_salvar() {
    /*
        * Lê os dados do FIFO do MPU-6500 e salva no buffer.
        * Se o FIFO estiver vazio, não faz nada.
        * Se houver dados, lê até 64 leituras (12 bytes cada) e salva no buffer.
        * Cada leitura contém: timestamp, ax, ay, az, gx, gy, gz.
    */

    uint8_t contagem[2];                                    // Buffer para armazenar a contagem de bytes disponíveis na FIFO
    mpu_read(MPU_FIFO_COUNTH, contagem, 2);                 // Lê os dois bytes do contador da FIFO (parte alta)
    uint16_t tamanho = (contagem[0] << 8) | contagem[1];    // Converte os dois bytes lidos em um número de 16 bits

    // Se não houver pelo menos 1 leitura completa (12 bytes), a função retorna
    if (tamanho < 12) return;

    tamanho = (tamanho / 12) * 12;          // Garante que o número de bytes a serem lidos seja múltiplo de 12.
    uint8_t dados[tamanho];                 // Array temporário para armazenar os dados lidos do FIFO
    mpu_read(MPU_FIFO_RW, dados, tamanho);  // Lê os dados do FIFO e armazena no array 'dados'

    // Marca o timestamp atual em microssegundos
    int64_t timestamp = esp_timer_get_time();

    // evita acesso simultâneo ao buffer_dados
    xSemaphoreTake(mutex_buffer, portMAX_DELAY);

    // Loop para processar todos os blocos de 12 bytes (amostras) lidos do FIFO
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
    /*
        * Salva os dados do buffer de memória RAM no cartão SD.
        * Abre o arquivo "dados.csv" no modo append ("a").
        * Se o arquivo for aberto com sucesso, escreve os dados do buffer.
        * Se não for possível abrir o arquivo, exibe uma mensagem de erro.
    */

    FILE *f = fopen("/sdcard/dados.csv", "a");

    if (f) {
        xSemaphoreTake(mutex_buffer, portMAX_DELAY);    // Trava o mutex antes de escrever no buffer
        fwrite(buffer_dados, 1, buffer_index, f);       // Escreve os dados do buffer no arquivo
        buffer_index = 0;                               // Reseta o índice do buffer para 0 após a gravação
        xSemaphoreGive(mutex_buffer);                   // Libera o mutex após a escrita
        fclose(f);                                      // Fecha o arquivo após a escrita
    } else {
        ESP_LOGE(TAG, "Erro ao abrir dados.csv");
    }
}

void tarefa_coleta(void *arg) {
    /*
        * Tarefa responsável por coletar dados do MPU-6500 e salvar no buffer.
        * Se o estado atual for ESTADO_COLETANDO, chama a função ler_fifo_e_salvar().
        * A tarefa roda continuamente com um delay de 100 ms.
        * Se o estado atual for ESTADO_FINALIZADO, a tarefa não faz nada.
    */
    while (1) {
        if (estado_atual == ESTADO_COLETANDO) {
            ler_fifo_e_salvar();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void tarefa_gravacao(void *arg) {
    /*
        * Tarefa responsável por salvar os dados do buffer de memória RAM no cartão SD.
        * Se o estado atual for ESTADO_COLETANDO, chama a função salvar_sd_card().
        * A tarefa roda continuamente com um delay de 1000 ms (1 segundo).
        * Se o estado atual for ESTADO_FINALIZADO, a tarefa não faz nada.
    */
    while (1) {
        if (estado_atual == ESTADO_COLETANDO) {
            salvar_sd_card();
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void IRAM_ATTR isr_botao(void *arg) {
    /*
        * Função de interrupção para o botão.
        * Verifica se o botão foi pressionado e alterna entre os estados ESTADO_IDLE e ESTADO_COLETANDO.
        * Implementa debounce para evitar múltiplas leituras rápidas do botão.
    */

    static int64_t ultimo_press = 0;
    int64_t agora = esp_timer_get_time();

    // Verifica se o tempo desde o último pressionamento é maior que o tempo de debounce
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
    /*
        * Inicializa o cartão SD usando a biblioteca FAT.
        * Configurações:
        * - Não formata se a montagem falhar.
        * - Permite 1 arquivo aberto.
        * - Tamanho de alocação de 16 KB.
    */

    // Bloco de configuração do sistema de arquivos FAT
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,        // Não formata se a montagem falhar
        .max_files = 1,                          // Permite apenas 1 arquivo aberto por vez
        .allocation_unit_size = 16 * 1024       // Tamanho mínimo de alocação de 16 KB
    };

    sdmmc_card_t *card;                         // Ponteiro para o cartão SD
    const char mount_point[] = "/sdcard";       // Ponto de montagem do cartão SD
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();   // configuração padrão de comunicação com o cartão SD via SDMMC
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT(); // configuração padrão do slot de leitura do cartão SD

    // Chamada para montar o cartão SD com os parâmetros definidos
    esp_err_t ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erro montando SD: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "SD montado com sucesso.");
    }
}

void app_main() {
    // Cria um mutex (semáforo de exclusão mútua)
    mutex_buffer = xSemaphoreCreateMutex();

    // Inicializa o I2C (ESP32<->MPU-6500)
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,                // ESP32 como master e MPU-6500 slave
        .sda_io_num = I2C_SDA_IO,               // GPIO 14 como SDA
        .scl_io_num = I2C_SCL_IO,               // GPIO 15 como SCL
        .sda_pullup_en = GPIO_PULLUP_ENABLE,    // Habilita pull-up interno no SDA !!!!! Revisar na hora de soldar !!!! exige resistores de pull-up
        .scl_pullup_en = GPIO_PULLUP_ENABLE,    // Habilita pull-up interno no SDA !!!!! Revisar na hora de soldar !!!! exige resistores de pull-up
        .master.clk_speed = I2C_FREQ_HZ,        // Define a frequência de 200 kHz
    };
    i2c_param_config(I2C_MASTER_NUM, &i2c_conf);  // Configura os parâmetros do barramento setados i2c_conf
    i2c_driver_install(I2C_MASTER_NUM, i2c_conf.mode, 0, 0, 0); // Instala o driver I2C no controlador

    // Configura o GPIO do botão
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_POSEDGE,         // Interrupção na borda de subida (LOW->HIGH)
        .mode = GPIO_MODE_INPUT,                // Configura o GPIO como input
        .pin_bit_mask = (1ULL << BUTTON_GPIO),  // Configura o GPIO 13 como bit mask
        .pull_up_en = 1,                        // Habilita pull-up interno no GPIO 13
    };
    gpio_config(&io_conf); // Configura o GPIO com os parâmetros setados em io_conf
    gpio_install_isr_service(0); // Instala o serviço de interrupção do GPIO
    gpio_isr_handler_add(BUTTON_GPIO, isr_botao, NULL); // Adiciona o handler de interrupção para o botão

    // Inicializa o SD card
    inicializar_sd();

    xTaskCreate(
        tarefa_coleta,      // ponteiro para a função tarefa_coleta
        "tarefa_coleta",    // nome da tarefa
        4096,               // tamanho da stack para essa task (4 KB)
        NULL,               // parâmetro para a task (não usado)
        2,                  // prioridade da task (2 é mais alta que 1)
        NULL                // ponteiro para o handle da task (não usado)
    );
    xTaskCreate(
        tarefa_gravacao,    // ponteiro para a função tarefa_gravacao
        "tarefa_gravacao",  // nome da tarefa
        4096,               // tamanho da stack para essa task (4 KB)
        NULL,               // parâmetro para a task (não usado)
        1,                  // prioridade (1 é mais baixa que 2) -> preferencia para task de coleta
        NULL                // ponteiro para o handle da task (não usado)
    );
}
