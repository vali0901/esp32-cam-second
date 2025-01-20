#include <WiFi.h>
#include <esp_http_server.h>
#include <esp_camera.h>
#include <esp_https_server.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <EMailSender.h>

#define MBEDTLS_DEBUG_C
#define MBEDTLS_DEBUG_LEVEL 4


#include "mbedtls/debug.h"
#include "mbedtls/ssl.h"
#include "mbedtls/platform.h"

#include "data_server.h"
#include "config_server.h"

#include "config.h"

#include "cert.h"
#include "key.h"

#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

const char *ap_ssid = "ESP32_AP";
const char *ap_password = "12345678";
const char* hostname = "esp32videocam";

volatile bool CONFIG_MODE = false;
volatile bool DATA_MODE = false;

volatile bool sensorTriggered = false;

httpd_handle_t curr_server;

EMailSender::EMailMessage message;
EMailSender::Attachments attach;
EMailSender::FileDescriptior fileDescriptor;


EMailSender emailSend(SMTP_USER, SMTP_PASS, SMTP_USER, SMTP_USER, SMTP_SERVER, SMTP_PORT);

hw_timer_t *blink_timer = NULL;

void read_access_tokens();
void start_data_mode();
void read_wifi_credentials(std::string &ssid, std::string &password);
void startCamera();
void stop_data_mode();
void start_config_mode();
void stop_config_mode();
void capture_photo();
void setup_smtp();
void send_smtp();
void setup_blink_timer();
void destroy_blink_timer();

void IRAM_ATTR handleButtonPress() {
    buttonPressed = true;
}

void IRAM_ATTR handleSenorTrigger() {
    sensorTriggered = true;
}

void IRAM_ATTR blink() {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
}

void setup() {
    if(setCpuFrequencyMhz(ESP_FREQ) != ESP_OK) {
        Serial.println("Failed to set CPU frequency");
    }

    Serial.begin(115200);

    if (!SPIFFS.begin(false, "/data", 20)) {
        Serial.println("An error occurred while mounting SPIFFS");
        return;
    }

    pinMode(BUTTON_PIN, INPUT_PULLUP);

    pinMode(MOTION_SENSOR_PIN, INPUT_PULLDOWN);
    pinMode(LED_PIN, OUTPUT);

    pinMode(FLASHLIGHT_PIN, OUTPUT);

    startCamera();

    read_access_tokens();

    setup_smtp();

    start_data_mode();
}

void loop() {
    if(buttonPressed) {
        buttonPressed = false;
        if(DATA_MODE) {
            stop_data_mode();
            start_config_mode();
        }
    }

    if(sensorTriggered) {
        sensorTriggered = false;
        #ifndef DATA_HTTPS_ENABLED
        if(DATA_MODE) {
          if(clients_connected > 0 || streamActive) {
            Serial.print("Clients connected, cannot capture photo: ");
            Serial.println(clients_connected);
            return;
          }

          capture_photo();
          send_smtp();
        }
        #endif
    }
}

void read_access_tokens() {
  File file = SPIFFS.open("/passwd.json", "r");
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  if (error) {
    Serial.println("Failed to read file, using default credentials");
    return;
  }

  JsonArray tokens = doc["tokens"].as<JsonArray>();
  for (JsonVariant token : tokens) {
    access_tokens[token.as<std::string>()] = "";
  }

  file.close();
}


void start_data_mode() {
  std::string ssid, password;
  read_wifi_credentials(ssid, password);

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(hostname);
  WiFi.begin(ssid.c_str(), password.c_str());

  Serial.println("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    Serial.println("Connecting to WiFi...");
  }
  digitalWrite(LED_PIN, HIGH);

  Serial.println("Connected to WiFi");

  curr_server = startCameraServer((uint8_t *)cert_pem, (uint8_t *)key_pem, cert_pem_len, key_pem_len);
  if(curr_server == NULL) {
    Serial.println("Failed to start data server");
    digitalWrite(LED_PIN, LOW);
    return;
  }

  buttonPressed = false;
  DATA_MODE = true;

  attachInterrupt(BUTTON_PIN, handleButtonPress, FALLING);
  attachInterrupt(MOTION_SENSOR_PIN, handleSenorTrigger,  RISING);

  Serial.println("Data server started");

  Serial.print("Total heap: ");
  Serial.println(ESP.getHeapSize());
  Serial.print("Free heap: ");
  Serial.println(ESP.getFreeHeap());
  Serial.print("Max block size heap: ");
  Serial.println(ESP.getMaxAllocHeap());
}

void stop_data_mode() {
  if(DATA_MODE) {
    httpd_stop(curr_server);
    DATA_MODE = false;
  }

  digitalWrite(LED_PIN, LOW);

  Serial.println("Data server stopped");

  flashlightActive = false;
  digitalWrite(FLASHLIGHT_PIN, LOW);
  streamActive = false;

  WiFi.disconnect();
  Serial.println("WiFi disconnected");
}


void start_config_mode() {
  detachInterrupt(BUTTON_PIN);
  detachInterrupt(MOTION_SENSOR_PIN);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password);
  Serial.println("WiFi AP started");

  curr_server = startConfigServer((uint8_t *)cert_pem, (uint8_t *)key_pem, cert_pem_len, key_pem_len);
  if(curr_server == NULL) {
    Serial.println("Failed to start config server");
    digitalWrite(LED_PIN, LOW);
    return;
  }

  CONFIG_MODE = true;

  setup_blink_timer();
}


void stop_config_mode() {
  if(CONFIG_MODE) {
    CONFIG_MODE = false;
    httpd_stop(curr_server);
  }

  Serial.println("Config server stopped");

  WiFi.softAPdisconnect();
  Serial.println("WiFi AP stopped");

  destroy_blink_timer();
}

void read_wifi_credentials(std::string &ssid, std::string &password) {
  File file = SPIFFS.open("/wifi.json", "r");
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  if (error) {
    Serial.println("Failed to read file, using default credentials");
    return;
  }

  ssid = doc["ssid"].as<std::string>();
  password = doc["password"].as<std::string>();

  file.close();
}

void startCamera()
{
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;

    if(psramFound()) {
        config.frame_size = FRAMESIZE_VGA;
        config.jpeg_quality = 20;
        config.fb_count = 1;
        config.fb_location = CAMERA_FB_IN_DRAM;
        Serial.println("PSRAM found");
    } else {
        config.frame_size = FRAMESIZE_SVGA;
        config.jpeg_quality = 20;
        config.fb_count = 1;
        Serial.println("PSRAM not found");
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x", err);
        return;
    }

    sensor_t *s = esp_camera_sensor_get();

    s->set_vflip(s, 1);
}

void capture_photo() {
  camera_fb_t *fb = NULL;

  digitalWrite(FLASHLIGHT_PIN, HIGH);

  // skip first 10 frames
  for(uint8_t i = 0; i < 10; i++) {
    fb = esp_camera_fb_get();
    esp_camera_fb_return(fb);
    fb = NULL;
  }

  fb = esp_camera_fb_get();
  digitalWrite(FLASHLIGHT_PIN, LOW);

  if (!fb) {
    Serial.println("Camera capture failed");
    return;
  }

  File file = SPIFFS.open(CAPTURE_NAME, "w");
  if (!file) {
    Serial.println("Failed to open file for writing");
    esp_camera_fb_return(fb);
    return;
  }

  file.write(fb->buf, fb->len);
  file.close();

  esp_camera_fb_return(fb);
}

void setup_smtp() {
  message.subject = "ESP32 Camera ALERT";
  message.message = "Motion detected! Image attached.";

  fileDescriptor.filename = ATTACHMENT_NAME;
  fileDescriptor.url = CAPTURE_NAME;
  fileDescriptor.mime = MIME_IMAGE_JPG;
  fileDescriptor.storageType = EMailSender::EMAIL_STORAGE_TYPE_SPIFFS;
  fileDescriptor.encode64 = true;

  attach.number = 1;
  attach.fileDescriptor = &fileDescriptor;
}

void send_smtp() {
  EMailSender::Response resp = emailSend.send(SMTP_RECIPIENT, message, attach);

  Serial.println("Sending status: ");
  Serial.println(resp.desc);
}

void setup_blink_timer() {
  blink_timer = timerBegin(0, ESP_FREQ, true);
  timerAttachInterrupt(blink_timer, &blink, true);
  timerAlarmWrite(blink_timer, 1000000, true);
  timerAlarmEnable(blink_timer);
}

void destroy_blink_timer() {
  timerEnd(blink_timer);
}

