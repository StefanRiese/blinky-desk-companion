// The web config page: GET / (the page), GET/POST /api/settings (schema, values, save),
// GET /api/status (links and sources). On the setup access point every other address
// redirects to the page, so phones open it as a captive portal.
#include <string>

#include <ArduinoJson.h>

#include "app_core/settings_json.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "net_internal.h"

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

namespace net {
namespace {

constexpr const char *kTag = "web";
constexpr size_t kMaxBody = 8192;

const char *wifi_name(core::Status::Wifi w)
{
    switch (w) {
    case core::Status::Wifi::Off:
        return "off";
    case core::Status::Wifi::Connecting:
        return "connecting";
    case core::Status::Wifi::Online:
        return "online";
    case core::Status::Wifi::NoInternet:
        return "no_internet";
    case core::Status::Wifi::SetupAp:
        return "setup_ap";
    }
    return "off";
}

esp_err_t send_json(httpd_req_t *req, const JsonDocument &doc, const char *status = "200 OK")
{
    std::string out;
    serializeJson(doc, out);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, out.data(), static_cast<ssize_t>(out.size()));
}

esp_err_t page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, index_html_start, index_html_end - index_html_start);
}

esp_err_t get_settings(httpd_req_t *req)
{
    JsonDocument doc;
    core::settings_schema(doc["schema"].to<JsonArray>());
    core::settings_values(doc["values"].to<JsonObject>(), settings(), "*");
    return send_json(req, doc);
}

esp_err_t post_settings(httpd_req_t *req)
{
    JsonDocument reply;
    if (req->content_len == 0 || req->content_len > kMaxBody) {
        reply["error"] = "send the settings as a JSON object (max 8 KB)";
        return send_json(req, reply, "400 Bad Request");
    }
    std::string body(req->content_len, '\0');
    size_t got = 0;
    while (got < body.size()) {
        const int n = httpd_req_recv(req, body.data() + got, body.size() - got);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (n <= 0) {
            return ESP_FAIL;
        }
        got += static_cast<size_t>(n);
    }
    JsonDocument set;
    if (deserializeJson(set, body) != DeserializationError::Ok || !set.is<JsonObject>()) {
        reply["error"] = "not a JSON object";
        return send_json(req, reply, "400 Bad Request");
    }
    std::vector<std::string> keys;
    if (auto err = core::apply_settings(settings(), set.as<JsonObjectConst>(), keys)) {
        reply["error"] = *err;
        return send_json(req, reply, "400 Bad Request");
    }
    ESP_LOGI(kTag, "%u settings saved", static_cast<unsigned>(keys.size()));
    if (hooks().changed) {
        hooks().changed(keys);
    }
    reply["ok"] = true;
    return send_json(req, reply);
}

esp_err_t get_status(httpd_req_t *req)
{
    const core::Status st = hooks().status ? hooks().status() : core::Status{};
    JsonDocument doc;
    doc["wifi"]["state"] = wifi_name(st.wifi);
    doc["wifi"]["text"] = st.wifi_text;
    for (auto [name, link] : {std::pair{"usb", &st.usb}, std::pair{"tcp", &st.tcp}}) {
        doc[name]["up"] = link->up;
        doc[name]["text"] = link->text;
    }
    if (st.battery >= 0) {
        doc["battery"]["percent"] = st.battery;
        doc["battery"]["charging"] = st.charging;
    }
    JsonArray sources = doc["sources"].to<JsonArray>();
    for (const auto &s : st.sources) {
        JsonObject o = sources.add<JsonObject>();
        o["sid"] = s.sid;
        o["src"] = s.src;
        o["via"] = s.via == "host" ? s.link : s.via;
        o["state"] = s.state;
        o["msg"] = s.msg;
    }
    return send_json(req, doc);
}

// not_found sends everything to the page while the setup access point is on.
esp_err_t not_found(httpd_req_t *req, httpd_err_code_t)
{
    if (!ap_on()) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
    }
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    return httpd_resp_send(req, "", 0);
}

} // namespace

void start_web()
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_open_sockets = 4;
    cfg.lru_purge_enable = true;
    cfg.stack_size = 6144;
    httpd_handle_t server = nullptr;
    if (httpd_start(&server, &cfg) != ESP_OK) {
        ESP_LOGE(kTag, "web server not started");
        return;
    }
    const struct {
        const char *uri;
        httpd_method_t method;
        esp_err_t (*handler)(httpd_req_t *);
    } routes[] = {
        {"/", HTTP_GET, page},
        {"/api/settings", HTTP_GET, get_settings},
        {"/api/settings", HTTP_POST, post_settings},
        {"/api/status", HTTP_GET, get_status},
    };
    for (const auto &r : routes) {
        httpd_uri_t u = {}; // more fields with websocket support
        u.uri = r.uri;
        u.method = r.method;
        u.handler = r.handler;
        httpd_register_uri_handler(server, &u);
    }
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, not_found);
}

} // namespace net
