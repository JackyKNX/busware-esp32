#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <Preferences.h>

class WebManager
{
public:
    WebManager();

    void begin(
        const char *deviceName,
        const char *version,
        uint32_t (*getUsbRx)(),
        uint32_t (*getUsbTx)(),
        uint32_t (*getKnxRx)(),
        uint32_t (*getKnxTx)()
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

    void handleStatus();

    void handleUpdatePage();
    void handleUpdateResult();
    void handleUpdateUpload();

    bool otaAuthorized();

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
