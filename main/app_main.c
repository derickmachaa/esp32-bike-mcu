#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "protocol_examples_common.h"
#include "string.h"
#include "applications.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "driver/gpio.h"
#include <sys/socket.h>

const char *TAG = "BMCU: main";
#define BLINK_GPIO 2
static uint8_t s_led_state = 0;
esp_reset_reason_t reason;

// void task4(void *pvParameters)
// {
//     for (;;)
//     {

//         uint8_t chip_id = 0;
//         bmi160_read_reg(0x00, &chip_id, 1);
//         ESP_LOGI("SPI CODE", "Chip ID: 0x%02X", chip_id);

//         if (chip_id != 0xD1)
//         {
//             ESP_LOGE("SPI CODE", "BMI160 not detected!");
//             return;
//         }
//         vTaskDelay(1000 / portTICK_PERIOD_MS);
//     }
// }

static void configure_led(void)
{
    printf("Example configured to blink GPIO LED!");
    gpio_reset_pin(BLINK_GPIO);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
}

static void configure_gpio(void)
{
    printf("Configuring GPIO PINS");
    gpio_reset_pin(0);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(0, GPIO_MODE_OUTPUT);
    gpio_set_level(0, 0);
    vTaskDelay(1 / portTICK_PERIOD_MS);
    gpio_set_level(0, 1);
}

void app_main(void)
{
    // state why we are up
    reason = esp_reset_reason();
    ESP_LOGI(TAG, "Reset reason: %d", reason);
    // configure_gpio();
    ESP_LOGI(TAG, "Main app Start");
    // Initialize NVS.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        // 1.OTA app partition table has a smaller NVS partition size than the non-OTA
        // partition table. This size mismatch may cause NVS initialization to fail.
        // 2.NVS partition contains data in new format and cannot be recognized by this version of code.
        // If this happens, we erase NVS partition and initialize NVS again.
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    ESP_ERROR_CHECK(err);
    // init and start ota
    bmi_spi_init();
    i2c_init();
   // ota_init();
    // spi_init();
    // configure_led();
    //xTaskCreate(&simple_ota_example_task, "ota_example_task", 8192, NULL, 5, NULL);
    xTaskCreate(&poll_sensor, "BMI Sensor", 4096, NULL, 1, NULL);
    //xTaskCreate(&udp_server_task, "udp_server", 4096, (void *)AF_INET, 1, NULL);
    // xTaskCreate(udp_client_task, "udp_client", 4096, NULL, 5, NULL);
    // xTaskCreate(&led_run, "Task 4", 2048, NULL, 1, NULL);
}
