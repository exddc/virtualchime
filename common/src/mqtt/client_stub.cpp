#include "vc/mqtt/client.h"

namespace vc::mqtt {

Client::Client(vc::logging::Logger &logger, EventHandler &handler) : logger_(logger), handler_(handler) {
    SetLastError("libmosquitto not available in this build");
}

Client::~Client() = default;

bool Client::Connect(const std::string &, int, const ConnectOptions &) {
    connected_ = false;
    SetLastError("connect unavailable: libmosquitto not available in this build");
    return false;
}

int Client::Loop(int, int) {
    SetLastError("loop unavailable: libmosquitto not available in this build");
    return -1;
}

bool Client::Reconnect() {
    SetLastError("reconnect unavailable: libmosquitto not available in this build");
    return false;
}

bool Client::Disconnect() {
    connected_ = false;
    return true;
}

bool Client::Subscribe(const std::string &, int) {
    SetLastError("subscribe unavailable: libmosquitto not available in this build");
    return false;
}

bool Client::Publish(const std::string &, const std::string &, int, bool) {
    SetLastError("publish unavailable: libmosquitto not available in this build");
    return false;
}

bool Client::IsConnected() const {
    return connected_;
}

std::string Client::LastError() const {
    return last_error_;
}

void Client::HandleConnect(struct mosquitto *, void *, int) {}
void Client::HandleDisconnect(struct mosquitto *, void *, int) {}
void Client::HandleMessage(struct mosquitto *, void *, const struct mosquitto_message *) {}

void Client::SetLastError(const std::string &message) {
    last_error_ = message;
}

void Client::DestroyClient() {}

} // namespace vc::mqtt
