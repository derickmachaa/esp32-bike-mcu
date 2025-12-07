#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "semaphore.h"
#include "runtime_data.h"

#define I2C_MASTER_SCL_IO CONFIG_I2C_MASTER_SCL        /*!< GPIO number used for I2C master clock */
#define I2C_MASTER_SDA_IO CONFIG_I2C_MASTER_SDA        /*!< GPIO number used for I2C master data  */
#define I2C_MASTER_NUM I2C_NUM_0                       /*!< I2C port number for master dev */
#define I2C_MASTER_FREQ_HZ CONFIG_I2C_MASTER_FREQUENCY /*!< I2C master clock frequency */
#define I2C_MASTER_TX_BUF_DISABLE 0                    /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE 0                    /*!< I2C master doesn't need buffer */
#define I2C_MASTER_TIMEOUT_MS 1000

#define MPU9250_SENSOR_ADDR 0x68         /*!< Address of the MPU9250 sensor */
#define MPU9250_WHO_AM_I_REG_ADDR 0x75   /*!< Register addresses of the "who am I" register */
#define MPU9250_PWR_MGMT_1_REG_ADDR 0x6B /*!< Register addresses of the power management register */
#define MPU9250_RESET_BIT 7
#define TAG "BMCU: i2c"

static SemaphoreHandle_t cmd_mutex = NULL;
// LED address
#define LED_ADDR 0x8
static i2c_master_dev_handle_t dev_led;
static i2c_master_dev_handle_t dev_mpu;

/**
 * @brief Read a sequence of bytes from a MPU9250 sensor registers
 */
static esp_err_t mpu9250_register_read(i2c_master_dev_handle_t dev_handle, uint8_t reg_addr, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(dev_handle, &reg_addr, 1, data, len, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

/**
 * @brief Write a byte to a MPU9250 sensor register
 */
static esp_err_t mpu9250_register_write_byte(i2c_master_dev_handle_t dev_handle, uint8_t reg_addr, uint8_t data)
{
    uint8_t write_buf[2] = {reg_addr, data};
    return i2c_master_transmit(dev_handle, write_buf, sizeof(write_buf), I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

/**
 * @brief i2c master initialization
 */
static void i2c_master_init(i2c_master_bus_handle_t *bus_handle)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, bus_handle));

    i2c_device_config_t dev_attiny_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = LED_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(*bus_handle, &dev_attiny_config, &dev_led));
    i2c_device_config_t dev_mpu_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MPU9250_SENSOR_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(*bus_handle, &dev_mpu_config, &dev_mpu));
}

static esp_err_t led_write_byte(i2c_master_dev_handle_t dev_handle, uint8_t reg_addr, uint8_t data)
{
    uint8_t write_buf[1] = {data};
    return i2c_master_transmit(dev_handle, write_buf, sizeof(write_buf), I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

void send_brake_signal(void)
{
    xSemaphoreTake(cmd_mutex, portMAX_DELAY);
    lastCommand = 'B';
    led_write_byte(dev_led, LED_ADDR, lastCommand);
    ESP_LOGI(TAG, "sent B");
    xSemaphoreGive(cmd_mutex);
}
void send_left_signal(void)
{
    xSemaphoreTake(cmd_mutex, portMAX_DELAY);
    lastCommand = 'L';
    led_write_byte(dev_led, LED_ADDR, lastCommand);
    xSemaphoreGive(cmd_mutex);
}
void send_right_signal(void)
{
    xSemaphoreTake(cmd_mutex, portMAX_DELAY);
    lastCommand = 'R';
    led_write_byte(dev_led, LED_ADDR, lastCommand);
    xSemaphoreGive(cmd_mutex);
}
void send_off_signal(void)
{
    xSemaphoreTake(cmd_mutex, portMAX_DELAY);
    lastCommand = 'O';
    led_write_byte(dev_led, LED_ADDR, lastCommand);
    xSemaphoreGive(cmd_mutex);
}
void send_normal_signal(void)
{
    xSemaphoreTake(cmd_mutex, portMAX_DELAY);
    lastCommand = 'N';
    led_write_byte(dev_led, LED_ADDR, lastCommand);
    xSemaphoreGive(cmd_mutex);
}

void led_run(void *pvParameters)
{
    while (1)
    {
        lastCommand = 'L';
        led_write_byte(dev_led, LED_ADDR, lastCommand);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        lastCommand = 'R';
        led_write_byte(dev_led, LED_ADDR, lastCommand);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void i2c_init(void)
{
    cmd_mutex = xSemaphoreCreateMutex();
    uint8_t data[2];
    i2c_master_bus_handle_t bus_handle;
    i2c_master_init(&bus_handle);
    ESP_LOGI(TAG, "I2C initialized successfully");
    // send L
    send_left_signal();
    vTaskDelay(1200 / portTICK_PERIOD_MS);
    // send R
    send_right_signal();
    vTaskDelay(1200 / portTICK_PERIOD_MS);
    // send B
    send_brake_signal();
    vTaskDelay(1200 / portTICK_PERIOD_MS);
    if (BMCU_TAILLIGHT_ENABLE)
    {
        send_normal_signal();
    }
    else
    {
        send_off_signal();
    }
    // if (retuned != ESP_OK)
    // {
    //     ESP_LOGW("I2C", "Nothing Sent");
    //     // Do NOT reset, just skip OTA for now
    //     return;
    // }
    /* Read the MPU9250 WHO_AM_I register, on power up the register should have the value 0x71 */
    // ESP_ERROR_CHECK(mpu9250_register_read(dev_handle, MPU9250_WHO_AM_I_REG_ADDR, data, 1));
    // ESP_LOGI(TAG, "WHO_AM_I = %X", data[0]);

    // /* Demonstrate writing by resetting the MPU9250 */
    // ESP_ERROR_CHECK(mpu9250_register_write_byte(dev_handle, MPU9250_PWR_MGMT_1_REG_ADDR, 1 << MPU9250_RESET_BIT));

    // ESP_ERROR_CHECK(i2c_master_bus_rm_device(dev_led));
    // ESP_ERROR_CHECK(i2c_del_master_bus(bus_handle));
    // ESP_LOGI(TAG, "I2C de-initialized successfully");
}