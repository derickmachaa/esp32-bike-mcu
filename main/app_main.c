#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "string.h"
#include "applications.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include <sys/socket.h>
#include "bmi_data.h"

const char *TAG = "BMCU: main";

// Task handles
static TaskHandle_t udp_server_task_handle = NULL;
static TaskHandle_t simple_ota_example_task_handle = NULL;
static TaskHandle_t bmi_send_sensor_data_task_handle = NULL;
bool BMCU_TAILLIGHT_ENABLE = true;
// Queues for data
#define QUEUE_LENGTH 10 // Buffer ~50ms at 200Hz
#define QUEUE_ITEM_SIZE sizeof(bmi160_data_t)

// Events for wifi and anything else I need
EventGroupHandle_t s_wifi_event_group = NULL;

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

const char *reset_reason_to_str(esp_reset_reason_t reason)
{
    switch (reason)
    {
    case ESP_RST_UNKNOWN:
        return "UNKNOWN";
    case ESP_RST_POWERON:
        return "POWERON";
    case ESP_RST_EXT:
        return "EXT_RST";
    case ESP_RST_SW:
        return "SW";
    case ESP_RST_PANIC:
        return "PANIC";
    case ESP_RST_INT_WDT:
        return "INT_WDT";
    case ESP_RST_TASK_WDT:
        return "TASK_WDT";
    case ESP_RST_WDT:
        return "WDT";
    case ESP_RST_DEEPSLEEP:
        return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:
        return "BROWNOUT";
    case ESP_RST_SDIO:
        return "SDIO";
    case ESP_RST_USB:
        return "USB_RESET";
    case ESP_RST_JTAG:
        return "JTAG";
    case ESP_RST_EFUSE:
        return "EFUSE";
    case ESP_RST_PWR_GLITCH:
        return "PWR_GLITCH";
    case ESP_RST_CPU_LOCKUP:
        return "CPU_LOCKUP";
    default:
        return "INVALID";
    }
}

void stop_udp_server_task()
{
    if (udp_server_task_handle != NULL)
    {
        vTaskDelete(udp_server_task_handle);
        udp_server_task_handle = NULL;
        ESP_LOGI(TAG, "stopped udp server");
    }
}

void stop_simple_ota_example_task()
{
    if (simple_ota_example_task_handle != NULL)
    {
        vTaskDelete(simple_ota_example_task_handle);
        simple_ota_example_task_handle = NULL;
        ESP_LOGI(TAG, "stopped ota");
    }
}

void stop_bmi_send_sensor_data()
{
    if (bmi_send_sensor_data_task_handle != NULL)
    {
        vTaskDelete(bmi_send_sensor_data_task_handle);
        bmi_send_sensor_data_task_handle = NULL;
        ESP_LOGI(TAG, "stopped upload of data");
    }
}

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
    ESP_LOGI(TAG, "Power up reason: %s", reset_reason_to_str(reason));
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
    // start sensors first wifi is slow will pickup later
    bmi_spi_init();
    i2c_init();
    ble_init();
    // create queues for sending data
    QueueHandle_t bmi_upload_data_queue = xQueueCreate(1, QUEUE_ITEM_SIZE);
    QueueHandle_t bmi_pocess_data_queue = xQueueCreate(QUEUE_LENGTH, QUEUE_ITEM_SIZE);
    // Bundle queue handles for the reading task
    bmi_queues_t data_queue = {
        .bmi_process_data_queue = bmi_pocess_data_queue,
        .bmi_upload_data_queue = bmi_upload_data_queue
    };

    xTaskCreate(&poll_sensor, "BMI Sensor", configMINIMAL_STACK_SIZE+512, (void *)&data_queue , 5, NULL);
    xTaskCreate(&detect_brake_1, "Detect Brake", 4096, (void *)bmi_pocess_data_queue, 4, NULL);
    // create a xevent group
    s_wifi_event_group = xEventGroupCreate();
    err = wifi_init_sta();
    if (err == ESP_OK)
    {
        // proceed to create udp and ota task
        xTaskCreate(&stop_network_task, "network monitor task", configMINIMAL_STACK_SIZE, NULL, 2, NULL);
        xTaskCreate(&simple_ota_example_task, "ota_example_task", 8192, NULL, 5, &simple_ota_example_task_handle);
        xTaskCreate(&udp_server_task, "udp_server", 4096, (void *)AF_INET, 1, &udp_server_task_handle);
        xTaskCreate(&bmi_send_sensor_data_task, "bmi_upload", 4096, (void *)bmi_upload_data_queue, 3, &bmi_send_sensor_data_task_handle);
    }

    // spi_init();
    // configure_led();
    // xTaskCreate(udp_client_task, "udp_client", 4096, NULL, 5, NULL);
    // xTaskCreate(&led_run, "Task 4", 2048, NULL, 1, NULL);
}
