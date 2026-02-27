#include "vc/mqtt/client.h"

#include <mosquitto.h>

#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <sys/socket.h>

#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "vc/logging/logger.h"

namespace vc::mqtt {
namespace {
std::mutex g_lib_mutex;
int g_lib_users = 0;

bool AcquireMosquittoLib(std::string* error) {
  const std::lock_guard<std::mutex> lock(g_lib_mutex);
  if (g_lib_users == 0) {
    const int rc = mosquitto_lib_init();
    if (rc != MOSQ_ERR_SUCCESS) {
      if (error != nullptr) {
        *error = "mosquitto_lib_init failed: " +
                 std::string(mosquitto_strerror(rc));
      }
      return false;
    }
  }
  ++g_lib_users;
  return true;
}

void ReleaseMosquittoLib() {
  const std::lock_guard<std::mutex> lock(g_lib_mutex);
  if (g_lib_users <= 0) {
    return;
  }
  --g_lib_users;
  if (g_lib_users == 0) {
    mosquitto_lib_cleanup();
  }
}

bool EndsWith(const std::string& value, const std::string& suffix) {
  if (value.size() < suffix.size()) {
    return false;
  }
  return value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string Join(const std::vector<std::string>& items, const std::string& sep) {
  std::string output;
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (i > 0) {
      output += sep;
    }
    output += items[i];
  }
  return output;
}

std::string DescribeHostLookup(const std::string& host, int port) {
  struct addrinfo hints {};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  struct addrinfo* results = nullptr;
  const std::string port_string = std::to_string(port);
  const int rc =
      getaddrinfo(host.c_str(), port_string.c_str(), &hints, &results);
  if (rc != 0) {
    std::string message = "resolver failed for '" + host + ":" + port_string +
                          "': " + gai_strerror(rc);
    if (EndsWith(host, ".local")) {
      message +=
          " ('.local' usually needs mDNS; try broker IP or DNS hostname)";
    }
    return message;
  }

  std::set<std::string> unique_addresses;
  for (const struct addrinfo* it = results; it != nullptr; it = it->ai_next) {
    char address[NI_MAXHOST] = {};
    const int name_rc =
        getnameinfo(it->ai_addr, it->ai_addrlen, address, sizeof(address),
                    nullptr, 0, NI_NUMERICHOST);
    if (name_rc == 0) {
      unique_addresses.insert(std::string(address));
    }
  }
  freeaddrinfo(results);

  if (unique_addresses.empty()) {
    return "resolver succeeded for '" + host + ":" + port_string +
           "' but produced no numeric addresses";
  }

  const std::vector<std::string> addresses(unique_addresses.begin(),
                                           unique_addresses.end());
  return "resolver addresses for '" + host + ":" + port_string + "': " +
         Join(addresses, ", ");
}
}  // namespace

Client::Client(vc::logging::Logger& logger, EventHandler& handler)
    : logger_(logger), handler_(handler) {
  std::string error;
  lib_ready_ = AcquireMosquittoLib(&error);
  if (!lib_ready_) {
    SetLastError(error);
    logger_.Error("mqtt", error);
  }
}

Client::~Client() {
  DestroyClient();
  if (lib_ready_) {
    ReleaseMosquittoLib();
  }
}

bool Client::Connect(const std::string& host, int port,
                     const ConnectOptions& options) {
  if (!lib_ready_) {
    return false;
  }

  DestroyClient();

  mosq_ = mosquitto_new(options.client_id.c_str(), true, this);
  if (mosq_ == nullptr) {
    SetLastError("failed to create client");
    return false;
  }

  mosquitto_connect_callback_set(mosq_, Client::HandleConnect);
  mosquitto_disconnect_callback_set(mosq_, Client::HandleDisconnect);
  mosquitto_message_callback_set(mosq_, Client::HandleMessage);
  mosquitto_reconnect_delay_set(mosq_, options.reconnect_min_seconds,
                                options.reconnect_max_seconds,
                                options.reconnect_exponential_backoff);

  if (!options.username.empty()) {
    const char* password =
        options.password.empty() ? nullptr : options.password.c_str();
    const int auth_rc =
        mosquitto_username_pw_set(mosq_, options.username.c_str(), password);
    if (auth_rc != MOSQ_ERR_SUCCESS) {
      SetLastError("mqtt auth setup failed: " +
                   std::string(mosquitto_strerror(auth_rc)));
      return false;
    }
  }

  if (options.tls_enabled) {
    const char* ca_file =
        options.tls_ca_file.empty() ? nullptr : options.tls_ca_file.c_str();
    const char* cert_file =
        options.tls_cert_file.empty() ? nullptr : options.tls_cert_file.c_str();
    const char* key_file =
        options.tls_key_file.empty() ? nullptr : options.tls_key_file.c_str();

    if ((cert_file == nullptr) != (key_file == nullptr)) {
      SetLastError(
          "mqtt tls setup failed: tls_cert_file and tls_key_file must both be set");
      return false;
    }

    const int tls_rc =
        mosquitto_tls_set(mosq_, ca_file, nullptr, cert_file, key_file, nullptr);
    if (tls_rc != MOSQ_ERR_SUCCESS) {
      SetLastError("mqtt tls setup failed: " +
                   std::string(mosquitto_strerror(tls_rc)));
      return false;
    }

    const int insecure_rc =
        mosquitto_tls_insecure_set(mosq_, !options.tls_validate_certificate);
    if (insecure_rc != MOSQ_ERR_SUCCESS) {
      SetLastError("mqtt tls verify setup failed: " +
                   std::string(mosquitto_strerror(insecure_rc)));
      return false;
    }
  }

  const int rc =
      mosquitto_connect(mosq_, host.c_str(), port, options.keepalive_seconds);
  if (rc != MOSQ_ERR_SUCCESS) {
    std::string error =
        "connect failed: " + std::string(mosquitto_strerror(rc));
    if (rc == MOSQ_ERR_ERRNO) {
      error += " (" + std::string(std::strerror(errno)) + ")";
    }

    bool include_lookup_details = false;
#ifdef MOSQ_ERR_EAI
    include_lookup_details = (rc == MOSQ_ERR_EAI);
#endif
    if (!include_lookup_details &&
        std::string(mosquitto_strerror(rc)) == "Lookup error.") {
      include_lookup_details = true;
    }
    if (include_lookup_details) {
      error += " [" + DescribeHostLookup(host, port) + "]";
    }

    SetLastError(error);
    return false;
  }

  connected_ = false;
  last_error_.clear();
  return true;
}

int Client::Loop(int timeout_ms, int max_packets) {
  if (mosq_ == nullptr) {
    SetLastError("loop called before connect");
    return MOSQ_ERR_INVAL;
  }

  const int rc = mosquitto_loop(mosq_, timeout_ms, max_packets);
  if (rc != MOSQ_ERR_SUCCESS) {
    SetLastError("loop error: " + std::string(mosquitto_strerror(rc)));
  }
  return rc;
}

bool Client::Reconnect() {
  if (mosq_ == nullptr) {
    SetLastError("reconnect called before connect");
    return false;
  }
  const int rc = mosquitto_reconnect(mosq_);
  if (rc != MOSQ_ERR_SUCCESS) {
    SetLastError("reconnect failed: " + std::string(mosquitto_strerror(rc)));
    return false;
  }
  return true;
}

bool Client::Disconnect() {
  if (mosq_ == nullptr) {
    return true;
  }
  const int rc = mosquitto_disconnect(mosq_);
  if (rc != MOSQ_ERR_SUCCESS) {
    SetLastError("disconnect returned: " + std::string(mosquitto_strerror(rc)));
    return false;
  }
  return true;
}

bool Client::Subscribe(const std::string& topic, int qos) {
  if (mosq_ == nullptr) {
    SetLastError("subscribe called before connect");
    return false;
  }
  const int rc = mosquitto_subscribe(mosq_, nullptr, topic.c_str(), qos);
  if (rc != MOSQ_ERR_SUCCESS) {
    SetLastError("subscribe failed topic='" + topic + "': " +
                 std::string(mosquitto_strerror(rc)));
    return false;
  }
  return true;
}

bool Client::Publish(const std::string& topic, const std::string& payload, int qos,
                     bool retain) {
  if (mosq_ == nullptr) {
    SetLastError("publish called before connect");
    return false;
  }
  const int rc = mosquitto_publish(mosq_, nullptr, topic.c_str(),
                                   static_cast<int>(payload.size()),
                                   payload.c_str(), qos, retain);
  if (rc != MOSQ_ERR_SUCCESS) {
    SetLastError("publish failed topic='" + topic + "': " +
                 std::string(mosquitto_strerror(rc)));
    return false;
  }
  return true;
}

bool Client::IsConnected() const { return connected_; }

std::string Client::LastError() const { return last_error_; }

void Client::HandleConnect(struct mosquitto*, void* obj, int rc) {
  auto* self = static_cast<Client*>(obj);
  if (self == nullptr) {
    return;
  }
  self->connected_ = (rc == MOSQ_ERR_SUCCESS);
  self->handler_.OnConnect(rc);
}

void Client::HandleDisconnect(struct mosquitto*, void* obj, int rc) {
  auto* self = static_cast<Client*>(obj);
  if (self == nullptr) {
    return;
  }
  self->connected_ = false;
  self->handler_.OnDisconnect(rc);
}

void Client::HandleMessage(struct mosquitto*, void* obj,
                           const struct mosquitto_message* msg) {
  auto* self = static_cast<Client*>(obj);
  if (self == nullptr || msg == nullptr || msg->topic == nullptr) {
    return;
  }

  Message message;
  message.topic = msg->topic;
  message.qos = msg->qos;
  message.retain = (msg->retain != 0);
  if (msg->payload != nullptr && msg->payloadlen > 0) {
    message.payload.assign(static_cast<const char*>(msg->payload),
                           static_cast<std::size_t>(msg->payloadlen));
  }

  self->handler_.OnMessage(message);
}

void Client::SetLastError(const std::string& message) { last_error_ = message; }

void Client::DestroyClient() {
  if (mosq_ == nullptr) {
    return;
  }
  mosquitto_destroy(mosq_);
  mosq_ = nullptr;
  connected_ = false;
}

}  // namespace vc::mqtt
