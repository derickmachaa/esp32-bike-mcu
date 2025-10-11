#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"

extern const char *TAG;

#define I2C_MASTER_SCL_IO 4                            /*!< GPIO number used for I2C master clock */
#define I2C_MASTER_SDA_IO 6                            /*!< GPIO number used for I2C master data  */
#define I2C_MASTER_NUM I2C_NUM_0                       /*!< I2C port number for master dev */
#define I2C_MASTER_FREQ_HZ CONFIG_I2C_MASTER_FREQUENCY /*!< I2C master clock frequency */
#define I2C_MASTER_TX_BUF_DISABLE 0                    /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE 0                    /*!< I2C master doesn't need buffer */
#define I2C_MASTER_TIMEOUT_MS 1000
#define GPIO_SAO 5
#define GPIO_CS 0
#define GPIO_DATA_READY 3

#define BMI_SENSOR_ADDR 0x69       /*!< Address of the BMI sensor */
#define BMI_WHO_AM_I_REG_ADDR 0x00 /*!< Register addresses of the "who am I" register */

static i2c_master_dev_handle_t dev_mpu;

/**
 * @brief Read a sequence of bytes from a BMI sensor registers
 */
static esp_err_t bmi_register_read(i2c_master_dev_handle_t dev_handle, uint8_t reg_addr, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(dev_handle, &reg_addr, 1, data, len, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

/**
 * @brief Write a byte to a MPU9250 sensor register
 */
static esp_err_t bmi_register_write_byte(i2c_master_dev_handle_t dev_handle, uint8_t reg_addr, uint8_t data)
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
    i2c_device_config_t dev_mpu_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BMI_SENSOR_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(*bus_handle, &dev_mpu_config, &dev_mpu));
}
void i2c_scan(i2c_master_bus_handle_t bus_handle)
{
    ESP_LOGI(TAG, "Starting I2C scan...");
    for (uint8_t addr = 0x03; addr < 0x78; addr++)
    {
        esp_err_t ret = i2c_master_probe(bus_handle, addr, 100 / portTICK_PERIOD_MS);
        if (ret == ESP_OK)
        {
            ESP_LOGI(TAG, "I2C device found at address 0x%02X", addr);
        }
    }
    ESP_LOGI(TAG, "I2C scan completed.");
}

void i2c_init(void)
{

    uint8_t data[2];
    i2c_master_bus_handle_t bus_handle;
    i2c_master_init(&bus_handle);
    i2c_scan(bus_handle);
    uint8_t count = 0;
    ESP_LOGI(TAG, "I2C initialized successfully");
    while (1)
    {
        esp_err_t retuned = bmi_register_read(dev_mpu, BMI_WHO_AM_I_REG_ADDR, data, 1);
        if (retuned != ESP_OK)
        {
            ESP_LOGW("I2C", "Nothing Sent");
            // Do NOT reset, just skip OTA for now
        }
        else
        {
            if (data[0] == 0xD1)
            {
                ESP_LOGI(TAG, "found BMI 160 reseting bit to SPI");
                if (count < 1)
                {
                    retuned = bmi_register_write_byte(dev_mpu, 0x70, 0x10);
                    if (retuned == ESP_OK)
                    {
                        ESP_LOGI(TAG, "SPI SET");
                        count++;
                    }
                }
            }
            ESP_LOGI(TAG, "WHO_AM_I = %X", data[0]);
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}