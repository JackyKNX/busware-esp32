#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <Preferences.h>

#include "MQTTManager.h"

class WebManager
{
MQTTManager *mqttManager;

void handleMQTT();
void handleMQTTSave();

public:
    WebManager();

void begin(
    const char *deviceName,
    const char *version,
    uint32_t (*getUsbRx)(),
    uint32_t (*getUsbTx)(),
    uint32_t (*getKnxRx)(),
    uint32_t (*getKnxTx)(),
    MQTTManager *mqttManager
);
    void loop();

    void logByte(const char direction, uint8_t value);

private:
    WebServer server;
    Preferences preferences;

    String deviceName;
    String version;

    uint32_t (*getUsbRx)();
    uint32_t (*getUsbTx)();
    uint32_t (*getKnxRx)();
    uint32_t (*getKnxTx)();

    bool webStarted;
    bool apMode;

    String apPassword;

    String htmlHeader(const String &title);
    String htmlFooter();

    void startWiFi();
    void startAP();
    void startWebServer();

    void handleRoot();
    void handleWiFi();
    void handleWiFiSave();
    void handleWiFiScan();
    void handleWiFiForget();

    bool connectWiFi(const String &ssid, const String &password);
    String wifiEncryptionName(uint8_t encryption);
    String htmlEscape(const String &value);

    void handleStatus();

    void handleUpdatePage();
    void handleUpdateResult();
    void handleUpdateUpload();

    bool otaAuthorized();

bool otaStarted;
bool otaFailed;
size_t otaReceived;
const esp_partition_t *otaPartition;

    String resetReason();
    String jsonEscape(const String &value);

static const uint16_t SERIAL_LOG_SIZE = 8192;

char serialLog[SERIAL_LOG_SIZE];
uint16_t serialLogHead;
uint16_t serialLogTail;

String serialLineRx;
String serialLineTx;

uint32_t lastRxByte;
uint32_t lastTxByte;

void flushSerialLine(char direction);
void appendSerialLine(char direction, uint8_t value);
void handleSerialPage();
void handleSerialApi();

};
