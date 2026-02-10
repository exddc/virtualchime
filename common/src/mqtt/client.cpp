#include "vc/mqtt/client.h"

#include <mosquitto.h>

#include <mutex>
#include <string>

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

  const int rc =
      mosquitto_connect(mosq_, host.c_str(), port, options.keepalive_seconds);
  if (rc != MOSQ_ERR_SUCCESS) {
    SetLastError("connect failed: " + std::string(mosquitto_strerror(rc)));
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
