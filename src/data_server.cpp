#include "data_server.h"
#include "json.hpp"
#include "config_server.h"


volatile bool streamActive = false;
volatile bool flashlightActive = false;
volatile bool buttonPressed = false;
volatile int clients_connected = 0;

// HTTP handler to stream video
esp_err_t stream_handler(httpd_req_t *req)
{
    if (!streamActive) {
        httpd_resp_set_status(req, "200 OK");
        httpd_resp_send(req, "Stream inactive", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;

    httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=frame");

    while (streamActive) {
        fb = esp_camera_fb_get();
        if (!fb) {
            Serial.println("Camera capture failed");
            res = ESP_FAIL;
            break;
        }

        char part_buf[64];
        size_t hlen = snprintf((char *)part_buf, 64, "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n", fb->len);
        res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        if (res != ESP_OK) {
            Serial.println("Failed to send frame header");
            esp_camera_fb_return(fb);
            break;
        }

        res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
        if (res != ESP_OK) {
            Serial.println("Failed to send frame data");
            esp_camera_fb_return(fb);
            break;
        }

        res = httpd_resp_send_chunk(req, "\r\n", 2);
        if (res != ESP_OK) {
            Serial.println("Failed to send frame terminator");
            esp_camera_fb_return(fb);
            break;
        }

        esp_camera_fb_return(fb);
    }

    return res;
}

// HTTP handler for HTML page
esp_err_t index_handler(httpd_req_t *req) {
    auto file = SPIFFS.open("/video/index.html", "r");
    if(!file) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    char *html;
    size_t len = file.size();
    html = (char *)malloc(len);
    file.readBytes(html, len);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_send(req, html, len);

    free(html);

    return ESP_OK;
}

esp_err_t index_handler_css(httpd_req_t *req) {
    auto file = SPIFFS.open("/video/style.css", "r");
    if(!file) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    char *css;
    size_t len = file.size();
    css = (char *)malloc(len);
    file.readBytes(css, len);

    httpd_resp_set_type(req, "text/css");
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_send(req, css, len);

    free(css);
    return ESP_OK;
}

esp_err_t index_handler_js(httpd_req_t *req) {
    auto file = SPIFFS.open("/video/script.js", "r");
    if(!file) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    char *js;
    size_t len = file.size();
    js = (char *)malloc(len);
    file.readBytes(js, len);

    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_send(req, js, len);

    free(js);
    return ESP_OK;
}

// HTTP handler to toggle flashlight
esp_err_t toggle_flashlight_handler(httpd_req_t *req)
{
    flashlightActive = !flashlightActive;
    digitalWrite(FLASHLIGHT_PIN, flashlightActive ? HIGH : LOW);
    httpd_resp_send(req, "Flashlight toggled", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// HTTP handler to toggle stream
esp_err_t toggle_stream_handler(httpd_req_t *req)
{
    streamActive = !streamActive;
    httpd_resp_send(req, "Stream toggled", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t stats_handler(httpd_req_t *req)
{
    char response[100];
    snprintf(response, 100, "Free heap: %d bytes\\nClients_connected: %d \n", esp_get_free_heap_size(), clients_connected);
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t validate_token_handler(httpd_req_t *req) {
    char buf[256];
    httpd_req_recv(req, buf, req->content_len);
    buf[req->content_len] = '\0';

    auto data = nlohmann::json::parse(buf);
    std::string token = data["token"];
    nlohmann::json response;

    if(access_tokens.find(token) == access_tokens.end()) {
        response["valid"] = false;
    } else {
        clients_connected++;
        response["valid"] = true;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_send(req, response.dump().c_str(), HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

esp_err_t disconnect_handler(httpd_req_t *req) {
    if(clients_connected <= 0) {
        return ESP_OK;
    }

    clients_connected--;
    if(clients_connected == 0) {
        streamActive = false;
        digitalWrite(FLASHLIGHT_PIN, LOW);
        flashlightActive = false;
    }

    return ESP_OK;
}

// Function to start the web server
httpd_handle_t startCameraServer(uint8_t cert_pem[], uint8_t key_pem[], size_t cert_pem_len, size_t key_pem_len)
{
    clients_connected = 0;

#ifdef DATA_HTTPS_ENABLED
    httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();

    config.cacert_pem = cert_pem;
    config.cacert_len = cert_pem_len;
    config.prvtkey_pem = key_pem;
    config.prvtkey_len = key_pem_len;

    config.httpd.max_uri_handlers = 10;
#else
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    config.max_uri_handlers = 10;
#endif // DATA_HTTPS_ENABLED

    httpd_handle_t server = NULL;

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL
    };

    httpd_uri_t index_uri_css = {
        .uri = "/style.css",
        .method = HTTP_GET,
        .handler = index_handler_css,
        .user_ctx = NULL
    };

    httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
        .user_ctx = (void *)&buttonPressed
    };

    httpd_uri_t toggle_flashlight_uri = {
        .uri = "/toggle_flashlight",
        .method = HTTP_GET,
        .handler = toggle_flashlight_handler,
        .user_ctx = NULL
    };

    httpd_uri_t toggle_stream_uri = {
        .uri = "/toggle_stream",
        .method = HTTP_GET,
        .handler = toggle_stream_handler,
        .user_ctx = NULL
    };

    httpd_uri_t stats_uri = {
        .uri = "/stats",
        .method = HTTP_GET,
        .handler = stats_handler,
        .user_ctx = NULL
    };

    httpd_uri_t validate_token_uri = {
        .uri = "/validate_token",
        .method = HTTP_POST,
        .handler = validate_token_handler,
        .user_ctx = NULL
    };

    httpd_uri_t index_uri_js = {
        .uri = "/script.js",
        .method = HTTP_GET,
        .handler = index_handler_js,
        .user_ctx = NULL
    };

    httpd_uri_t disconnect_uri = {
        .uri = "/disconnect",
        .method = HTTP_POST,
        .handler = disconnect_handler,
        .user_ctx = NULL
    };


#ifdef DATA_HTTPS_ENABLED
    if (httpd_ssl_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &index_uri);
        httpd_register_uri_handler(server, &stream_uri);
        httpd_register_uri_handler(server, &toggle_flashlight_uri);
        httpd_register_uri_handler(server, &toggle_stream_uri);
        httpd_register_uri_handler(server, &index_uri_css);
        httpd_register_uri_handler(server, &stats_uri);
        httpd_register_uri_handler(server, &validate_token_uri);
        httpd_register_uri_handler(server, &index_uri_js);
        httpd_register_uri_handler(server, &disconnect_uri);
    } else {
        // Serial.println("Failed to start HTTPS server");
        for(int i = 0; i < 10; i++) {
            digitalWrite(FLASHLIGHT_PIN, HIGH);
            delay(100);
            digitalWrite(FLASHLIGHT_PIN, LOW);
            delay(250);
        }
    }
#else
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &index_uri);
        httpd_register_uri_handler(server, &stream_uri);
        httpd_register_uri_handler(server, &toggle_flashlight_uri);
        httpd_register_uri_handler(server, &toggle_stream_uri);
        httpd_register_uri_handler(server, &index_uri_css);
        httpd_register_uri_handler(server, &stats_uri);
        httpd_register_uri_handler(server, &validate_token_uri);
        httpd_register_uri_handler(server, &index_uri_js);
        httpd_register_uri_handler(server, &disconnect_uri);

    } else {
        for(int i = 0; i < 10; i++) {
            digitalWrite(FLASHLIGHT_PIN, HIGH);
            delay(100);
            digitalWrite(FLASHLIGHT_PIN, LOW);
            delay(250);
        }

        return NULL;
    }
#endif
    return server;
}
