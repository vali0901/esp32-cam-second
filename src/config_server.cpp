#include <ArduinoJson.h>

#include "config_server.h"
#include "json.hpp"

#define TOKEN_LENGTH 12


std::unordered_map<std::string, std::string> access_tokens = {};

void save_wifi_credentials(std::string ssid, std::string password);
void generate_admin_token(std::string &new_token);
void save_access_tokens();

// HTTP handler to serve the index page
esp_err_t config_index_handler(httpd_req_t *req)
{
    auto file = SPIFFS.open("/config/index.html", "r");
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

// HTTP handler to serve the index page
esp_err_t index_handler_style(httpd_req_t *req)
{
    auto file = SPIFFS.open("/config/style.css", "r");
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

// HTTP handler to serve the index page
esp_err_t index_handler_script(httpd_req_t *req)
{
    auto file = SPIFFS.open("/config/script.js", "r");
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

esp_err_t submit_data(httpd_req_t *req) {
    char buf[256];
    httpd_req_recv(req, buf, req->content_len);
    buf[req->content_len] = '\0';

    // Parse the data as json
    auto data = nlohmann::json::parse(buf);
    std::string ssid = data["ssid"];
    std::string password = data["password"];
    std::string token = data["token"];

    if(access_tokens.find(token) == access_tokens.end()) {
        httpd_resp_send(req, "Invalid token", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    // Save the wifi credentials
    save_wifi_credentials(ssid, password);

    // Send a success message
    httpd_resp_send(req, "Credentials saved", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t quit(httpd_req_t *req) {
    // Send a success message
    httpd_resp_send(req, "Quitting", HTTPD_RESP_USE_STRLEN);
    esp_restart();
    return ESP_OK;
}

esp_err_t token_mgmt_index(httpd_req_t *req) {
    auto file = SPIFFS.open("/config/token_mgmt/index.html", "r");
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

esp_err_t token_mgmt_script(httpd_req_t *req) {
    auto file = SPIFFS.open("/config/token_mgmt/script.js", "r");
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


esp_err_t token_mgmt_style(httpd_req_t *req) {
    auto file = SPIFFS.open("/config/token_mgmt/style.css", "r");
    if(!file) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    char *js;
    size_t len = file.size();
    js = (char *)malloc(len);
    file.readBytes(js, len);

    httpd_resp_set_type(req, "text/css");
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_send(req, js, len);

    free(js);
    return ESP_OK;
}

esp_err_t token_mgmt_submit(httpd_req_t *req) {
    char buf[256];
    httpd_req_recv(req, buf, req->content_len);
    buf[req->content_len] = '\0';

    // Parse the data as json
    auto data = nlohmann::json::parse(buf);
    std::string token = data["token"];

    if(access_tokens.find(token) == access_tokens.end()) {
        httpd_resp_send(req, "Invalid admin token", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    std::string action = data["action"];
    if(action == "add") {
        std::string new_token;
        generate_admin_token(new_token);
        access_tokens[new_token] = "";
        save_access_tokens();
        httpd_resp_send(req, new_token.c_str(), HTTPD_RESP_USE_STRLEN);
    } else if (action == "remove") {
        if(access_tokens.size() == 1) {
            httpd_resp_send(req, "Cannot remove last token", HTTPD_RESP_USE_STRLEN);
            return ESP_FAIL;
        }

        access_tokens.erase(token);
        save_access_tokens();
        httpd_resp_send(req, "Token removed", HTTPD_RESP_USE_STRLEN);
    } else {
        httpd_resp_send(req, "Invalid action", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    return ESP_OK;
}



// Function to start the web server
httpd_handle_t startConfigServer(uint8_t cert_pem[], uint8_t key_pem[], size_t cert_pem_len, size_t key_pem_len)
{

#ifdef CONFIG_HTTPS_ENABLED
    httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();

    config.cacert_pem = cert_pem;
    config.cacert_len = cert_pem_len;
    config.prvtkey_pem = key_pem;
    config.prvtkey_len = key_pem_len;

    config.httpd.max_uri_handlers = 10;
#else
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    config.max_uri_handlers = 10;
#endif // CONFIG_HTTPS_ENABLED

    httpd_handle_t server = NULL;

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = config_index_handler,
        .user_ctx = NULL
    };

    httpd_uri_t index_uri_style = {
        .uri = "/style.css",
        .method = HTTP_GET,
        .handler = index_handler_style,
        .user_ctx = NULL
    };

    httpd_uri_t index_uri_script = {
        .uri = "/script.js",
        .method = HTTP_GET,
        .handler = index_handler_script,
        .user_ctx = NULL
    };

    httpd_uri_t submit_uri = {
        .uri = "/submit",
        .method = HTTP_POST,
        .handler = submit_data,
        .user_ctx = NULL
    };

    httpd_uri_t quit_uri = {
        .uri = "/quit",
        .method = HTTP_GET,
        .handler = quit,
        .user_ctx = NULL
    };

    httpd_uri_t token_mgmt_index_uri = {
        .uri = "/token_mgmt/",
        .method = HTTP_GET,
        .handler = token_mgmt_index,
        .user_ctx = NULL
    };

    httpd_uri_t token_mgmt_script_uri = {
        .uri = "/token_mgmt/script.js",
        .method = HTTP_GET,
        .handler = token_mgmt_script,
        .user_ctx = NULL
    };

    httpd_uri_t token_mgmt_style_uri = {
        .uri = "/token_mgmt/style.css",
        .method = HTTP_GET,
        .handler = token_mgmt_style,
        .user_ctx = NULL
    };

    httpd_uri_t token_mgmt_submit_uri = {
        .uri = "/token_mgmt/submit",
        .method = HTTP_POST,
        .handler = token_mgmt_submit,
        .user_ctx = NULL
    };

#ifdef CONFIG_HTTPS_ENABLED
    if (httpd_ssl_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &index_uri);
        httpd_register_uri_handler(server, &index_uri_style);
        httpd_register_uri_handler(server, &index_uri_script);
        httpd_register_uri_handler(server, &submit_uri);
        httpd_register_uri_handler(server, &quit_uri);
        httpd_register_uri_handler(server, &token_mgmt_index_uri);
        httpd_register_uri_handler(server, &token_mgmt_script_uri);
        httpd_register_uri_handler(server, &token_mgmt_style_uri);
        httpd_register_uri_handler(server, &token_mgmt_submit_uri);

    } else {
        // Serial.println("Failed to start HTTPS server");
        for(int i = 0; i < 10; i++) {
            digitalWrite(4, HIGH);
            delay(100);
            digitalWrite(4, LOW);
            delay(250);
        }
    }
#else
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &index_uri);
        httpd_register_uri_handler(server, &index_uri_style);
        httpd_register_uri_handler(server, &index_uri_script);
        httpd_register_uri_handler(server, &submit_uri);
        httpd_register_uri_handler(server, &quit_uri);
        httpd_register_uri_handler(server, &token_mgmt_index_uri);
        httpd_register_uri_handler(server, &token_mgmt_script_uri);
        httpd_register_uri_handler(server, &token_mgmt_style_uri);
        httpd_register_uri_handler(server, &token_mgmt_submit_uri);
    } else {
        // Serial.println("Failed to start HTTP server");
        for(int i = 0; i < 10; i++) {
            digitalWrite(4, HIGH);
            delay(100);
            digitalWrite(4, LOW);
            delay(250);
        }

        return NULL;
    }
#endif
    return server;
}

void save_wifi_credentials(std::string ssid, std::string password) {
  File file = SPIFFS.open("/wifi.json", "w");
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }

  JsonDocument doc;
  doc["ssid"] = ssid;
  doc["password"] = password;

  if (serializeJson(doc, file) == 0) {
    Serial.println("Failed to write file");
  }

  file.close();
}

void generate_admin_token(std::string &new_token) {
  // Generate a random token
  char token[TOKEN_LENGTH + 1];
  for (int i = 0; i < TOKEN_LENGTH; i++) {
    token[i] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"[random(62)];
  }
  token[TOKEN_LENGTH] = '\0';
  new_token = token;
}


void save_access_tokens() {
  File file = SPIFFS.open("/passwd.json", "w");
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }

  JsonDocument doc;
  JsonArray tokens = doc["tokens"].to<JsonArray>();
  for (auto const& token : access_tokens) {
    tokens.add(token.first);
  }

  if (serializeJson(doc, file) == 0) {
    Serial.println("Failed to write file");
  }

  file.close();
}
