/*This is where rubber meets the road, this file holds the code to detect deceleration and signal to break
Sensor orientation
X-----> Forward
Y-----> Downward
Z------> Sideways
*/

/*I will start stupid and get smarter over time, samples are coming in at 200Hz rate that is roughly every 5ms
    Plan 1. create and array of 10, fill in the samples every 5ms, lag = 5*10=50ms then when array is full go through the array if from array[0] to array[10] is increasing indicate brake
*/
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "bmi_data.h"
#include "math.h"
extern void send_brake_signal(void);
extern void send_normal_signal(void);
extern void send_off_signal(void);
extern bool BMCU_TAILLIGHT_ENABLE;

#define BRAKE_THRESHOLD -0.15f // Braking threshold in g
#define DECEL_MIN_COUNT 4      // Min decreasing samples for braking
static const char *TAG = "BMCU: Algo";

void detect_brake_2(void *pvParameters)
{
    QueueHandle_t detect_brake_data_queue = (QueueHandle_t)pvParameters;
    bmi160_data_t data;
    int count = 0;
    float arr[10] = {0.0};
    UBaseType_t stack_high_water_mark;

    for (;;)
    {
        // wait for data from poll sensor
        // Add stack monitoring
        // stack_high_water_mark = uxTaskGetStackHighWaterMark(NULL);
        // printf("Stack high water mark: %u\n", stack_high_water_mark); // Debug stack usage
        if (xQueueReceive(detect_brake_data_queue, &data, portMAX_DELAY) == pdTRUE)
        {
            int decel_count = 0;
            arr[count] = round(data.ax * 100) / 100;
            count++;
            if (count > 10)
            {
                bool isbreaking = true;
                // calculate now and reset the counter to zero
                for (int i = 0; i < 9; i++)
                {
                    if (arr[i + 1] < arr[i])
                    {
                        decel_count++;
                        continue;
                    }
                    else
                    {
                        isbreaking = false;
                        decel_count--;
                        break;
                    }
                }
                if (isbreaking && arr[10] <= -0.15f)
                {
                    ESP_LOGI("BMCU: Algo", "is breaking");
                    send_brake_signal();
                    vTaskDelay(2000 / portTICK_PERIOD_MS);
                    if (BMCU_TAILLIGHT_ENABLE)
                    {
                        send_normal_signal();
                    }
                    else
                    {
                        send_off_signal();
                    }
                    for (int j = 0; j < 10; j++)
                    {
                        printf("%f\n", arr[j]);
                    }
                }
                count = 0;
            }
        }
    }
}

void detect_brake_1(void *pvParameters)
{
    QueueHandle_t detect_brake_data_queue = (QueueHandle_t)pvParameters;
    if (detect_brake_data_queue == NULL)
    {
        ESP_LOGE(TAG, "Invalid queue handle");
        vTaskSuspend(NULL);
    }
    bmi160_data_t data;
    int count = 0;
    float arr[10] = {0.0};
    float filtered_ax = 0.0f;
    float alpha = 0.2f; // IIR filter, ~10 Hz cutoff at 200 Hz

    for (;;)
    {
        // Log stack usage (uncomment for debugging)
        // UBaseType_t stack_high_water_mark = uxTaskGetStackHighWaterMark(NULL);
        // ESP_LOGI(TAG, "Stack high water mark: %u words (%u bytes)", 
        //          stack_high_water_mark, stack_high_water_mark * sizeof(StackType_t));

        if (xQueueReceive(detect_brake_data_queue, &data, portMAX_DELAY) == pdTRUE)
        {
            // Validate data
            if (isnan(data.ax) || fabs(data.ax) > 16.0f)
            {
                ESP_LOGW(TAG, "Invalid data: ax=%f", data.ax);
                continue;
            }

            // IIR filter to reduce noise
            filtered_ax = alpha * data.ax + (1.0f - alpha) * filtered_ax;
            arr[count] = filtered_ax; // Store filtered data
            count++;

            if (count >= 10) // Fix: Process after 10 samples
            {
                int decel_count = 0;
                float sum_ax = 0.0f;

                // Check for deceleration trend and compute average
                for (int i = 0; i < 9; i++)
                {
                    if (arr[i + 1] < arr[i])
                    {
                        decel_count++;
                    }
                    sum_ax += arr[i];
                }
                sum_ax += arr[9];
                float avg_ax = sum_ax / 10.0f;

                // Detect braking: enough decreasing samples and average below threshold
                if (decel_count >= DECEL_MIN_COUNT && avg_ax <= BRAKE_THRESHOLD)
                {
                    ESP_LOGI(TAG, "Braking detected: avg_ax=%f, decel_count=%d", avg_ax, decel_count);
                    send_brake_signal();
                    // Reduced delay to avoid missing events (adjust as needed)
                    vTaskDelay(2000 / portTICK_PERIOD_MS); // 100 ms
                    if (BMCU_TAILLIGHT_ENABLE)
                    {
                        send_normal_signal();
                    }
                    else
                    {
                        send_off_signal();
                    }
                    // Log array for debugging
                    for (int j = 0; j < 10; j++)
                    {
                        ESP_LOGI(TAG, "arr[%d]=%f", j, arr[j]);
                    }
                }
                count = 0; // Reset counter
            }
        }
    }
}