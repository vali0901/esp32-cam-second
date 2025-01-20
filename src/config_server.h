#pragma once

#include <esp_https_server.h>
#include <esp_http_server.h>
#include <esp32-hal-gpio.h>
#include <SPIFFS.h>
#include <unordered_map>

#include "config.h"

extern std::unordered_map<std::string, std::string> access_tokens;

httpd_handle_t startConfigServer(uint8_t cert_pem[], uint8_t key_pem[], size_t cert_pem_len, size_t key_pem_len);