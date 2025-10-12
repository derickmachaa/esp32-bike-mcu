# esp32-bike-mcu
a diy bike mcu based on esp32.
This is a bike mcu that combines BMI160 to detect when deceleration is happening and turns on the lights


| Supported Targets | ESP32-C3 | 
| ----------------- | -------- |
| IMU in Use (SPI protocol)       | BMI160  |

## wifi config
Wifi configuration is defined inside wifi_creds.h feel free to modify to any value.

Current value is
| WIFI SSID | WIFI PASS |
|-----------| -------|
|M4Ch44  | #Iamroot |


## Configuration
Run `idf.py menuconfig`.
