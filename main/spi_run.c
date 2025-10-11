#define GPIO_DATA_READY 3
#define PIN_NUM_MOSI 6
#define PIN_NUM_MISO 5
#define PIN_NUM_CLK 4
#define PIN_NUM_CS 0

#define SENDER_HOST SPI2_HOST
#include <stdio.h>
#include <string.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#define TAG "BMI160"

spi_device_handle_t bmi160_handle;

/* ===== SPI INIT ===== */
void bmi160_spi_init(void) {
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 10 * 1000 * 1000, // 10 MHz
        .mode = 0,
        .spics_io_num = PIN_NUM_CS,
        .queue_size = 1,
    };

    ESP_ERROR_CHECK(spi_bus_initialize(SENDER_HOST, &buscfg, SPI_DMA_CH_AUTO));
    ESP_ERROR_CHECK(spi_bus_add_device(SENDER_HOST, &devcfg, &bmi160_handle));

    ESP_LOGI(TAG, "SPI bus initialized");
}

/* ===== LOW LEVEL READ/WRITE ===== */
esp_err_t bmi160_write_reg(uint8_t reg, uint8_t data) {
    uint8_t tx[2] = {reg & 0x7F, data}; // write = MSB=0
    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = tx,
    };
    return spi_device_transmit(bmi160_handle, &t);
}

esp_err_t bmi160_read_reg(uint8_t reg, uint8_t *data, size_t len) {
    uint8_t tx[len + 1];
    uint8_t rx[len + 1];
    memset(tx, 0, sizeof(tx));
    tx[0] = reg | 0x80; // read = MSB=1

    spi_transaction_t t = {
        .length = (len + 1) * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };

    esp_err_t ret = spi_device_transmit(bmi160_handle, &t);
    if (ret == ESP_OK) {
        memcpy(data, rx + 1, len); // skip dummy
    }
    return ret;
}

/* ===== SENSOR INIT ===== */
void bmi160_init(void) {
    uint8_t chip_id = 0;
    bmi160_read_reg(0x00, &chip_id, 1);
    ESP_LOGI(TAG, "Chip ID: 0x%02X", chip_id);

    if (chip_id != 0xD1) {
        ESP_LOGE(TAG, "BMI160 not detected!");
        return;
    }

    // Wake up accelerometer
    bmi160_write_reg(0x7E, 0x11); // ACC_MODE_NORMAL
    vTaskDelay(pdMS_TO_TICKS(50));

    // Wake up gyroscope
    bmi160_write_reg(0x7E, 0x15); // GYR_MODE_NORMAL
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "BMI160 initialized");
}

/* ===== READ ACCEL & GYRO ===== */
typedef struct {
    float ax, ay, az; // g
    float gx, gy, gz; // dps
} bmi160_data_t;

void bmi160_read_data(bmi160_data_t *out) {
    uint8_t buf[12];

    // Gyro first (0x0C-0x11)
    bmi160_read_reg(0x0C, buf, 6);
    int16_t gx = (int16_t)((buf[1] << 8) | buf[0]);
    int16_t gy = (int16_t)((buf[3] << 8) | buf[2]);
    int16_t gz = (int16_t)((buf[5] << 8) | buf[4]);

    // Accel (0x12-0x17)
    bmi160_read_reg(0x12, buf, 6);
    int16_t ax = (int16_t)((buf[1] << 8) | buf[0]);
    int16_t ay = (int16_t)((buf[3] << 8) | buf[2]);
    int16_t az = (int16_t)((buf[5] << 8) | buf[4]);

    // Convert to physical units
    out->ax = ax * 0.000061f;   // g
    out->ay = ay * 0.000061f;
    out->az = az * 0.000061f;

    out->gx = gx * 0.061f;      // °/s
    out->gy = gy * 0.061f;
    out->gz = gz * 0.061f;
}

/* ===== MAIN TASK ===== */
void app_main(void) {
    bmi160_spi_init();
    bmi160_init();

    while (1) {
        bmi160_data_t d;
        bmi160_read_data(&d);

        ESP_LOGI(TAG, "Accel[g]: X=%.3f Y=%.3f Z=%.3f | Gyro[dps]: X=%.2f Y=%.2f Z=%.2f",
                 d.ax, d.ay, d.az, d.gx, d.gy, d.gz);

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

