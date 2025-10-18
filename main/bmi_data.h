// bmi160_data.h
#ifndef BMI160_DATA_H
#define BMI160_DATA_H
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
// Structure to hold bmi data
typedef struct
{
    int seq;
    float ax, ay, az; // g
    float gx, gy, gz; // dps
} bmi160_data_t;

// Structure to hold multiple queue handles for the reading task
typedef struct
{
    QueueHandle_t bmi_upload_data_queue; // Queue for processing task
    QueueHandle_t bmi_process_data_queue;     // Queue for logging or other consumer
} bmi_queues_t;

// Function prototypes
void poll_sensor(void *pvParameters);
void bmi_send_sensor_data_task(void *pvParameters);
void detect_brake_1(void *pvParameters);

#endif