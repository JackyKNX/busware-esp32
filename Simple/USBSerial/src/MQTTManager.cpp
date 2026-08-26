#include "MQTTManager.h"

#include "esp_system.h"

MQTTManager::MQTTManager()
    : mqttClient(wifiClient)
{
    enabled = false;
    port = 1883;

    lastConnectAttempt = 0;
    lastStatusPublish = 0;

lastStatusPublishOk = false;
lastMqttState = MQTT_DISCONNECTED;
lastError = "";

    getUsbRx = nullptr;
    getUsbTx = nullptr;
    getKnxRx = nullptr;
    getKnxTx = nullptr;
}

void MQTTManager::begin(
    const char *deviceName,
    const char *firmwareVersion,
    uint32_t (*usbRx)(),
    uint32_t (*usbTx)(),
    uint32_t (*knxRx)(),
    uint32_t (*knxTx)()
)
{
    this->deviceName = deviceName;
    this->firmwareVersion = firmwareVersion;

    getUsbRx = usbRx;
    getUsbTx = usbTx;
    getKnxRx = knxRx;
    getKnxTx = knxTx;

    loadConfig();

    mqttClient.setServer(host.c_str(), port);

if (!mqttClient.setBufferSize(1024))
{
    Serial.println("MQTT ERROR: cannot allocate MQTT buffer");
}
else
{
    Serial.println("MQTT: packet buffer = 1024 bytes");
}


    Serial.println("MQTT: manager initialized");

    if (!enabled)
    {
        Serial.println("MQTT: disabled");
        return;
    }

    Serial.print("MQTT: broker ");
    Serial.print(host);
    Serial.print(":");
    Serial.println(port);
}

void MQTTManager::loadConfig()
{
    preferences.begin("mqtt", false);

    enabled = preferences.getBool("enabled", false);
    host = preferences.getString("host", "");
    port = preferences.getUShort("port", 1883);
    username = preferences.getString("user", "");
    password = preferences.getString("pass", "");
    baseTopic = preferences.getString(
        "topic",
        "busware/TUL"
    );

    if (baseTopic.length() == 0)
        baseTopic = "busware/TUL";
}

void MQTTManager::saveConfig(
    bool enabled,
    const String &host,
    uint16_t port,
    const String &username,
    const String &password,
    const String &baseTopic
)
{
this->enabled = enabled;
this->host = host;
this->port = port;
this->username = username;

if (password.length() > 0)
    this->password = password;

this->baseTopic = baseTopic;

    if (this->baseTopic.length() == 0)
        this->baseTopic = "busware/TUL";

    preferences.putBool("enabled", this->enabled);
    preferences.putString("host", this->host);
    preferences.putUShort("port", this->port);
    preferences.putString("user", this->username);
    preferences.putString("pass", this->password);
    preferences.putString("topic", this->baseTopic);

    mqttClient.disconnect();
}

bool MQTTManager::isEnabled()
{
    return enabled;
}

bool MQTTManager::isConnected()
{
    return enabled && mqttClient.connected();
}


int MQTTManager::getState()
{
    return mqttClient.state();
}

bool MQTTManager::getLastStatusPublishOk()
{
    return lastStatusPublishOk;
}

String MQTTManager::getLastError()
{
    return lastError;
}

String MQTTManager::getHost()
{
    return host;
}

uint16_t MQTTManager::getPort()
{
    return port;
}

String MQTTManager::getUsername()
{
    return username;
}

String MQTTManager::getBaseTopic()
{
    return baseTopic;
}

String MQTTManager::topic(const char *suffix)
{
    String result = baseTopic;

    if (!result.endsWith("/"))
        result += "/";

    result += suffix;

    return result;
}

bool MQTTManager::connect()
{
    if (!enabled)
        return false;

    if (WiFi.status() != WL_CONNECTED)
        return false;

    if (host.length() == 0)
        return false;

    String clientId = deviceName;

    clientId += "-";
    clientId += String(
        (uint32_t)(ESP.getEfuseMac() & 0xFFFFFF),
        HEX
    );

    clientId.toUpperCase();

    String availabilityTopic = topic("availability");

    Serial.print("MQTT: connecting to ");
    Serial.print(host);
    Serial.print(":");
    Serial.println(port);

    mqttClient.setServer(host.c_str(), port);

    bool result;

    if (username.length() > 0)
    {
        result = mqttClient.connect(
            clientId.c_str(),
            username.c_str(),
            password.c_str(),
            availabilityTopic.c_str(),
            0,
            true,
            "offline"
        );
    }
    else
    {
        result = mqttClient.connect(
            clientId.c_str(),
            availabilityTopic.c_str(),
            0,
            true,
            "offline"
        );
    }

    if (!result)
    {
        Serial.print("MQTT: connection failed, state=");
        Serial.println(mqttClient.state());

        return false;
    }

Serial.println("MQTT: connected");

lastMqttState = mqttClient.state();
lastError = "";

publishAvailability("online");

publishEvent(
    "mqtt",
    "connected"
);

publishStatus();

lastStatusPublish = millis();

return true;

}

void MQTTManager::loop()
{
    if (!enabled)
        return;

    if (WiFi.status() != WL_CONNECTED)
        return;

    if (!mqttClient.connected())
    {
        uint32_t now = millis();

        if (now - lastConnectAttempt >= CONNECT_INTERVAL_MS)
        {
            lastConnectAttempt = now;
            connect();
        }

        return;
    }

mqttClient.loop();

uint32_t now = millis();

if (now - lastStatusPublish >= STATUS_INTERVAL_MS)
{
    bool ok = publishStatus();

    lastStatusPublish = now;

    if (!ok)
    {
        Serial.println("MQTT: periodic status publish failed");
    }
}
}

void MQTTManager::publishAvailability(const char *state)
{
    if (!mqttClient.connected())
        return;

    String t = topic("availability");

    mqttClient.publish(
        t.c_str(),
        state,
        true
    );
}

String MQTTManager::jsonEscape(const String &value)
{
    String result;

    for (size_t i = 0; i < value.length(); i++)
    {
        char c = value[i];

        if (c == '"')
            result += "\\\"";
        else if (c == '\\')
            result += "\\\\";
        else if (c == '\n')
            result += "\\n";
        else if (c == '\r')
            result += "\\r";
        else
            result += c;
    }

    return result;
}

String MQTTManager::resetReason()
{
    switch (esp_reset_reason())
    {
        case ESP_RST_UNKNOWN:
            return "Unknown";

        case ESP_RST_POWERON:
            return "Power-on";

        case ESP_RST_EXT:
            return "External reset";

        case ESP_RST_SW:
            return "Software reset";

        case ESP_RST_PANIC:
            return "Panic";

        case ESP_RST_INT_WDT:
            return "Interrupt watchdog";

        case ESP_RST_TASK_WDT:
            return "Task watchdog";

        case ESP_RST_WDT:
            return "Watchdog";

        case ESP_RST_DEEPSLEEP:
            return "Deep sleep";

        case ESP_RST_BROWNOUT:
            return "Brownout";

        case ESP_RST_SDIO:
            return "SDIO reset";

        default:
            return "Other";
    }
}


bool MQTTManager::publishStatus()
{
    if (!mqttClient.connected())
    {
        lastStatusPublishOk = false;
        lastError = "MQTT not connected";
        return false;
    }

    String json;

    json.reserve(700);

    json += "{";

    json += "\"firmware\":\"";
    json += jsonEscape(firmwareVersion);
    json += "\",";

    json += "\"uptime\":";
    json += String(millis() / 1000);
    json += ",";

    json += "\"ip\":\"";
    json += WiFi.localIP().toString();
    json += "\",";

    json += "\"ssid\":\"";
    json += jsonEscape(WiFi.SSID());
    json += "\",";

    json += "\"rssi\":";
    json += String(WiFi.RSSI());
    json += ",";

    json += "\"knx_rx\":";
    json += String(getKnxRx ? getKnxRx() : 0);
    json += ",";

    json += "\"knx_tx\":";
    json += String(getKnxTx ? getKnxTx() : 0);
    json += ",";

    json += "\"usb_rx\":";
    json += String(getUsbRx ? getUsbRx() : 0);
    json += ",";

    json += "\"usb_tx\":";
    json += String(getUsbTx ? getUsbTx() : 0);
    json += ",";

    json += "\"free_heap\":";
    json += String(ESP.getFreeHeap());
    json += ",";

    json += "\"min_free_heap\":";
    json += String(ESP.getMinFreeHeap());
    json += ",";

    json += "\"reset_reason\":\"";
    json += jsonEscape(resetReason());
    json += "\"";

    json += "}";

    String t = topic("status");

    bool ok = mqttClient.publish(
        t.c_str(),
        json.c_str(),
        true
    );

    lastStatusPublishOk = ok;
    lastMqttState = mqttClient.state();

    if (ok)
    {
        lastError = "";

        Serial.print("MQTT: status published, bytes=");
        Serial.println(json.length());
    }
    else
    {
        lastError = "MQTT status publish failed";

        Serial.print("MQTT ERROR: status publish failed, bytes=");
        Serial.print(json.length());
        Serial.print(", buffer=1024, state=");
        Serial.println(mqttClient.state());
    }

    return ok;
}

void MQTTManager::publishEvent(
    const char *type,
    const char *event
)
{
    if (!mqttClient.connected())
        return;

    String json;

    json += "{";
    json += "\"type\":\"";
    json += jsonEscape(type);
    json += "\",";
    json += "\"event\":\"";
    json += jsonEscape(event);
    json += "\",";
    json += "\"uptime\":";
    json += String(millis() / 1000);
    json += "}";

    String t = topic("event");

    mqttClient.publish(
        t.c_str(),
        json.c_str(),
        false
    );
}

void MQTTManager::publishError(
    const char *code,
    const char *message
)
{
    if (!mqttClient.connected())
        return;

    String json;

    json += "{";
    json += "\"type\":\"error\",";
    json += "\"code\":\"";
    json += jsonEscape(code);
    json += "\",";
    json += "\"message\":\"";
    json += jsonEscape(message);
    json += "\",";
    json += "\"uptime\":";
    json += String(millis() / 1000);
    json += "}";

    String t = topic("error");

    mqttClient.publish(
        t.c_str(),
        json.c_str(),
        true
    );
}