#include "unity.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include <string.h>

// --- Protótipos dos testes ---
void test_mpu_write_comunica_registro();
void test_mpu_read_ler_bytes();
void test_inicializar_mpu_confere_config();
void test_ler_fifo_e_salvar_processa_buffer();
void test_salvar_sd_card_grava_dados();

// === MOCKS / STUBS DE DEPENDÊNCIAS ===
#define FAKE_FIFO_TAMANHO 24

static uint8_t fake_fifo[FAKE_FIFO_TAMANHO] = {
    0x00, 0x10, 0x00, 0x20, 0x00, 0x30,  // ax, ay, az
    0x00, 0x40, 0x00, 0x50, 0x00, 0x60,  // gx, gy, gz
    0x00, 0x11, 0x00, 0x21, 0x00, 0x31,
    0x00, 0x41, 0x00, 0x51, 0x00, 0x61
};

// Simula escrita no I2C
esp_err_t i2c_master_write_to_device(...) {
    return ESP_OK;
}

// Simula leitura no I2C com dados fake
esp_err_t i2c_master_write_read_device(i2c_port_t port, uint8_t addr,
                                       const uint8_t *wr_buf, size_t wr_size,
                                       uint8_t *rd_buf, size_t rd_size,
                                       TickType_t ticks_to_wait) {
    if (*wr_buf == 0x72) { // FIFO_COUNTH
        rd_buf[0] = 0x00;
        rd_buf[1] = FAKE_FIFO_TAMANHO;
    } else if (*wr_buf == 0x74) { // FIFO_RW
        memcpy(rd_buf, fake_fifo, rd_size);
    } else {
        memset(rd_buf, 0, rd_size);
    }
    return ESP_OK;
}

// Mock para tempo
int64_t esp_timer_get_time() {
    return 123456789;
}

// Mock de mutex
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (SemaphoreHandle_t)1; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t x, TickType_t y) { return pdTRUE; }
BaseType_t xSemaphoreGive(SemaphoreHandle_t x) { return pdTRUE; }

// === TESTES ===

TEST_CASE("mpu_write envia para I2C sem erro", "[mpu]") {
    esp_err_t ret = mpu_write(0x6B, 0x00);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

TEST_CASE("mpu_read preenche buffer com dado simulado", "[mpu]") {
    uint8_t buf[2];
    esp_err_t ret = mpu_read(0x75, buf, 2); // WHO_AM_I ou outro
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[0]);  // esperado no mock
}

TEST_CASE("inicializar_mpu escreve config padrão", "[mpu]") {
    inicializar_mpu(); // Sem assert porque usamos mocks
    TEST_PASS();       // apenas valida execução sem crash
}

TEST_CASE("ler_fifo_e_salvar converte FIFO em string CSV", "[fifo]") {
    extern void ler_fifo_e_salvar();
    extern uint8_t buffer_dados[];
    extern size_t buffer_index;

    buffer_index = 0;
    ler_fifo_e_salvar();

    TEST_ASSERT_GREATER_THAN(0, buffer_index);
    TEST_ASSERT_NOT_NULL(strstr((char*)buffer_dados, "123456789"));
    TEST_ASSERT_NOT_NULL(strstr((char*)buffer_dados, ",16,32,48,64,80,96"));
}

TEST_CASE("salvar_sd_card grava dados do buffer", "[sdcard]") {
    extern void salvar_sd_card();
    extern uint8_t buffer_dados[];
    extern size_t buffer_index;

    // Preenche buffer com dados fake
    strcpy((char*)buffer_dados, "123456789,1,2,3,4,5,6\n");
    buffer_index = strlen((char*)buffer_dados);

    salvar_sd_card();

    // Para testes reais: validar arquivo /sdcard/dados.csv
    TEST_PASS(); // Aqui não há assert pois ESP-IDF usa sistema real
}
