Comandos:

1. source ~/esp/esp-idf/export.sh
2. idf.py set-target esp32 (so uma vez)
3. idf.py build
4. idf.py -p /dev/tty.usbserial-23210 -b 115200 flash monitor
5. idf.py fullclean (apagar diretorio build e cache)

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
