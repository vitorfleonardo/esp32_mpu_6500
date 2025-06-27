# 🔌 Como gravar e rodar o firmware na ESP32S

## 1. Abra o terminal e execute os comando em sequencia:

```bash
source ~/esp/esp-idf/export.sh         # Carrega variáveis de ambiente do ESP-IDF
idf.py set-target esp32                # Define o alvo como ESP32 (necessário apenas a primeira vez)
idf.py build                           # Compila o projeto
idf.py -p /dev/tty.usbserial-0001 -b 115200 flash monitor  # Grava na ESP32 e abre o monitor serial
```

## Limpeza do build (se necessário):

```bash
idf.py fullclean
```

# Registradores do MPU

## MPU_PWR_MGMT_1 (0x6B) – Controle de Energia

📖 Função:
Esse registrador controla o modo de energia do chip, fonte de clock e pode até dar um reset de hardware.

| Bit |     Nome     |                              Descriçao                              |
| :-: | :----------: | :-----------------------------------------------------------------: |
|  7  | DEVICE_RESET |      Se 1, reinicia todos os registradores para valores padrão      |
|  6  |    SLEEP     |      Se 1, coloca o chip em modo de sono. Nenhum dado é gerado      |
|  3  |    CYCLE     | Se 1, habilita o modo "ciclo de amostragem" para economizar energia |
| 2:0 |    CLKSEL    |            Seleciona a fonte de clock usada internamente            |

O chip precisa de um clock para funcionar (duas formas):

- 000: Clock interno de 8 MHz (menos preciso)
- 001: PLL com giroscópio X (mais estável — uso padrão)

## MPU_SMPLRT_DIV (0x19) – Divisor de Taxa de Amostragem

📖 Função:
Esse registrador define com que frequência os dados são passados para os registradores de saída ou FIFO.

Fórmula (com DLPF ativo):

```
Sample Rate = Gyro Output Rate / (1 + SMPLRT_DIV)
```

- Gyro Output Rate = 1000 Hz com DLPF ativado
- Exemplo: SMPLRT_DIV = 4 → Sample Rate = 1000 / (1 + 4) = 200 Hz
- Permite reduzir a frequência de amostragem para economizar energia ou evitar saturação da FIFO.

## MPU_CONFIG (0x1A) – Filtro Digital (DLPF) e Sincronização

📖 Função:
Controla os filtros passa-baixa digitais (DLPF) e define se você quer sincronizar os sensores com eventos externos.

O que é DLPF?

- DLPF = Digital Low Pass Filter
- Reduz o ruído de alta frequência nas medições.

| DLPF_CFG | Bandwidth |  Delay  | Sample Rate |
| :------: | :-------: | :-----: | :---------: |
|    0     |  260 Hz   | 0.0 ms  |    1 kHz    |
|    1     |  184 Hz   | 2.0 ms  |    1 kHz    |
|    2     |   94 Hz   | 3.0 ms  |    1 kHz    |
|    3     |   44 Hz   | 4.9 ms  |    1 kHz    |
|    4     |   21 Hz   | 8.5 ms  |    1 kHz    |
|    5     |   10 Hz   | 13.8 ms |    1 kHz    |
|    6     |   5 Hz    | 19.0 ms |    1 kHz    |

## MPU_GYRO_CONFIG (0x1B) – Escala e Self-Test do Giroscópio

📖 Função:
Define a sensibilidade angular e ativa testes internos.

| FS_SEL |   Faixa   | Sensibilidade |
| :----: | :-------: | :-----------: |
|   0    | ±250 °/s  | 131 LSB/(°/s) |
|   1    | ±500 °/s  |     65.5      |
|   2    | ±1000 °/s |     32.8      |
|   3    | ±2000 °/s |     16.4      |

- Quanto maior a faixa, menor a precisão (resolução).

## MPU_ACCEL_CONFIG (0x1C) – Escala e Self-Test do Acelerômetro

📖 Função:
Define a sensibilidade linear do acelerômetro (aceleração).

| AFS_SEL | Faixa | Sensibilidade |
| :-----: | :---: | :-----------: |
|    0    |  ±2g  |  16384 LSB/g  |
|    1    |  ±4g  |     8192      |
|    2    |  ±8g  |     4096      |
|    3    | ±16g  |     2048      |

- Quanto maior a faixa, menor a precisão (resolução).

## MPU_FIFO_EN (0x23) – Habilita sensores na FIFO

📖 Função:
Seleciona quais dados são enviados ao buffer FIFO.

| Bit |     Nome     | Significado  |
| :-: | :----------: | :----------: |
|  6  | `XG_FIFO_EN` |    Gyro X    |
|  5  | `YG_FIFO_EN` |    Gyro Y    |
|  4  | `ZG_FIFO_EN` |    Gyro Z    |
|  3  |   `ACCEL`    | Acelerômetro |

## MPU_USER_CTRL (0x6A) – Controle da FIFO

📖 Função:
Habilita a FIFO e funções do I²C interno (não usado no modo slave).

| Bit |    Nome    |                 Descrição                  |
| :-: | :--------: | :----------------------------------------: |
|  6  | `FIFO_EN`  |                 Ativa FIFO                 |
|  2  | `FIFO_RST` | Reseta FIFO (recomenda-se antes de ativar) |

## MPU_FIFO_COUNTH (0x72) e MPU_FIFO_COUNTL (0x73)

📖 Função:
Contêm o número de bytes disponíveis para leitura na FIFO.

- COUNTH = parte alta do valor de 16 bits (bits 15–8)
- COUNTL = parte baixa do valor de 16 bits (bits 7–0)

## MPU_FIFO_RW (0x74) – Registro de leitura/escrita da FIFO

📖 Função:
Contêm o número de bytes disponíveis para leitura na FIFO.

- Usado para ler os dados brutos armazenados na FIFO (ordem FIFO).
- Cada leitura consome os dados da FIFO.
- Cada amostra completa tem 12 bytes (6 eixos x 2 bytes).

# uso de bibliotcas

```bash
#include <stdio.h>
// ✔️ Usada para FILE*, fopen, fwrite, fclose

#include "driver/i2c.h"
// ✔️ Usada para comunicação I2C com o MPU-6500:
//    - i2c_param_config()
//    - i2c_driver_install()
//    - i2c_master_write_to_device()
//    - i2c_master_write_read_device()
//    - i2c_config_t

#include "freertos/FreeRTOS.h"
// ✔️ Usada para macros do FreeRTOS como:
//    - pdMS_TO_TICKS()
//    - portMAX_DELAY

#include "freertos/task.h"
// ✔️ Usada para criação e controle de tarefas:
//    - xTaskCreate()
//    - vTaskDelay()

#include "esp_log.h"
// ✔️ Usada para log de mensagens no terminal serial:
//    - ESP_LOGI()
//    - ESP_LOGE()

#include "esp_err.h"
// ✔️ Usada para tipo e mensagens de erro:
//    - esp_err_t
//    - esp_err_to_name()

#include "freertos/semphr.h"
// ✔️ Usada para controle de concorrência com mutex:
//    - xSemaphoreCreateMutex()
//    - xSemaphoreTake()
//    - xSemaphoreGive()

#include "esp_vfs_fat.h"
// ✔️ Usada para montagem do sistema de arquivos FAT no SD card SPI:
//    - esp_vfs_fat_sdspi_mount()
//    - esp_vfs_fat_sdmmc_mount_config_t (e sdspi equivalente)

#include "driver/sdmmc_host.h"
// ✔️ Usada para host SPI ou SDMMC:
//    - SDSPI_HOST_DEFAULT()
//    - sdmmc_host_t

#include "driver/sdmmc_defs.h"
// ✔️ Usada para constantes e definições auxiliares do SD:
//    - allocation_unit_size

#include "sdmmc_cmd.h"
// ✔️ Usada para:
//    - tipo sdmmc_card_t
//    - leitura e escrita no SD card (implícito via mount)

#include "sdkconfig.h"
// ✔️ Usada indiretamente via macros de build (como CONFIG_FREERTOS_HZ)
//    - Presente como dependência do ESP-IDF (não usada diretamente, mas requerida)

#include <string.h>
// ✔️ Usada para manipulação de strings:
//    - snprintf()

#include <stdlib.h>
// ❌ **Não usada** no código atual
//    - Pode ser removida a menos que vá usar malloc(), atoi(), etc.

#include "driver/gpio.h"
// ✔️ Usada para configurar e utilizar GPIO:
//    - gpio_config()
//    - gpio_install_isr_service()
//    - gpio_isr_handler_add()
//    - gpio_config_t

#include "esp_timer.h"
// ✔️ Usada para obter timestamp com microssegundos:
//    - esp_timer_get_time()

#include "driver/spi_common.h"
// ✔️ Usada para inicializar barramento SPI:
//    - spi_bus_initialize()

#include "driver/sdspi_host.h"
// ✔️ Usada para configurar o SD card via SPI:
//    - sdspi_device_config_t
//    - SDSPI_DEVICE_CONFIG_DEFAULT()

```
