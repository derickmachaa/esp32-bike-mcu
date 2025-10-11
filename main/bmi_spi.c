#include <stdio.h>
#include <string.h>
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

#define GPIO_DATA_READY 3
#define PIN_NUM_MOSI 6
#define PIN_NUM_MISO 5
#define PIN_NUM_CLK 4
#define PIN_NUM_CS 0
#define BMI_CMD 0x7E
#define BMI_ACC_MODE_NORMAL 0x11
#define BMI_GRY_MODE_NORMAL 0x15
#define SENDER_HOST SPI2_HOST
#define TAG "BMCU: bmi"
extern void udp_send_data(char *payload);
extern void udp_socket_init(void);
extern void udp_socket_close(void);

/*
SPI sender (master) example.

This example is supposed to work together with the SPI receiver. It uses the standard SPI pins (MISO, MOSI, SCLK, CS) to
transmit data over in a full-duplex fashion, that is, while the master puts data on the MOSI pin, the slave puts its own
data on the MISO pin.

This example uses one extra pin: GPIO_DATA_READY is used as a handshake pin. The slave makes this pin high as soon as it is
ready to receive/send data. This code connects this line to a GPIO interrupt which gives the rdySem semaphore. The main
task waits for this semaphore to be given before queueing a transmission.
*/

spi_device_handle_t bmi160_handle;

// The semaphore indicating the slave is ready to receive stuff.
static QueueHandle_t rdySem = NULL;

/*
This ISR is called when the handshake line goes high.
*/
int count = 0;
static void IRAM_ATTR gpio_handshake_isr_handler(void *arg)
{
    // Sometimes due to interference or ringing or something, we get two irqs after each other. This is solved by
    // looking at the time between interrupts and refusing any interrupt too close to another one.
    static uint32_t lasthandshaketime_us;
    uint32_t currtime_us = esp_timer_get_time();
    uint32_t diff = currtime_us - lasthandshaketime_us;
    if (diff < 1000)
    {
        return; // ignore everything <1ms after an earlier irq
    }
    lasthandshaketime_us = currtime_us;

    // Give the semaphore.
    BaseType_t mustYield = false;
    xSemaphoreGiveFromISR(rdySem, &mustYield);
    count++;
    if (mustYield)
    {
        portYIELD_FROM_ISR();
    }
}
static void data_gpio(void)
{
    // GPIO config for the handshake line.
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_POSEDGE,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,
        .pin_bit_mask = BIT64(GPIO_DATA_READY),
    };

    // Set up handshake line interrupt.
    gpio_config(&io_conf);
    gpio_install_isr_service(0);
    gpio_set_intr_type(GPIO_DATA_READY, GPIO_INTR_POSEDGE);
    gpio_isr_handler_add(GPIO_DATA_READY, gpio_handshake_isr_handler, NULL);
}
/* ===== SPI INIT ===== */
static void spi_init(void)
{
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096};

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
static esp_err_t bmi160_write_reg(uint8_t reg, uint8_t data)
{
    uint8_t tx[2] = {reg & 0x7F, data}; // write = MSB=0
    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = tx,
    };
    return spi_device_transmit(bmi160_handle, &t);
}

static esp_err_t bmi160_read_reg(uint8_t reg, uint8_t *data, size_t len)
{
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
    if (ret == ESP_OK)
    {
        memcpy(data, rx + 1, len); // skip dummy
    }
    return ret;
}

/* ===== SENSOR INIT ===== */
static bool bmi160_init(void)
{
    bool bmi_ready = false;

    // dummy to read 0x7f as stated in the datasheet before beginning SPI
    uint8_t dummy = 0;
    bmi160_read_reg(0x7F, &dummy, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    uint8_t chip_id = 0;
    bmi160_read_reg(0x00, &chip_id, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_LOGI(TAG, "Chip ID: 0x%02X", chip_id);

    if (chip_id == 0xD1)
    {
        bmi_ready = true;
        ESP_LOGI(TAG, "BMI160 detected!");

        // enable interrupt on pin 1
        //  1. Configure INT1 pin (push-pull, active high)
        bmi160_write_reg(0x53, 0x08);

        // 2. Enable data-ready interrupt
        bmi160_write_reg(0x51, 0x10);

        // 3. Map data-ready interrupt to INT1
        bmi160_write_reg(0x56, 0x80);

        // 4. Set ACC_CONF to 200HZ
        bmi160_write_reg(0x40, 0x29); // 200Hz, normal bandwidth, no undersampling

        // 5. set GRY_CONF to 200HZ also
        bmi160_write_reg(0x42, 0x29); // 200Hz, normal bandwidth, no undersampling

        // Enable latching until interrupt is cleared
        bmi160_write_reg(0x54, 0xF8);

        // Wake up accelerometer
        bmi160_write_reg(BMI_CMD, BMI_ACC_MODE_NORMAL); // ACC_MODE_NORMAL
        vTaskDelay(pdMS_TO_TICKS(50));

        // Wake up gyroscope
        bmi160_write_reg(BMI_CMD, BMI_GRY_MODE_NORMAL); // GYR_MODE_NORMAL
        vTaskDelay(pdMS_TO_TICKS(100));

        ESP_LOGI(TAG, "BMI160 initialized");
    }
    else
    {
        ESP_LOGE(TAG, "BMI160 not detected");
    }
    return bmi_ready;
}

/* ===== READ ACCEL & GYRO ===== */
typedef struct
{
    int seq;
    float ax, ay, az; // g
    float gx, gy, gz; // dps
} bmi160_data_t;

static void bmi160_read_data(bmi160_data_t *out)
{
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
    out->ax = ax * 0.000061f; // g
    out->ay = ay * 0.000061f;
    out->az = az * 0.000061f;

    out->gx = gx * 0.061f; // °/s
    out->gy = gy * 0.061f;
    out->gz = gz * 0.061f;
}

/* ===== MAIN TASK ===== */
void bmi_spi_init(void)
{
    // Create the semaphore.
    rdySem = xSemaphoreCreateBinary();
    data_gpio();
    spi_init();

    if (bmi160_init())
    {
        ESP_LOGI(TAG, "SENSOR READY");
    }
}

void poll_sensor(void)
{
    udp_socket_init();
    char msg[128];
    xSemaphoreGive(rdySem);
    bmi160_data_t data;
    data.seq = 0;
    data.ax = 0;
    data.ay = 0;
    data.az = 0;
    data.gx = 0;
    data.gy = 0;
    data.gz = 0;

    int64_t ms = 0;

    while (1)
    {
        // if we timeout before we take the semaphore re init the bmi160
        if (xSemaphoreTake(rdySem, 5000 / portTICK_PERIOD_MS) == pdFALSE)
        {
            ESP_LOGW(TAG, "Semaphore timeout — waited full 5 seconds");
            bmi160_init();
        }
        bmi160_read_data(&data);
        ms = esp_timer_get_time() / 1000;
        // ESP_LOGI(TAG, "count is %d", data.seq);
        // snprintf(msg, sizeof(msg), "Time: %lld | Accel[g]: X=%.3f Y=%.3f Z=%.3f | Gyro[dps]: X=%.2f Y=%.2f Z=%.2f | SeqNo: %d \n", ms, data.ax, data.ay, data.az, data.gx, data.gy, data.gz, data.seq);
        snprintf(msg, sizeof(msg), "%lld,%.3f,%.3f,%.3f,%.2f,%.2f,%.2f,%d \n", ms, data.ax, data.ay, data.az, data.gx, data.gy, data.gz, data.seq);
        udp_send_data(msg);
        data.seq++;
        // ESP_LOGI(TAG, "Accel[g]: X=%.3f Y=%.3f Z=%.3f | Gyro[dps]: X=%.2f Y=%.2f Z=%.2f", d.ax, d.ay, d.az, d.gx, d.gy, d.gz);
        // vTaskDelay(pdMS_TO_TICKS(500));
    }
     udp_socket_close();
}
