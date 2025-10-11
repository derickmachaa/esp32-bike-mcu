#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = "BMCU: storage";
nvs_handle_t my_handle;
esp_err_t err;
void storage_write_char(char my_char)
{
    //open
    err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error opening NVS handle!");
        return;
    }
    // Write a char safely
    err = nvs_set_i8(my_handle, "mychar", my_char); // store as 8-bit integer
    if (err == ESP_OK)
    {
        nvs_commit(my_handle); // VERY important
        ESP_LOGI(TAG, "Stored char '%c' in flash", my_char);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to store char");
    }
    //close
    nvs_close(my_handle);
}
int8_t storage_read_char()
{
    //open
    err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error opening NVS handle!");
    }
    // Read it back
    int8_t read_char = 0;
    err = nvs_get_i8(my_handle, "mychar", &read_char);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Read char from flash: '%c'", read_char);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to read char");
    }

     //close
    nvs_close(my_handle);
    return read_char;
}