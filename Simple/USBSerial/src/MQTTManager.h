#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <PubSubClient.h>

class MQTTManager
{
public:
    MQTTManager();

    void begin(
        const char *deviceName,
        const char *firmwareVersion,
        uint32_t (*getUsbRx)(),
        uint32_t (*getUsbTx)(),
        uint32_t (*getKnxRx)(),
        uint32_t (*getKnxTx)()
    );

    void loop();

    bool isEnabled();
    bool isConnected();

int getState();
bool getLastStatusPublishOk();
String getLastError();

    String getHost();
    uint16_t getPort();
    String getUsername();
    String getBaseTopic();

    void saveConfig(
        bool enabled,
        const String &host,
        uint16_t port,
        const String &username,
        const String &password,
        const String &baseTopic
    );

    void publishEvent(
        const char *type,
        const char *event
    );

    void publishError(
        const char *code,
        const char *message
    );

private:
    Preferences preferences;

    WiFiClient wifiClient;
    PubSubClient mqttClient;

    String deviceName;
    String firmwareVersion;

    uint32_t (*getUsbRx)();
    uint32_t (*getUsbTx)();
    uint32_t (*getKnxRx)();
    uint32_t (*getKnxTx)();

bool lastStatusPublishOk;
int lastMqttState;
String lastError;

    bool enabled;
    String host;
    uint16_t port;
    String username;
    String password;
    String baseTopic;

    uint32_t lastConnectAttempt;
    uint32_t lastStatusPublish;

    static const uint32_t CONNECT_INTERVAL_MS = 10000;
    static const uint32_t STATUS_INTERVAL_MS = 10000;

    void loadConfig();

    bool connect();

    void publishAvailability(const char *state);
    bool publishStatus();

    String jsonEscape(const String &value);
    String resetReason();

    String topic(const char *suffix);
};