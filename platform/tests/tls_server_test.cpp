#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include "doctest.h"
#include "oc/http/http.h"
#include "oc/http/json_http.h"
#include "oc/http/product_routes.h"
#include "oc/http/tls_server.h"
#include "oc/json/json.h"
#include "test_support.h"

namespace {

class PingRoutes final : public oc::http::ProductRoutes {
  public:
    void Register(oc::http::HttpRouter &router) override {
        router.SetMethodNotAllowed(oc::http::JsonHttpError(405, "method_not_allowed"));
        router.SetNotFound(oc::http::JsonHttpError(404, "not_found"));
        router.Add("GET", "/api/v1/ping", [](const oc::http::HttpRequest &) {
            return oc::http::JsonHttpBody(200, oc::json::JsonValue::Object({
                                                   {"ok", oc::json::JsonValue::Bool(true)},
                                               }));
        });
    }
};

std::string TlsExchange(const std::string &bind_address, int port, const std::string &request,
                        std::chrono::seconds timeout = std::chrono::seconds(5)) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(fd >= 0);

    struct timeval timeout_tv {};
    timeout_tv.tv_sec = static_cast<time_t>(timeout.count());
    timeout_tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout_tv, sizeof(timeout_tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout_tv, sizeof(timeout_tv));

    struct sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(port));
    REQUIRE(inet_pton(AF_INET, bind_address.c_str(), &address.sin_addr) == 1);
    REQUIRE(connect(fd, reinterpret_cast<struct sockaddr *>(&address), sizeof(address)) == 0);

    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    REQUIRE(ctx != nullptr);
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);

    SSL *ssl = SSL_new(ctx);
    REQUIRE(ssl != nullptr);
    SSL_set_fd(ssl, fd);
    REQUIRE(SSL_connect(ssl) == 1);

    std::size_t offset = 0;
    while (offset < request.size()) {
        const int written =
            SSL_write(ssl, request.data() + offset,
                      static_cast<int>(std::min(request.size() - offset, static_cast<std::size_t>(2048))));
        REQUIRE(written > 0);
        offset += static_cast<std::size_t>(written);
    }

    std::string response;
    std::array<char, 2048> buffer{};
    for (;;) {
        const int bytes = SSL_read(ssl, buffer.data(), static_cast<int>(buffer.size()));
        if (bytes <= 0) {
            break;
        }
        response.append(buffer.data(), static_cast<std::size_t>(bytes));
    }

    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    close(fd);
    return response;
}

std::string CertificateSubject(const std::string &cert_path, int nid) {
    FILE *file = std::fopen(cert_path.c_str(), "r");
    REQUIRE(file != nullptr);
    X509 *cert = PEM_read_X509(file, nullptr, nullptr, nullptr);
    std::fclose(file);
    REQUIRE(cert != nullptr);
    char text[256] = {};
    const int n = X509_NAME_get_text_by_NID(X509_get_subject_name(cert), nid, text, sizeof(text));
    X509_free(cert);
    if (n < 0) {
        return {};
    }
    return std::string(text);
}

std::string CertificateOrganization(const std::string &cert_path) {
    return CertificateSubject(cert_path, NID_organizationName);
}

std::string CertificateCommonName(const std::string &cert_path) {
    return CertificateSubject(cert_path, NID_commonName);
}

std::string ReadFile(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

oc::http::TlsServerConfig CertTestConfig(const std::filesystem::path &dir, const std::string &organization,
                                         const std::string &common_name) {
    oc::http::TlsServerConfig config;
    config.bind_address = "127.0.0.1";
    config.port = 0;
    config.cert_path = (dir / "cert.pem").string();
    config.key_path = (dir / "key.pem").string();
    config.cert_organization = organization;
    config.cert_common_name = common_name;
    return config;
}

void StartAndStop(oc::logging::Logger &logger, const oc::http::TlsServerConfig &config) {
    oc::http::TlsServer server(logger, config,
                               [](const oc::http::HttpRequest &) { return oc::http::JsonHttpError(404, "not_found"); });
    REQUIRE(server.Start());
    server.Stop();
}

} // namespace

TEST_SUITE("tls_server") {
    TEST_CASE("serves a product-registered ping route and stops") {
        const ScopedTempDir tmp;
        NullLogger logger;
        oc::http::HttpRouter router;
        PingRoutes ping;
        ping.Register(router);

        oc::http::TlsServerConfig config;
        config.bind_address = "127.0.0.1";
        config.port = 0;
        config.cert_path = (tmp.path() / "cert.pem").string();
        config.key_path = (tmp.path() / "key.pem").string();
        config.cert_common_name = "localhost";

        oc::http::TlsServer server(
            logger, config, [&router](const oc::http::HttpRequest &request) { return router.Dispatch(request); });

        REQUIRE(server.Start());
        CHECK(server.port() > 0);
        CHECK(CertificateOrganization(config.cert_path).empty());

        const std::string ok =
            TlsExchange("127.0.0.1", server.port(), "GET /api/v1/ping HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
        CHECK(ok.find("HTTP/1.1 200 OK\r\n") == 0);
        CHECK(ok.find("Connection: close\r\n") != std::string::npos);
        const auto parsed = oc::json::ParseJson(ok.substr(ok.find("\r\n\r\n") + 4));
        REQUIRE(parsed.success);
        const auto ok_field = oc::json::GetObjectField(parsed.value, "ok");
        REQUIRE(ok_field.has_value());
        bool ping_ok = false;
        REQUIRE(ok_field->AsBool(&ping_ok));
        CHECK(ping_ok);

        const std::string bad = TlsExchange("127.0.0.1", server.port(), "GET / HTTP/2.0\r\n\r\n");
        CHECK(bad.find("HTTP/1.1 400 Bad Request\r\n") == 0);

        server.Stop();
        server.Stop();
    }

    TEST_CASE("Stop returns promptly when a client stalls in the TLS handshake") {
        const ScopedTempDir tmp;
        NullLogger logger;
        oc::http::HttpRouter router;
        PingRoutes ping;
        ping.Register(router);

        oc::http::TlsServerConfig config;
        config.bind_address = "127.0.0.1";
        config.port = 0;
        config.cert_path = (tmp.path() / "cert.pem").string();
        config.key_path = (tmp.path() / "key.pem").string();

        oc::http::TlsServer server(
            logger, config, [&router](const oc::http::HttpRequest &request) { return router.Dispatch(request); });
        REQUIRE(server.Start());

        int fd = socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(fd >= 0);
        struct sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<uint16_t>(server.port()));
        REQUIRE(inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) == 1);
        REQUIRE(connect(fd, reinterpret_cast<struct sockaddr *>(&address), sizeof(address)) == 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        const auto started = std::chrono::steady_clock::now();
        server.Stop();
        const auto elapsed = std::chrono::steady_clock::now() - started;
        CHECK(elapsed < std::chrono::seconds(1));
        close(fd);
    }

    TEST_CASE("returns HTTP 500 when the request handler throws") {
        const ScopedTempDir tmp;
        NullLogger logger;
        oc::http::TlsServerConfig config;
        config.bind_address = "127.0.0.1";
        config.port = 0;
        config.cert_path = (tmp.path() / "cert.pem").string();
        config.key_path = (tmp.path() / "key.pem").string();

        oc::http::TlsServer server(logger, config, [](const oc::http::HttpRequest &) -> oc::http::HttpResponse {
            throw std::runtime_error("handler boom");
        });
        REQUIRE(server.Start());

        const std::string response =
            TlsExchange("127.0.0.1", server.port(), "GET /api/v1/ping HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
        CHECK(response.find("HTTP/1.1 500 Internal Server Error\r\n") == 0);
        CHECK(response.find("Connection: close\r\n") != std::string::npos);
        CHECK(response.find("\"error\":\"internal_error\"") != std::string::npos);
        server.Stop();
    }

    TEST_CASE("returns HTTP 500 when the request handler throws an unknown exception") {
        const ScopedTempDir tmp;
        NullLogger logger;
        oc::http::TlsServerConfig config;
        config.bind_address = "127.0.0.1";
        config.port = 0;
        config.cert_path = (tmp.path() / "cert.pem").string();
        config.key_path = (tmp.path() / "key.pem").string();

        oc::http::TlsServer server(logger, config,
                                   [](const oc::http::HttpRequest &) -> oc::http::HttpResponse { throw 42; });
        REQUIRE(server.Start());

        const std::string response =
            TlsExchange("127.0.0.1", server.port(), "GET /api/v1/ping HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
        CHECK(response.find("HTTP/1.1 500 Internal Server Error\r\n") == 0);
        server.Stop();
    }

    TEST_CASE("returns HTTP 500 when the slow-request predicate throws") {
        const ScopedTempDir tmp;
        NullLogger logger;
        oc::http::HttpRouter router;
        PingRoutes ping;
        ping.Register(router);

        oc::http::TlsServerConfig config;
        config.bind_address = "127.0.0.1";
        config.port = 0;
        config.cert_path = (tmp.path() / "cert.pem").string();
        config.key_path = (tmp.path() / "key.pem").string();

        oc::http::TlsServer server(
            logger, config, [&router](const oc::http::HttpRequest &request) { return router.Dispatch(request); },
            [](const oc::http::HttpRequest &) -> bool { throw std::runtime_error("predicate boom"); });
        REQUIRE(server.Start());

        const std::string response =
            TlsExchange("127.0.0.1", server.port(), "GET /api/v1/ping HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
        CHECK(response.find("HTTP/1.1 500 Internal Server Error\r\n") == 0);
        CHECK(response.find("HTTP/1.1 200 OK\r\n") == std::string::npos);
        server.Stop();
    }

    TEST_CASE("self-signed certificate uses the configured organization") {
        const ScopedTempDir tmp;
        NullLogger logger;
        oc::http::TlsServerConfig config;
        config.bind_address = "127.0.0.1";
        config.port = 0;
        config.cert_path = (tmp.path() / "cert.pem").string();
        config.key_path = (tmp.path() / "key.pem").string();
        config.cert_organization = "Acme Bell Co";
        config.cert_common_name = "acme.local";

        oc::http::TlsServer server(
            logger, config, [](const oc::http::HttpRequest &) { return oc::http::JsonHttpError(404, "not_found"); });
        REQUIRE(server.Start());
        CHECK(CertificateOrganization(config.cert_path) == "Acme Bell Co");
        server.Stop();
    }

    TEST_CASE("regenerates a generated certificate when the common name changes") {
        const ScopedTempDir tmp;
        NullLogger logger;
        StartAndStop(logger, CertTestConfig(tmp.path(), "OpenChime", "chime.local"));
        const auto config = CertTestConfig(tmp.path(), "OpenChime", "ring.local");
        StartAndStop(logger, config);
        CHECK(CertificateCommonName(config.cert_path) == "ring.local");
    }

    TEST_CASE("keeps a certificate with the configured common name") {
        const ScopedTempDir tmp;
        NullLogger logger;
        const auto config = CertTestConfig(tmp.path(), "OpenChime", "chime.local");
        StartAndStop(logger, config);
        const std::string first = ReadFile(config.cert_path);
        StartAndStop(logger, config);
        CHECK(ReadFile(config.cert_path) == first);
    }

    TEST_CASE("keeps a certificate from another organization") {
        const ScopedTempDir tmp;
        NullLogger logger;
        StartAndStop(logger, CertTestConfig(tmp.path(), "Someone Else", "custom.example"));
        const auto config = CertTestConfig(tmp.path(), "OpenChime", "chime.local");
        StartAndStop(logger, config);
        CHECK(CertificateCommonName(config.cert_path) == "custom.example");
    }
}
