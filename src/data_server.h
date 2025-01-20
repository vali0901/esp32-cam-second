#pragma once

#include <SPIFFS.h>
#include <esp_https_server.h>
#include <esp_http_server.h>
#include <esp_camera.h>
#include <esp32-hal-gpio.h>

#include "config.h"

extern volatile bool streamActive;
extern volatile bool flashlightActive;
extern volatile bool buttonPressed;
extern volatile int clients_connected;

extern httpd_handle_t startCameraServer(uint8_t cert_pem[], uint8_t key_pem[], size_t cert_pem_len, size_t key_pem_len);
