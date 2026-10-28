// Outgoing connections: HTTP(S) requests, NTP, TCP connections and the mDNS search for buddy-link.
#include <cerrno>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "mdns.h"
#include "net/net.h"

namespace net {
namespace {
constexpr const char *kTag = "net";
constexpr int kMaxRedirects = 3;
} // namespace

core::fetch::Response http_get(const std::string &url, const core::fetch::Headers &headers)
{
    core::fetch::Response r;
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = 10000;
    cfg.user_agent = "Blinky";
    cfg.buffer_size = 2048;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (c == nullptr) {
        r.error = "out of memory";
        return r;
    }
    for (const auto &[k, v] : headers) {
        esp_http_client_set_header(c, k.c_str(), v.c_str());
    }
    for (int redirects = 0;; redirects++) {
        const esp_err_t err = esp_http_client_open(c, 0);
        if (err != ESP_OK) {
            r.error = std::string("request failed (") + esp_err_to_name(err) + ")";
            break;
        }
        esp_http_client_fetch_headers(c);
        const int status = esp_http_client_get_status_code(c);
        if ((status == 301 || status == 302 || status == 307 || status == 308) && redirects < kMaxRedirects) {
            esp_http_client_set_redirection(c);
            esp_http_client_close(c);
            continue;
        }
        r.status = status;
        char buf[512];
        for (;;) {
            const int n = esp_http_client_read(c, buf, sizeof buf);
            if (n < 0) {
                r = {.status = 0, .body = "", .error = "reading the response failed"};
                break;
            }
            if (n == 0) {
                break;
            }
            if (r.body.size() + static_cast<size_t>(n) > core::fetch::kMaxBody) {
                r = {.status = 0, .body = "", .error = "response larger than 16 KB"};
                break;
            }
            r.body.append(buf, static_cast<size_t>(n));
        }
        esp_http_client_close(c);
        break;
    }
    esp_http_client_cleanup(c);
    return r;
}

std::optional<int64_t> sync_time()
{
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    if (esp_netif_sntp_init(&cfg) != ESP_OK) {
        return std::nullopt;
    }
    const esp_err_t err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000));
    esp_netif_sntp_deinit();
    if (err != ESP_OK) {
        return std::nullopt;
    }
    return static_cast<int64_t>(std::time(nullptr));
}

int connect_tcp(const std::string &host, uint16_t port, uint32_t timeout_ms, std::string &err)
{
    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *res = nullptr;
    const std::string service = std::to_string(port);
    if (getaddrinfo(host.c_str(), service.c_str(), &hints, &res) != 0 || res == nullptr) {
        err = "cannot resolve " + host;
        return -1;
    }
    const int s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s < 0) {
        freeaddrinfo(res);
        err = "no socket";
        return -1;
    }
    const int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);
    int rc = connect(s, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if (rc != 0 && errno == EINPROGRESS) {
        fd_set w;
        FD_ZERO(&w);
        FD_SET(s, &w);
        timeval tv = {.tv_sec = static_cast<time_t>(timeout_ms / 1000),
                      .tv_usec = static_cast<suseconds_t>((timeout_ms % 1000) * 1000)};
        if (select(s + 1, nullptr, &w, nullptr, &tv) <= 0) {
            close(s);
            err = "no answer";
            return -1;
        }
        int so = 0;
        socklen_t len = sizeof so;
        getsockopt(s, SOL_SOCKET, SO_ERROR, &so, &len);
        rc = so == 0 ? 0 : -1;
        errno = so;
    }
    if (rc != 0) {
        err = std::strerror(errno);
        close(s);
        return -1;
    }
    fcntl(s, F_SETFL, flags);
    return s;
}

std::optional<Found> find_buddy_link(uint32_t timeout_ms)
{
    mdns_result_t *results = nullptr;
    if (mdns_query_ptr("_blinky", "_tcp", timeout_ms, 4, &results) != ESP_OK || results == nullptr) {
        return std::nullopt;
    }
    std::optional<Found> found;
    for (mdns_result_t *r = results; r != nullptr && !found; r = r->next) {
        bool proto_ok = true;
        for (size_t i = 0; i < r->txt_count; i++) {
            if (std::strcmp(r->txt[i].key, "proto") == 0 && r->txt[i].value != nullptr &&
                std::strcmp(r->txt[i].value, "1") != 0) {
                proto_ok = false;
            }
        }
        if (!proto_ok || r->port == 0) {
            continue;
        }
        char ip[16] = "";
        for (mdns_ip_addr_t *a = r->addr; a != nullptr; a = a->next) {
            if (a->addr.type == ESP_IPADDR_TYPE_V4) {
                esp_ip4addr_ntoa(&a->addr.u_addr.ip4, ip, sizeof ip);
                break;
            }
        }
        if (ip[0] == '\0' && r->hostname != nullptr) {
            esp_ip4_addr_t a = {};
            if (mdns_query_a(r->hostname, 2000, &a) == ESP_OK) {
                esp_ip4addr_ntoa(&a, ip, sizeof ip);
            }
        }
        if (ip[0] != '\0') {
            found = Found{.name = r->instance_name != nullptr ? r->instance_name : "buddy-link", .addr = ip, .port = r->port};
        }
    }
    mdns_query_results_free(results);
    if (found) {
        ESP_LOGI(kTag, "found %s at %s:%u via mDNS", found->name.c_str(), found->addr.c_str(), found->port);
    }
    return found;
}

} // namespace net
