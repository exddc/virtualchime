#include "oc/http/tls_server.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <exception>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "oc/http/http.h"
#include "oc/http/json_http.h"
#include "oc/logging/logger.h"

namespace oc::http {
namespace {

bool WriteAllSsl(SSL *ssl, const std::string &data) {
    std::size_t offset = 0;
    while (offset < data.size()) {
        const int written =
            SSL_write(ssl, data.data() + offset, static_cast<int>(std::min<std::size_t>(data.size() - offset, 16384)));
        if (written <= 0) {
            return false;
        }
        offset += static_cast<std::size_t>(written);
    }
    return true;
}

std::string SubjectText(X509 *cert, int nid) {
    char text[256] = {};
    if (X509_NAME_get_text_by_NID(X509_get_subject_name(cert), nid, text, sizeof(text)) < 0) {
        return {};
    }
    return text;
}

// True for a certificate this server generated (self-signed, same organization) whose CN differs from common_name.
bool IsStaleGeneratedCertificate(const std::string &cert_path, const std::string &organization,
                                 const std::string &common_name) {
    FILE *file = std::fopen(cert_path.c_str(), "r");
    if (file == nullptr) {
        return false;
    }
    X509 *cert = PEM_read_X509(file, nullptr, nullptr, nullptr);
    std::fclose(file);
    if (cert == nullptr) {
        return false;
    }
    const bool stale = X509_check_issued(cert, cert) == X509_V_OK &&
                       SubjectText(cert, NID_organizationName) == organization &&
                       SubjectText(cert, NID_commonName) != common_name;
    X509_free(cert);
    return stale;
}

bool GenerateSelfSignedCertificate(const std::string &cert_path, const std::string &key_path,
                                   const std::string &organization, const std::string &common_name,
                                   std::string *error) {
    EVP_PKEY *pkey = EVP_PKEY_new();
    if (pkey == nullptr) {
        if (error != nullptr) {
            *error = "EVP_PKEY_new failed";
        }
        return false;
    }

    RSA *rsa = RSA_new();
    BIGNUM *exponent = BN_new();
    X509 *cert = nullptr;
    X509_NAME *name = nullptr;
    FILE *key_file = nullptr;
    FILE *cert_file = nullptr;

    bool success = false;

    if (rsa == nullptr || exponent == nullptr) {
        if (error != nullptr) {
            *error = "RSA allocation failed";
        }
        goto cleanup;
    }

    if (BN_set_word(exponent, RSA_F4) != 1) {
        if (error != nullptr) {
            *error = "BN_set_word failed";
        }
        goto cleanup;
    }

    if (RSA_generate_key_ex(rsa, 2048, exponent, nullptr) != 1) {
        if (error != nullptr) {
            *error = "RSA_generate_key_ex failed";
        }
        goto cleanup;
    }

    if (EVP_PKEY_assign_RSA(pkey, rsa) != 1) {
        if (error != nullptr) {
            *error = "EVP_PKEY_assign_RSA failed";
        }
        goto cleanup;
    }
    rsa = nullptr;

    cert = X509_new();
    if (cert == nullptr) {
        if (error != nullptr) {
            *error = "X509_new failed";
        }
        goto cleanup;
    }

    X509_set_version(cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(cert), static_cast<long>(time(nullptr)));
    X509_gmtime_adj(X509_get_notBefore(cert), 0);
    X509_gmtime_adj(X509_get_notAfter(cert), 60L * 60L * 24L * 365L * 10L);
    X509_set_pubkey(cert, pkey);

    name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC, reinterpret_cast<const unsigned char *>("US"), -1, -1, 0);
    if (!organization.empty()) {
        X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC,
                                   reinterpret_cast<const unsigned char *>(organization.c_str()), -1, -1, 0);
    }
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char *>(common_name.c_str()),
                               -1, -1, 0);
    X509_set_issuer_name(cert, name);

    if (X509_sign(cert, pkey, EVP_sha256()) == 0) {
        if (error != nullptr) {
            *error = "X509_sign failed";
        }
        goto cleanup;
    }

    key_file = fopen(key_path.c_str(), "w");
    if (key_file == nullptr) {
        if (error != nullptr) {
            *error = "failed to open key file";
        }
        goto cleanup;
    }
    if (PEM_write_PrivateKey(key_file, pkey, nullptr, nullptr, 0, nullptr, nullptr) != 1) {
        if (error != nullptr) {
            *error = "PEM_write_PrivateKey failed";
        }
        goto cleanup;
    }

    cert_file = fopen(cert_path.c_str(), "w");
    if (cert_file == nullptr) {
        if (error != nullptr) {
            *error = "failed to open cert file";
        }
        goto cleanup;
    }
    if (PEM_write_X509(cert_file, cert) != 1) {
        if (error != nullptr) {
            *error = "PEM_write_X509 failed";
        }
        goto cleanup;
    }

    chmod(key_path.c_str(), 0600);
    chmod(cert_path.c_str(), 0644);
    success = true;

cleanup:
    if (key_file != nullptr) {
        fclose(key_file);
    }
    if (cert_file != nullptr) {
        fclose(cert_file);
    }
    if (rsa != nullptr) {
        RSA_free(rsa);
    }
    if (exponent != nullptr) {
        BN_free(exponent);
    }
    if (cert != nullptr) {
        X509_free(cert);
    }
    EVP_PKEY_free(pkey);

    return success;
}

constexpr int kHandlerWorkers = 2;
constexpr int kHandshakeTimeoutSeconds = 10;
constexpr std::size_t kMaxPendingClients = 16;
constexpr std::size_t kMaxHashJobs = 4;

void SetSocketTimeout(int fd, int seconds) {
    struct timeval timeout {};
    timeout.tv_sec = seconds;
    timeout.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

} // namespace

TlsServer::TlsServer(oc::logging::Logger &logger, TlsServerConfig config, RequestHandler handler,
                     RequestPredicate slow_request)
    : logger_(logger), config_(std::move(config)), handler_(std::move(handler)), slow_request_(std::move(slow_request)),
      port_(config_.port) {}

TlsServer::~TlsServer() {
    Stop();
}

bool TlsServer::Start() {
    if (running_.load()) {
        return true;
    }

    std::string error;
    if (!EnsureTlsMaterial(&error)) {
        logger_.Error(config_.log_component, "TLS setup failed: " + error);
        return false;
    }

    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();
    std::signal(SIGPIPE, SIG_IGN);

    SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
    if (ctx == nullptr) {
        logger_.Error(config_.log_component, "SSL_CTX_new failed");
        return false;
    }

    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);

    if (SSL_CTX_use_certificate_file(ctx, config_.cert_path.c_str(), SSL_FILETYPE_PEM) != 1) {
        logger_.Error(config_.log_component, "failed to load TLS certificate from " + config_.cert_path);
        SSL_CTX_free(ctx);
        return false;
    }

    if (SSL_CTX_use_PrivateKey_file(ctx, config_.key_path.c_str(), SSL_FILETYPE_PEM) != 1) {
        logger_.Error(config_.log_component, "failed to load TLS private key from " + config_.key_path);
        SSL_CTX_free(ctx);
        return false;
    }

    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        logger_.Error(config_.log_component, "socket() failed");
        SSL_CTX_free(ctx);
        return false;
    }

    const int reuse = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(port_));
    if (inet_pton(AF_INET, config_.bind_address.c_str(), &address.sin_addr) != 1) {
        logger_.Error(config_.log_component, "invalid bind address: " + config_.bind_address);
        close(listen_fd_);
        listen_fd_ = -1;
        SSL_CTX_free(ctx);
        return false;
    }

    if (bind(listen_fd_, reinterpret_cast<struct sockaddr *>(&address), sizeof(address)) != 0) {
        logger_.Error(config_.log_component, "bind() failed on " + config_.bind_address + ":" + std::to_string(port_));
        close(listen_fd_);
        listen_fd_ = -1;
        SSL_CTX_free(ctx);
        return false;
    }

    if (port_ == 0) {
        struct sockaddr_in actual {};
        socklen_t actual_len = sizeof(actual);
        if (getsockname(listen_fd_, reinterpret_cast<struct sockaddr *>(&actual), &actual_len) != 0) {
            logger_.Error(config_.log_component, "getsockname() failed after bind");
            close(listen_fd_);
            listen_fd_ = -1;
            SSL_CTX_free(ctx);
            return false;
        }
        port_ = static_cast<int>(ntohs(actual.sin_port));
    }

    if (listen(listen_fd_, 16) != 0) {
        logger_.Error(config_.log_component, "listen() failed");
        close(listen_fd_);
        listen_fd_ = -1;
        SSL_CTX_free(ctx);
        return false;
    }

    ssl_ctx_ = ctx;
    running_.store(true);
    hash_thread_ = std::thread([this]() { HashLoop(); });
    workers_.clear();
    workers_.reserve(static_cast<std::size_t>(kHandlerWorkers));
    for (int i = 0; i < kHandlerWorkers; ++i) {
        workers_.emplace_back([this]() { WorkerLoop(); });
    }
    accept_thread_ = std::thread([this]() { AcceptLoop(); });

    logger_.Info(config_.log_component,
                 "https server listening on " + config_.bind_address + ":" + std::to_string(port_));
    return true;
}

int TlsServer::port() const {
    return port_;
}

void TlsServer::Stop() {
    if (!running_.exchange(false)) {
        return;
    }

    if (listen_fd_ >= 0) {
        shutdown(listen_fd_, SHUT_RDWR);
        close(listen_fd_);
        listen_fd_ = -1;
    }

    InterruptClients();
    connection_cv_.notify_all();
    hash_cv_.notify_all();

    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

    for (auto &worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();

    hash_cv_.notify_all();
    if (hash_thread_.joinable()) {
        hash_thread_.join();
    }

    {
        const std::lock_guard<std::mutex> lock(connection_mutex_);
        for (const int fd : pending_clients_) {
            close(fd);
        }
        pending_clients_.clear();
        active_client_fds_.clear();
    }

    {
        const std::lock_guard<std::mutex> lock(hash_mutex_);
        for (auto &job : hash_jobs_) {
            if (job.ssl != nullptr) {
                SSL_free(static_cast<SSL *>(job.ssl));
            }
            if (job.client_fd >= 0) {
                close(job.client_fd);
            }
        }
        hash_jobs_.clear();
    }

    if (ssl_ctx_ != nullptr) {
        SSL_CTX_free(static_cast<SSL_CTX *>(ssl_ctx_));
        ssl_ctx_ = nullptr;
    }

    EVP_cleanup();
}

void TlsServer::AcceptLoop() {
    while (running_.load()) {
        struct sockaddr_in client_addr {};
        socklen_t client_len = sizeof(client_addr);
        const int client_fd = accept(listen_fd_, reinterpret_cast<struct sockaddr *>(&client_addr), &client_len);

        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (!running_.load()) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        EnqueueClient(client_fd);
    }
}

void TlsServer::EnqueueClient(int client_fd) {
    std::lock_guard<std::mutex> lock(connection_mutex_);
    if (!running_.load() || pending_clients_.size() >= kMaxPendingClients) {
        close(client_fd);
        return;
    }
    pending_clients_.push_back(client_fd);
    active_client_fds_.insert(client_fd);
    connection_cv_.notify_one();
}

void TlsServer::WorkerLoop() {
    for (;;) {
        int client_fd = -1;
        {
            std::unique_lock<std::mutex> lock(connection_mutex_);
            connection_cv_.wait(lock, [this]() { return !running_.load() || !pending_clients_.empty(); });
            if (!running_.load()) {
                for (const int fd : pending_clients_) {
                    active_client_fds_.erase(fd);
                    close(fd);
                }
                pending_clients_.clear();
                return;
            }
            if (pending_clients_.empty()) {
                continue;
            }
            client_fd = pending_clients_.front();
            pending_clients_.pop_front();
        }
        HandleConnection(client_fd);
    }
}

void TlsServer::HashLoop() {
    for (;;) {
        HashJob job;
        {
            std::unique_lock<std::mutex> lock(hash_mutex_);
            hash_cv_.wait(lock, [this]() { return !running_.load() || !hash_jobs_.empty(); });
            if (!running_.load()) {
                std::deque<HashJob> leftover = std::move(hash_jobs_);
                hash_jobs_.clear();
                lock.unlock();
                for (auto &pending : leftover) {
                    ReleaseConnection(pending.ssl, pending.client_fd);
                }
                return;
            }
            if (hash_jobs_.empty()) {
                continue;
            }
            job = std::move(hash_jobs_.front());
            hash_jobs_.pop_front();
        }
        CompleteConnection(job.ssl, job.client_fd, InvokeHandler(job.request));
    }
}

bool TlsServer::EnqueueHashJob(int client_fd, void *ssl, HttpRequest request) {
    const std::lock_guard<std::mutex> lock(hash_mutex_);
    if (!running_.load() || hash_jobs_.size() >= kMaxHashJobs) {
        return false;
    }
    HashJob job;
    job.client_fd = client_fd;
    job.ssl = ssl;
    job.request = std::move(request);
    hash_jobs_.push_back(std::move(job));
    hash_cv_.notify_one();
    return true;
}

void TlsServer::CompleteConnection(void *ssl, int client_fd, const HttpResponse &response) {
    if (ssl != nullptr) {
        WriteAllSsl(static_cast<SSL *>(ssl), FormatHttpResponse(response));
    }
    ReleaseConnection(ssl, client_fd);
}

void TlsServer::ReleaseConnection(void *ssl, int client_fd) {
    if (ssl != nullptr) {
        SSL_shutdown(static_cast<SSL *>(ssl));
        SSL_free(static_cast<SSL *>(ssl));
    }
    UnregisterClientFd(client_fd);
    if (client_fd >= 0) {
        close(client_fd);
    }
}

void TlsServer::UnregisterClientFd(int client_fd) {
    const std::lock_guard<std::mutex> lock(connection_mutex_);
    active_client_fds_.erase(client_fd);
}

void TlsServer::InterruptClients() {
    const std::lock_guard<std::mutex> lock(connection_mutex_);
    for (const int fd : active_client_fds_) {
        shutdown(fd, SHUT_RDWR);
    }
}

void TlsServer::HandleConnection(int client_fd) {
    SetSocketTimeout(client_fd, kHandshakeTimeoutSeconds);

    SSL *ssl = SSL_new(static_cast<SSL_CTX *>(ssl_ctx_));
    if (ssl == nullptr) {
        ReleaseConnection(nullptr, client_fd);
        return;
    }

    SSL_set_fd(ssl, client_fd);
    if (SSL_accept(ssl) <= 0) {
        ReleaseConnection(ssl, client_fd);
        return;
    }

    HttpRequest request;
    std::string read_error;
    const HttpReadFn reader = [ssl](char *buffer, std::size_t length) {
        const int to_read = static_cast<int>(std::min(length, static_cast<std::size_t>(16384)));
        return SSL_read(ssl, buffer, to_read);
    };
    if (!ReadHttpRequest(reader, &request, &read_error)) {
        CompleteConnection(ssl, client_fd, JsonHttpError(400, "bad_request", read_error));
        return;
    }

    struct sockaddr_in peer {};
    socklen_t peer_len = sizeof(peer);
    char ip[INET_ADDRSTRLEN] = {};
    if (getpeername(client_fd, reinterpret_cast<struct sockaddr *>(&peer), &peer_len) == 0 &&
        inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip)) != nullptr) {
        request.peer_address = ip;
    }

    bool slow = false;
    try {
        slow = IsSlowRequest(request);
    } catch (const std::exception &ex) {
        logger_.Error(config_.log_component, std::string("request predicate threw: ") + ex.what());
        CompleteConnection(ssl, client_fd, JsonHttpError(500, "internal_error"));
        return;
    } catch (...) {
        logger_.Error(config_.log_component, "request predicate threw");
        CompleteConnection(ssl, client_fd, JsonHttpError(500, "internal_error"));
        return;
    }

    if (slow) {
        if (!EnqueueHashJob(client_fd, ssl, std::move(request))) {
            CompleteConnection(ssl, client_fd, JsonHttpError(429, "rate_limited"));
        }
        return;
    }

    CompleteConnection(ssl, client_fd, InvokeHandler(request));
}

bool TlsServer::EnsureTlsMaterial(std::string *error) const {
    const bool cert_exists = std::filesystem::exists(config_.cert_path);
    const bool key_exists = std::filesystem::exists(config_.key_path);
    if (cert_exists && key_exists) {
        if (!IsStaleGeneratedCertificate(config_.cert_path, config_.cert_organization, config_.cert_common_name)) {
            return true;
        }
        logger_.Info(config_.log_component, "regenerating TLS certificate for CN " + config_.cert_common_name);
    }

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(config_.cert_path).parent_path(), ec);
    if (ec) {
        if (error != nullptr) {
            *error = "failed to create cert directory: " + ec.message();
        }
        return false;
    }

    std::filesystem::create_directories(std::filesystem::path(config_.key_path).parent_path(), ec);
    if (ec) {
        if (error != nullptr) {
            *error = "failed to create key directory: " + ec.message();
        }
        return false;
    }

    return GenerateSelfSignedCertificate(config_.cert_path, config_.key_path, config_.cert_organization,
                                         config_.cert_common_name, error);
}

bool TlsServer::IsSlowRequest(const HttpRequest &request) const {
    return static_cast<bool>(slow_request_) && slow_request_(request);
}

HttpResponse TlsServer::InvokeHandler(const HttpRequest &request) const {
    try {
        return handler_(request);
    } catch (const std::exception &ex) {
        logger_.Error(config_.log_component, std::string("request handler threw: ") + ex.what());
        return JsonHttpError(500, "internal_error");
    } catch (...) {
        logger_.Error(config_.log_component, "request handler threw");
        return JsonHttpError(500, "internal_error");
    }
}

} // namespace oc::http
