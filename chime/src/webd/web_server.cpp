#include "chime/webd_web_server.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <ctime>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include "chime/webd_apply_manager.h"
#include "chime/webd_config_store.h"
#include "chime/webd_json.h"
#include "chime/webd_ui_assets.h"
#include "chime/webd_wifi_scan.h"
#include "vc/config/kv_config.h"
#include "vc/logging/logger.h"

namespace chime::webd {
namespace {

constexpr std::size_t kMaxRequestBytes = 65536;
constexpr std::size_t kMaxBodyBytes = 32768;

bool StartsWith(const std::string& value, const std::string& prefix) {
  return value.size() >= prefix.size() &&
         value.compare(0, prefix.size(), prefix) == 0;
}

std::string ToLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

bool IsSafeRelativePath(const std::filesystem::path& path) {
  for (const auto& part : path) {
    if (part == "..") {
      return false;
    }
  }
  return true;
}

std::string ContentTypeForPath(const std::filesystem::path& path) {
  const std::string extension = ToLower(path.extension().string());
  if (extension == ".html") {
    return "text/html; charset=utf-8";
  }
  if (extension == ".css") {
    return "text/css; charset=utf-8";
  }
  if (extension == ".js") {
    return "application/javascript; charset=utf-8";
  }
  if (extension == ".json") {
    return "application/json; charset=utf-8";
  }
  if (extension == ".svg") {
    return "image/svg+xml";
  }
  if (extension == ".ico") {
    return "image/x-icon";
  }
  if (extension == ".png") {
    return "image/png";
  }
  if (extension == ".jpg" || extension == ".jpeg") {
    return "image/jpeg";
  }
  if (extension == ".webp") {
    return "image/webp";
  }
  if (extension == ".woff2") {
    return "font/woff2";
  }
  if (extension == ".woff") {
    return "font/woff";
  }
  return "application/octet-stream";
}

std::string CacheControlForPath(const std::string& request_path,
                                const std::filesystem::path& path) {
  if (ToLower(path.extension().string()) == ".html") {
    return "no-cache";
  }
  if (StartsWith(request_path, "/assets/")) {
    return "public, max-age=31536000, immutable";
  }
  return "public, max-age=3600";
}

bool ReadFile(const std::filesystem::path& path, std::string* body) {
  if (body == nullptr) {
    return false;
  }
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return false;
  }
  std::ostringstream stream;
  stream << file.rdbuf();
  *body = stream.str();
  return true;
}

std::vector<std::string> ReadObservedTopicsFromFile(
    const std::string& path, std::string* error) {
  std::vector<std::string> topics;
  std::ifstream file(path);
  if (!file.is_open()) {
    if (std::filesystem::exists(path)) {
      if (error != nullptr) {
        *error = "failed to open observed topics file";
      }
    }
    return topics;
  }

  std::set<std::string> seen;
  std::string line;
  while (std::getline(file, line)) {
    const std::string topic = vc::config::trim(line);
    if (topic.empty()) {
      continue;
    }
    if (seen.insert(topic).second) {
      topics.push_back(topic);
    }
  }
  return topics;
}

std::string StatusText(int code) {
  switch (code) {
    case 200:
      return "OK";
    case 400:
      return "Bad Request";
    case 404:
      return "Not Found";
    case 405:
      return "Method Not Allowed";
    case 500:
      return "Internal Server Error";
    case 501:
      return "Not Implemented";
    case 503:
      return "Service Unavailable";
    default:
      return "Error";
  }
}

bool WriteAllSsl(SSL* ssl, const std::string& data) {
  std::size_t offset = 0;
  while (offset < data.size()) {
    const int written = SSL_write(
        ssl, data.data() + offset,
        static_cast<int>(std::min<std::size_t>(data.size() - offset, 16384)));
    if (written <= 0) {
      return false;
    }
    offset += static_cast<std::size_t>(written);
  }
  return true;
}

std::optional<std::string> ReadRequiredString(const JsonValue& object,
                                              const std::string& key,
                                              std::vector<ValidationError>* errors) {
  const auto field = GetObjectField(object, key);
  if (!field.has_value()) {
    if (errors != nullptr) {
      errors->push_back({key, key + " is required"});
    }
    return std::nullopt;
  }

  std::string value;
  if (!field->AsString(&value)) {
    if (errors != nullptr) {
      errors->push_back({key, key + " must be a string"});
    }
    return std::nullopt;
  }

  return value;
}

std::optional<int> ReadRequiredInt(const JsonValue& object, const std::string& key,
                                   std::vector<ValidationError>* errors) {
  const auto field = GetObjectField(object, key);
  if (!field.has_value()) {
    if (errors != nullptr) {
      errors->push_back({key, key + " is required"});
    }
    return std::nullopt;
  }

  double value = 0.0;
  if (!field->AsNumber(&value)) {
    if (errors != nullptr) {
      errors->push_back({key, key + " must be a number"});
    }
    return std::nullopt;
  }

  const int rounded = static_cast<int>(value);
  if (static_cast<double>(rounded) != value) {
    if (errors != nullptr) {
      errors->push_back({key, key + " must be an integer"});
    }
    return std::nullopt;
  }

  return rounded;
}

std::optional<bool> ReadRequiredBool(const JsonValue& object,
                                     const std::string& key,
                                     std::vector<ValidationError>* errors) {
  const auto field = GetObjectField(object, key);
  if (!field.has_value()) {
    if (errors != nullptr) {
      errors->push_back({key, key + " is required"});
    }
    return std::nullopt;
  }

  bool value = false;
  if (!field->AsBool(&value)) {
    if (errors != nullptr) {
      errors->push_back({key, key + " must be a boolean"});
    }
    return std::nullopt;
  }

  return value;
}

std::optional<std::vector<std::string>> ReadRequiredStringArray(
    const JsonValue& object, const std::string& key,
    std::vector<ValidationError>* errors) {
  const auto field = GetObjectField(object, key);
  if (!field.has_value()) {
    if (errors != nullptr) {
      errors->push_back({key, key + " is required"});
    }
    return std::nullopt;
  }

  std::vector<JsonValue> items;
  if (!field->AsArray(&items)) {
    if (errors != nullptr) {
      errors->push_back({key, key + " must be an array"});
    }
    return std::nullopt;
  }

  std::vector<std::string> output;
  output.reserve(items.size());
  for (std::size_t i = 0; i < items.size(); ++i) {
    std::string entry;
    if (!items[i].AsString(&entry)) {
      if (errors != nullptr) {
        errors->push_back({key,
                           key + "[" + std::to_string(i) + "] must be a string"});
      }
      continue;
    }
    output.push_back(entry);
  }

  return output;
}

std::optional<std::string> ReadOptionalString(const JsonValue& object,
                                              const std::string& key,
                                              std::vector<ValidationError>* errors) {
  const auto field = GetObjectField(object, key);
  if (!field.has_value()) {
    return std::nullopt;
  }

  std::string value;
  if (!field->AsString(&value)) {
    if (errors != nullptr) {
      errors->push_back({key, key + " must be a string"});
    }
    return std::nullopt;
  }

  return value;
}

std::string SerializeValidationErrors(
    const std::vector<ValidationError>& validation_errors) {
  std::string body = "[";
  for (std::size_t i = 0; i < validation_errors.size(); ++i) {
    if (i > 0) {
      body += ",";
    }
    body += "{";
    body += "\"field\":" + JsonString(validation_errors[i].field) + ",";
    body += "\"message\":" + JsonString(validation_errors[i].message);
    body += "}";
  }
  body += "]";
  return body;
}

std::string SerializeTopics(const std::vector<std::string>& topics) {
  std::string output = "[";
  for (std::size_t i = 0; i < topics.size(); ++i) {
    if (i > 0) {
      output += ",";
    }
    output += JsonString(topics[i]);
  }
  output += "]";
  return output;
}

std::string SerializeApplyStatus(const ApplyStatus& status) {
  std::string output = "{";
  output += "\"job_id\":" + std::to_string(status.job_id) + ",";
  output += "\"state\":" + JsonString(status.state) + ",";
  output += "\"started_at_utc\":" + JsonString(status.started_at_utc) + ",";
  output += "\"finished_at_utc\":" + JsonString(status.finished_at_utc) + ",";
  output += "\"error\":" + JsonString(status.error);
  output += "}";
  return output;
}

bool GenerateSelfSignedCertificate(const std::string& cert_path,
                                   const std::string& key_path,
                                   std::string* error) {
  EVP_PKEY* pkey = EVP_PKEY_new();
  if (pkey == nullptr) {
    if (error != nullptr) {
      *error = "EVP_PKEY_new failed";
    }
    return false;
  }

  RSA* rsa = RSA_new();
  BIGNUM* exponent = BN_new();
  X509* cert = nullptr;
  X509_NAME* name = nullptr;
  FILE* key_file = nullptr;
  FILE* cert_file = nullptr;

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
  X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC,
                             reinterpret_cast<const unsigned char*>("US"), -1, -1,
                             0);
  X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC,
                             reinterpret_cast<const unsigned char*>("VirtualChime"),
                             -1, -1, 0);
  X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                             reinterpret_cast<const unsigned char*>("chime.local"),
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
  if (PEM_write_PrivateKey(key_file, pkey, nullptr, nullptr, 0, nullptr, nullptr) !=
      1) {
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

}  // namespace

WebServer::WebServer(vc::logging::Logger& logger, ConfigStore& config_store,
                     WifiScanner& wifi_scanner, ApplyManager& apply_manager,
                     std::string bind_address, int port, std::string cert_path,
                     std::string key_path, std::string ui_dist_dir,
                     std::string observed_topics_path)
    : logger_(logger),
      config_store_(config_store),
      wifi_scanner_(wifi_scanner),
      apply_manager_(apply_manager),
      bind_address_(std::move(bind_address)),
      port_(port),
      cert_path_(std::move(cert_path)),
      key_path_(std::move(key_path)),
      ui_dist_dir_(std::move(ui_dist_dir)),
      observed_topics_path_(std::move(observed_topics_path)) {}

WebServer::~WebServer() { Stop(); }

bool WebServer::Start() {
  if (running_.load()) {
    return true;
  }

  std::string error;
  if (!EnsureTlsMaterial(&error)) {
    logger_.Error("webd", "TLS setup failed: " + error);
    return false;
  }

  SSL_load_error_strings();
  OpenSSL_add_ssl_algorithms();

  SSL_CTX* ctx = SSL_CTX_new(TLS_server_method());
  if (ctx == nullptr) {
    logger_.Error("webd", "SSL_CTX_new failed");
    return false;
  }

  SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);

  if (SSL_CTX_use_certificate_file(ctx, cert_path_.c_str(), SSL_FILETYPE_PEM) != 1) {
    logger_.Error("webd", "failed to load TLS certificate from " + cert_path_);
    SSL_CTX_free(ctx);
    return false;
  }

  if (SSL_CTX_use_PrivateKey_file(ctx, key_path_.c_str(), SSL_FILETYPE_PEM) != 1) {
    logger_.Error("webd", "failed to load TLS private key from " + key_path_);
    SSL_CTX_free(ctx);
    return false;
  }

  listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd_ < 0) {
    logger_.Error("webd", "socket() failed");
    SSL_CTX_free(ctx);
    return false;
  }

  const int reuse = 1;
  setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  struct sockaddr_in address {};
  address.sin_family = AF_INET;
  address.sin_port = htons(static_cast<uint16_t>(port_));
  if (inet_pton(AF_INET, bind_address_.c_str(), &address.sin_addr) != 1) {
    logger_.Error("webd", "invalid bind address: " + bind_address_);
    close(listen_fd_);
    listen_fd_ = -1;
    SSL_CTX_free(ctx);
    return false;
  }

  if (bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&address),
           sizeof(address)) != 0) {
    logger_.Error("webd", "bind() failed on " + bind_address_ + ":" +
                              std::to_string(port_));
    close(listen_fd_);
    listen_fd_ = -1;
    SSL_CTX_free(ctx);
    return false;
  }

  if (listen(listen_fd_, 16) != 0) {
    logger_.Error("webd", "listen() failed");
    close(listen_fd_);
    listen_fd_ = -1;
    SSL_CTX_free(ctx);
    return false;
  }

  ssl_ctx_ = ctx;
  running_.store(true);
  accept_thread_ = std::thread([this]() { AcceptLoop(); });

  logger_.Info("webd", "https server listening on " + bind_address_ + ":" +
                           std::to_string(port_));
  return true;
}

void WebServer::Stop() {
  if (!running_.exchange(false)) {
    return;
  }

  if (listen_fd_ >= 0) {
    shutdown(listen_fd_, SHUT_RDWR);
    close(listen_fd_);
    listen_fd_ = -1;
  }

  if (accept_thread_.joinable()) {
    accept_thread_.join();
  }

  if (ssl_ctx_ != nullptr) {
    SSL_CTX_free(static_cast<SSL_CTX*>(ssl_ctx_));
    ssl_ctx_ = nullptr;
  }

  EVP_cleanup();
}

void WebServer::AcceptLoop() {
  while (running_.load()) {
    struct sockaddr_in client_addr {};
    socklen_t client_len = sizeof(client_addr);
    const int client_fd =
        accept(listen_fd_, reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);

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

    HandleConnection(client_fd);
  }
}

void WebServer::HandleConnection(int client_fd) {
  SSL* ssl = SSL_new(static_cast<SSL_CTX*>(ssl_ctx_));
  if (ssl == nullptr) {
    close(client_fd);
    return;
  }

  SSL_set_fd(ssl, client_fd);
  if (SSL_accept(ssl) <= 0) {
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(client_fd);
    return;
  }

  HttpRequest request;
  std::string read_error;
  HttpResponse response;
  if (!ReadHttpRequest(ssl, &request, &read_error)) {
    response.status = 400;
    response.body = "{\"error\":\"bad_request\",\"message\":" +
                    JsonString(read_error) + "}";
  } else {
    response = Route(request);
  }

  std::string raw;
  raw += "HTTP/1.1 " + std::to_string(response.status) + " " +
         StatusText(response.status) + "\r\n";
  raw += "Content-Type: " + response.content_type + "\r\n";
  raw += "Content-Length: " + std::to_string(response.body.size()) + "\r\n";
  raw += "Cache-Control: " + response.cache_control + "\r\n";
  raw += "Connection: close\r\n";
  raw += "\r\n";
  raw += response.body;

  WriteAllSsl(ssl, raw);

  SSL_shutdown(ssl);
  SSL_free(ssl);
  close(client_fd);
}

bool WebServer::ReadHttpRequest(void* ssl_ptr, HttpRequest* request,
                                std::string* error) const {
  if (ssl_ptr == nullptr || request == nullptr || error == nullptr) {
    return false;
  }

  SSL* ssl = static_cast<SSL*>(ssl_ptr);

  std::string data;
  data.reserve(2048);

  std::size_t headers_end = std::string::npos;
  while (headers_end == std::string::npos) {
    if (data.size() >= kMaxRequestBytes) {
      *error = "request too large";
      return false;
    }

    std::array<char, 2048> buffer{};
    const int bytes = SSL_read(ssl, buffer.data(), static_cast<int>(buffer.size()));
    if (bytes <= 0) {
      *error = "failed to read request";
      return false;
    }

    data.append(buffer.data(), static_cast<std::size_t>(bytes));
    headers_end = data.find("\r\n\r\n");
  }

  const std::string header_blob = data.substr(0, headers_end);
  std::istringstream header_stream(header_blob);

  std::string request_line;
  if (!std::getline(header_stream, request_line)) {
    *error = "missing request line";
    return false;
  }
  if (!request_line.empty() && request_line.back() == '\r') {
    request_line.pop_back();
  }

  std::istringstream request_line_stream(request_line);
  std::string method;
  std::string path;
  std::string version;
  request_line_stream >> method >> path >> version;
  if (method.empty() || path.empty() || version.empty()) {
    *error = "invalid request line";
    return false;
  }

  std::map<std::string, std::string> headers;
  std::string header_line;
  while (std::getline(header_stream, header_line)) {
    if (!header_line.empty() && header_line.back() == '\r') {
      header_line.pop_back();
    }
    const auto sep = header_line.find(':');
    if (sep == std::string::npos) {
      continue;
    }
    const std::string key = ToLower(vc::config::trim(header_line.substr(0, sep)));
    const std::string value = vc::config::trim(header_line.substr(sep + 1));
    headers[key] = value;
  }

  std::size_t content_length = 0;
  const auto content_length_it = headers.find("content-length");
  if (content_length_it != headers.end()) {
    char* end = nullptr;
    const long parsed = std::strtol(content_length_it->second.c_str(), &end, 10);
    if (end == nullptr || *end != '\0' || parsed < 0) {
      *error = "invalid Content-Length";
      return false;
    }
    content_length = static_cast<std::size_t>(parsed);
  }

  if (content_length > kMaxBodyBytes) {
    *error = "request body too large";
    return false;
  }

  std::string body = data.substr(headers_end + 4);
  while (body.size() < content_length) {
    std::array<char, 2048> buffer{};
    const int bytes = SSL_read(ssl, buffer.data(), static_cast<int>(buffer.size()));
    if (bytes <= 0) {
      *error = "failed to read request body";
      return false;
    }
    body.append(buffer.data(), static_cast<std::size_t>(bytes));
    if (body.size() > kMaxBodyBytes) {
      *error = "request body too large";
      return false;
    }
  }

  if (body.size() > content_length) {
    body.resize(content_length);
  }

  const auto query = path.find('?');
  if (query != std::string::npos) {
    path = path.substr(0, query);
  }

  request->method = method;
  request->path = path;
  request->body = std::move(body);
  return true;
}

WebServer::HttpResponse WebServer::Route(const HttpRequest& request) {
  if (request.path == "/api/v1/config/core") {
    if (request.method == "GET") {
      return HandleGetCoreConfig();
    }
    if (request.method == "POST") {
      return HandlePostCoreConfig(request);
    }
    HttpResponse response;
    response.status = 405;
    response.body = "{\"error\":\"method_not_allowed\"}";
    return response;
  }

  if (request.path == "/api/v1/wifi/scan") {
    if (request.method != "GET") {
      HttpResponse response;
      response.status = 405;
      response.body = "{\"error\":\"method_not_allowed\"}";
      return response;
    }
    return HandleWifiScan();
  }

  if (request.path == "/api/v1/mqtt/topics") {
    if (request.method != "GET") {
      HttpResponse response;
      response.status = 405;
      response.body = "{\"error\":\"method_not_allowed\"}";
      return response;
    }
    return HandleGetObservedTopics();
  }

  if (request.path == "/api/v1/system" || request.path == "/api/v1/device" ||
      request.path == "/api/v1/diagnostics" ||
      request.path.rfind("/api/v1/system/", 0) == 0 ||
      request.path.rfind("/api/v1/device/", 0) == 0 ||
      request.path.rfind("/api/v1/diagnostics/", 0) == 0) {
    return ReservedNotImplemented(request.path);
  }

  if (const auto ui_response = TryServeExternalUi(request);
      ui_response.has_value()) {
    return *ui_response;
  }

  if (request.method == "GET" && request.path == "/") {
    HttpResponse response;
    response.status = 200;
    response.content_type = "text/html; charset=utf-8";
    response.body = MainPageHtml();
    return response;
  }

  HttpResponse response;
  response.status = 404;
  response.body = "{\"error\":\"not_found\"}";
  return response;
}

WebServer::HttpResponse WebServer::HandleGetCoreConfig() {
  const SaveResult loaded = config_store_.LoadCoreConfig();
  HttpResponse response;
  if (!loaded.success) {
    response.status = 500;
    response.body = "{\"error\":\"load_failed\",\"message\":" +
                    JsonString(loaded.error) + "}";
    return response;
  }

  response.status = 200;
  response.body = "{";
  response.body +=
      "\"wifi_ssid\":" + JsonString(loaded.snapshot.config.wifi_ssid) + ",";
  response.body += "\"wifi_password_set\":" +
                   JsonBool(loaded.snapshot.wifi_password_set) + ",";
  response.body +=
      "\"mqtt_host\":" + JsonString(loaded.snapshot.config.mqtt_host) + ",";
  response.body +=
      "\"mqtt_port\":" + JsonNumber(loaded.snapshot.config.mqtt_port) + ",";
  response.body += "\"mqtt_client_id\":" +
                   JsonString(loaded.snapshot.config.mqtt_client_id) + ",";
  response.body += "\"mqtt_username\":" +
                   JsonString(loaded.snapshot.config.mqtt_username) + ",";
  response.body += "\"mqtt_password_set\":" +
                   JsonBool(loaded.snapshot.mqtt_password_set) + ",";
  response.body += "\"mqtt_tls_enabled\":" +
                   JsonBool(loaded.snapshot.config.mqtt_tls_enabled) + ",";
  response.body += "\"mqtt_tls_validate_certificate\":" +
                   JsonBool(
                       loaded.snapshot.config.mqtt_tls_validate_certificate) +
                   ",";
  response.body += "\"mqtt_tls_ca_file\":" +
                   JsonString(loaded.snapshot.config.mqtt_tls_ca_file) + ",";
  response.body += "\"mqtt_tls_cert_file\":" +
                   JsonString(loaded.snapshot.config.mqtt_tls_cert_file) + ",";
  response.body += "\"mqtt_tls_key_file\":" +
                   JsonString(loaded.snapshot.config.mqtt_tls_key_file) + ",";
  response.body += "\"mqtt_topics\":" +
                   SerializeTopics(loaded.snapshot.config.mqtt_topics) + ",";
  response.body +=
      "\"ring_topic\":" + JsonString(loaded.snapshot.config.ring_topic) + ",";
  response.body +=
      "\"apply\":" + SerializeApplyStatus(apply_manager_.CurrentStatus());
  response.body += "}";
  return response;
}

WebServer::HttpResponse WebServer::HandlePostCoreConfig(
    const HttpRequest& request) {
  HttpResponse response;

  const JsonParseResult parsed = ParseJson(request.body);
  if (!parsed.success) {
    response.status = 400;
    response.body = "{\"error\":\"invalid_json\",\"message\":" +
                    JsonString(parsed.error) + "}";
    return response;
  }

  if (parsed.value.type() != JsonValue::Type::kObject) {
    response.status = 400;
    response.body = "{\"error\":\"invalid_payload\",\"message\":\"payload must be an object\"}";
    return response;
  }

  std::vector<ValidationError> parse_errors;

  SaveRequest save_request;

  const auto wifi_ssid = ReadRequiredString(parsed.value, "wifi_ssid", &parse_errors);
  const auto mqtt_host = ReadRequiredString(parsed.value, "mqtt_host", &parse_errors);
  const auto mqtt_port = ReadRequiredInt(parsed.value, "mqtt_port", &parse_errors);
  const auto mqtt_client_id =
      ReadRequiredString(parsed.value, "mqtt_client_id", &parse_errors);
  const auto mqtt_username =
      ReadRequiredString(parsed.value, "mqtt_username", &parse_errors);
  const auto mqtt_tls_enabled =
      ReadRequiredBool(parsed.value, "mqtt_tls_enabled", &parse_errors);
  const auto mqtt_tls_validate_certificate = ReadRequiredBool(
      parsed.value, "mqtt_tls_validate_certificate", &parse_errors);
  const auto mqtt_tls_ca_file =
      ReadRequiredString(parsed.value, "mqtt_tls_ca_file", &parse_errors);
  const auto mqtt_tls_cert_file =
      ReadRequiredString(parsed.value, "mqtt_tls_cert_file", &parse_errors);
  const auto mqtt_tls_key_file =
      ReadRequiredString(parsed.value, "mqtt_tls_key_file", &parse_errors);
  const auto mqtt_topics =
      ReadRequiredStringArray(parsed.value, "mqtt_topics", &parse_errors);
  const auto ring_topic = ReadRequiredString(parsed.value, "ring_topic", &parse_errors);
  const auto wifi_password =
      ReadOptionalString(parsed.value, "wifi_password", &parse_errors);
  const auto mqtt_password =
      ReadOptionalString(parsed.value, "mqtt_password", &parse_errors);

  if (!parse_errors.empty()) {
    response.status = 400;
    response.body = "{";
    response.body += "\"error\":\"validation_failed\",";
    response.body +=
        "\"validation_errors\":" + SerializeValidationErrors(parse_errors);
    response.body += "}";
    return response;
  }

  save_request.config.wifi_ssid = *wifi_ssid;
  save_request.config.mqtt_host = *mqtt_host;
  save_request.config.mqtt_port = *mqtt_port;
  save_request.config.mqtt_client_id = *mqtt_client_id;
  save_request.config.mqtt_username = *mqtt_username;
  save_request.config.mqtt_tls_enabled = *mqtt_tls_enabled;
  save_request.config.mqtt_tls_validate_certificate =
      *mqtt_tls_validate_certificate;
  save_request.config.mqtt_tls_ca_file = *mqtt_tls_ca_file;
  save_request.config.mqtt_tls_cert_file = *mqtt_tls_cert_file;
  save_request.config.mqtt_tls_key_file = *mqtt_tls_key_file;
  save_request.config.mqtt_topics = *mqtt_topics;
  save_request.config.ring_topic = *ring_topic;
  save_request.wifi_password = wifi_password;
  save_request.mqtt_password = mqtt_password;

  const SaveResult saved = config_store_.SaveCoreConfig(save_request);
  if (!saved.validation_errors.empty()) {
    response.status = 400;
    response.body = "{";
    response.body += "\"error\":\"validation_failed\",";
    response.body +=
        "\"validation_errors\":" + SerializeValidationErrors(saved.validation_errors);
    response.body += "}";
    return response;
  }

  if (!saved.success) {
    response.status = 500;
    response.body = "{\"error\":\"save_failed\",\"message\":" +
                    JsonString(saved.error) + "}";
    return response;
  }

  const ApplyStatus apply = apply_manager_.StartApply();

  response.status = 200;
  response.body = "{";
  response.body +=
      "\"wifi_ssid\":" + JsonString(saved.snapshot.config.wifi_ssid) + ",";
  response.body += "\"wifi_password_set\":" +
                   JsonBool(saved.snapshot.wifi_password_set) + ",";
  response.body +=
      "\"mqtt_host\":" + JsonString(saved.snapshot.config.mqtt_host) + ",";
  response.body +=
      "\"mqtt_port\":" + JsonNumber(saved.snapshot.config.mqtt_port) + ",";
  response.body += "\"mqtt_client_id\":" +
                   JsonString(saved.snapshot.config.mqtt_client_id) + ",";
  response.body += "\"mqtt_username\":" +
                   JsonString(saved.snapshot.config.mqtt_username) + ",";
  response.body += "\"mqtt_password_set\":" +
                   JsonBool(saved.snapshot.mqtt_password_set) + ",";
  response.body += "\"mqtt_tls_enabled\":" +
                   JsonBool(saved.snapshot.config.mqtt_tls_enabled) + ",";
  response.body += "\"mqtt_tls_validate_certificate\":" +
                   JsonBool(
                       saved.snapshot.config.mqtt_tls_validate_certificate) +
                   ",";
  response.body += "\"mqtt_tls_ca_file\":" +
                   JsonString(saved.snapshot.config.mqtt_tls_ca_file) + ",";
  response.body += "\"mqtt_tls_cert_file\":" +
                   JsonString(saved.snapshot.config.mqtt_tls_cert_file) + ",";
  response.body += "\"mqtt_tls_key_file\":" +
                   JsonString(saved.snapshot.config.mqtt_tls_key_file) + ",";
  response.body += "\"mqtt_topics\":" +
                   SerializeTopics(saved.snapshot.config.mqtt_topics) + ",";
  response.body +=
      "\"ring_topic\":" + JsonString(saved.snapshot.config.ring_topic) + ",";
  response.body += "\"apply\":" + SerializeApplyStatus(apply);
  response.body += "}";

  return response;
}

WebServer::HttpResponse WebServer::HandleWifiScan() {
  const WifiScanResult scan = wifi_scanner_.Scan();
  HttpResponse response;
  if (!scan.success) {
    response.status = 503;
    response.body = "{\"error\":\"scan_failed\",\"message\":" +
                    JsonString(scan.error) + "}";
    return response;
  }

  response.status = 200;
  response.body = "{";
  response.body += "\"networks\":[";
  for (std::size_t i = 0; i < scan.networks.size(); ++i) {
    if (i > 0) {
      response.body += ",";
    }
    response.body += "{";
    response.body += "\"ssid\":" + JsonString(scan.networks[i].ssid) + ",";
    response.body +=
        "\"signal_dbm\":" + JsonNumber(scan.networks[i].signal_dbm) + ",";
    response.body +=
        "\"security\":" + JsonString(scan.networks[i].security);
    response.body += "}";
  }
  response.body += "]";
  response.body += "}";

  return response;
}

WebServer::HttpResponse WebServer::HandleGetObservedTopics() {
  HttpResponse response;
  std::string read_error;
  const std::vector<std::string> topics =
      ReadObservedTopicsFromFile(observed_topics_path_, &read_error);

  if (!read_error.empty()) {
    logger_.Warn("webd", read_error + " path=" + observed_topics_path_);
  }

  response.status = 200;
  response.body = "{";
  response.body += "\"topics\":" + SerializeTopics(topics);
  response.body += "}";
  return response;
}

std::optional<WebServer::HttpResponse> WebServer::TryServeExternalUi(
    const HttpRequest& request) const {
  if (ui_dist_dir_.empty() || request.method != "GET") {
    return std::nullopt;
  }
  if (request.path.empty() || request.path[0] != '/' ||
      StartsWith(request.path, "/api/")) {
    return std::nullopt;
  }

  const std::filesystem::path root(ui_dist_dir_);
  std::error_code ec;
  if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) {
    return std::nullopt;
  }

  auto response_from_file = [&](const std::filesystem::path& file_path,
                                const std::string& request_path)
      -> std::optional<HttpResponse> {
    if (!std::filesystem::exists(file_path, ec) ||
        !std::filesystem::is_regular_file(file_path, ec)) {
      return std::nullopt;
    }
    std::string body;
    if (!ReadFile(file_path, &body)) {
      HttpResponse response;
      response.status = 500;
      response.body = "{\"error\":\"ui_read_failed\"}";
      return response;
    }
    HttpResponse response;
    response.status = 200;
    response.content_type = ContentTypeForPath(file_path);
    response.cache_control = CacheControlForPath(request_path, file_path);
    response.body = std::move(body);
    return response;
  };

  if (request.path == "/") {
    const auto index_response = response_from_file(root / "index.html", "/");
    if (index_response.has_value()) {
      return index_response;
    }
    return std::nullopt;
  }

  const std::filesystem::path relative_path =
      std::filesystem::path(request.path).relative_path();
  if (relative_path.empty() || !IsSafeRelativePath(relative_path)) {
    HttpResponse response;
    response.status = 404;
    response.body = "{\"error\":\"not_found\"}";
    return response;
  }

  if (const auto file_response =
          response_from_file(root / relative_path, request.path);
      file_response.has_value()) {
    return file_response;
  }

  if (!StartsWith(request.path, "/assets/") &&
      relative_path.extension().empty()) {
    if (const auto fallback_index = response_from_file(root / "index.html", "/");
        fallback_index.has_value()) {
      return fallback_index;
    }
  }

  HttpResponse response;
  response.status = 404;
  response.body = "{\"error\":\"not_found\"}";
  return response;
}

WebServer::HttpResponse WebServer::ReservedNotImplemented(
    const std::string& path) const {
  HttpResponse response;
  response.status = 501;
  response.body = "{";
  response.body += "\"error\":\"not_implemented\",";
  response.body += "\"message\":\"reserved endpoint\",";
  response.body += "\"path\":" + JsonString(path);
  response.body += "}";
  return response;
}

bool WebServer::EnsureTlsMaterial(std::string* error) const {
  const bool cert_exists = std::filesystem::exists(cert_path_);
  const bool key_exists = std::filesystem::exists(key_path_);
  if (cert_exists && key_exists) {
    return true;
  }

  std::error_code ec;
  std::filesystem::create_directories(
      std::filesystem::path(cert_path_).parent_path(), ec);
  if (ec) {
    if (error != nullptr) {
      *error = "failed to create cert directory: " + ec.message();
    }
    return false;
  }

  std::filesystem::create_directories(
      std::filesystem::path(key_path_).parent_path(), ec);
  if (ec) {
    if (error != nullptr) {
      *error = "failed to create key directory: " + ec.message();
    }
    return false;
  }

  return GenerateSelfSignedCertificate(cert_path_, key_path_, error);
}

}  // namespace chime::webd
