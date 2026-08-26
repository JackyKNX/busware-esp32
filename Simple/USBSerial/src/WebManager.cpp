#include "WebManager.h"

#include <WiFi.h>
#include <Update.h>
#include <ESPmDNS.h>
#include <esp_system.h>
#include <esp_ota_ops.h>
#include "version.h"

#define WEB_PORT 80

#define AP_PREFIX "TUL-"
#define AP_PASSWORD "tulsetup"

#define WIFI_CONNECT_TIMEOUT_MS 10000

#define OTA_USER "admin"
#define OTA_PASSWORD "tul"

WebManager::WebManager()
    : server(WEB_PORT),
      webStarted(false),
      apMode(false),
      getUsbRx(nullptr),
      getUsbTx(nullptr),
      getKnxRx(nullptr),
      getKnxTx(nullptr),
      otaStarted(false),
      otaFailed(false),
      otaReceived(0),
      otaPartition(nullptr),
      serialLogHead(0),
      serialLogTail(0),
      lastRxByte(0),
      lastTxByte(0)
{
}


void WebManager::begin(
    const char *name,
    const char *fwVersion,
    uint32_t (*usbRx)(),
    uint32_t (*usbTx)(),
    uint32_t (*knxRx)(),
    uint32_t (*knxTx)()
)
{
    deviceName = name;
    version = fwVersion;

    getUsbRx = usbRx;
    getUsbTx = usbTx;
    getKnxRx = knxRx;
    getKnxTx = knxTx;

    preferences.begin("wifi", false);

    startWiFi();
    startWebServer();

    webStarted = true;
}

void WebManager::startWiFi()
{
    String ssid = preferences.getString("ssid", "");
    String password = preferences.getString("password", "");

    /*
     * First boot:
     * if no WiFi configuration is stored yet, provision the
     * initial production WiFi credentials.
     */
    if (ssid.length() == 0)
    {
        startAP();
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.setHostname(deviceName.c_str());
    WiFi.begin(ssid.c_str(), password.c_str());

    Serial.print("WiFi: connecting to ");
    Serial.println(ssid);

    uint32_t start = millis();

    while (WiFi.status() != WL_CONNECTED &&
           millis() - start < WIFI_CONNECT_TIMEOUT_MS)
    {
        delay(100);
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        apMode = false;

        Serial.print("WiFi connected, IP: ");
        Serial.println(WiFi.localIP());

        if (MDNS.begin(deviceName.c_str()))
        {
            Serial.print("mDNS: http://");
            Serial.print(deviceName);
            Serial.println(".local/");
        }
    }
    else
    {
        Serial.println("WiFi: connection failed");
        startAP();
    }
}

void WebManager::startAP()
{
    String chipId = String((uint32_t)(ESP.getEfuseMac() & 0xFFFFFF), HEX);
    chipId.toUpperCase();

    while (chipId.length() < 6)
        chipId = "0" + chipId;

    String apName = String(AP_PREFIX) + chipId;

WiFi.mode(WIFI_AP);
WiFi.softAPsetHostname(deviceName.c_str());
WiFi.softAP(
    apName.c_str(),
    AP_PASSWORD
);
    apMode = true;
    apPassword = AP_PASSWORD;

    Serial.println("WiFi: configuration AP started");
    Serial.print("SSID: ");
    Serial.println(apName);
    Serial.print("Password: ");
    Serial.println(AP_PASSWORD);
    Serial.print("IP: ");
    Serial.println(WiFi.softAPIP());
}

void WebManager::startWebServer()
{
    server.on("/", HTTP_GET, [this]()
    {
        handleRoot();
    });

server.on("/restart", HTTP_POST, [this]()
{
    server.send(200, "text/html",
                "<html><body>"
                "<h1>Restarting...</h1>"
                "<p>BUSWARE TUL is restarting.</p>"
                "</body></html>");

    delay(200);
    ESP.restart();
});
    server.on("/wifi", HTTP_GET, [this]()
    {
        handleWiFi();
    });

    server.on("/wifi/save", HTTP_POST, [this]()
    {
        handleWiFiSave();
    });

server.on("/wifi/scan", HTTP_GET, [this]()
{
    handleWiFiScan();
});

server.on("/wifi/forget", HTTP_POST, [this]()
{
    handleWiFiForget();
});


    server.on("/api/status", HTTP_GET, [this]()
    {
        handleStatus();
    });

    server.on("/update", HTTP_GET, [this]()
    {
        handleUpdatePage();
    });

    server.on(
        "/update",
        HTTP_POST,
        [this]()
        {
            handleUpdateResult();
        },
        [this]()
        {
            handleUpdateUpload();
        }
    );

    server.on("/serial", HTTP_GET, [this]()
    {
        handleSerialPage();
    });

    server.on("/api/serial", HTTP_GET, [this]()
    {
        handleSerialApi();
    });

    server.begin();

    Serial.println("WebManager: HTTP server started");
}

void WebManager::loop()
{
    if (!webStarted)
        return;

    server.handleClient();
}

String WebManager::htmlHeader(const String &title)
{
    String s;

    s += "<!DOCTYPE html>";
    s += "<html><head>";
    s += "<meta charset='utf-8'>";
    s += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    s += "<title>";
    s += title;
    s += "</title>";

    s += "<style>";
    s += "body{font-family:Arial,sans-serif;background:#f3f4f6;";
    s += "margin:0;padding:20px;color:#222;}";
    s += ".box{background:white;max-width:1100px;margin:auto;";
    s += "padding:24px;border-radius:10px;";
    s += "box-shadow:0 2px 8px rgba(0,0,0,.12);}";
    s += "h1{margin-top:0;}";
    s += "h2{margin-top:28px;}";
    s += ".row{padding:8px 0;border-bottom:1px solid #eee;}";
    s += ".label{font-weight:bold;display:inline-block;min-width:150px;}";
    s += "input{width:100%;padding:10px;margin:6px 0 15px;";
    s += "box-sizing:border-box;border:1px solid #bbb;border-radius:5px;}";
    s += "button{padding:10px 18px;border:0;border-radius:5px;";
    s += "cursor:pointer;}";
    s += "a{display:inline-block;margin:8px 12px 8px 0;}";
    s += ".ok{color:#167534;}";
    s += ".warn{color:#9a6700;}";
    s += ".muted{color:#666;font-size:13px;}";

s += ".grid{display:grid;grid-template-columns:1fr 1fr;gap:20px;}";
s += ".card{background:#fff;border:1px solid #e5e7eb;";
s += "border-radius:8px;padding:18px;}";
s += ".card h2{margin-top:0;}";
s += ".wifi-list{margin:15px 0;}";
s += ".wifi-net{display:flex;align-items:center;";
s += "justify-content:space-between;padding:12px;";
s += "border:1px solid #ddd;border-radius:6px;margin:6px 0;";
s += "cursor:pointer;}";
s += ".wifi-net:hover{background:#f5f5f5;}";
s += ".wifi-info{display:flex;flex-direction:column;}";
s += ".wifi-ssid{font-weight:bold;}";
s += ".wifi-meta{font-size:13px;color:#666;}";
s += ".wifi-rssi{font-weight:bold;}";
s += ".danger{background:#b42318;color:white;}";
s += ".secondary{background:#e5e7eb;color:#222;}";
s += "@media(max-width:700px){";
s += ".grid{grid-template-columns:1fr;}";
s += ".box{padding:16px;}";
s += "}";

    s += "</style>";

    s += "</head><body><div class='box'>";

    return s;
}

String WebManager::htmlFooter()
{
    return "</div></body></html>";
}

void WebManager::handleRoot()
{
    String s = htmlHeader("BUSWARE TUL C3");

    s += "<h1>BUSWARE TUL</h1>";

    /*
     * Device information
     */
    s += "<div class='card'>";

s += "<div class='row'><span class='label'>Firmware:</span>";
s += VERSION_SHORT;
s += "</div>";

s += "<div class='row'><span class='label'>Build:</span>";
s += BUILD_NUMBER;
s += "</div>";

s += "<div class='row'><span class='label'>Built:</span>";
s += BUILD_TIME;
s += "</div>";

s += "<div class='row'><span class='label'>Git:</span>";
s += GIT_COMMIT;
s += "</div>";

    s += "<div class='row'><span class='label'>Device:</span>";
    s += deviceName;
    s += "</div>";

    s += "<div class='row'><span class='label'>WiFi:</span>";

    if (apMode)
        s += "<span class='warn'>Configuration AP</span>";
    else
        s += "<span class='ok'>Connected</span>";

    s += "</div>";

    s += "<div class='row'><span class='label'>IP:</span>";

    if (apMode)
        s += WiFi.softAPIP().toString();
    else
        s += WiFi.localIP().toString();

    s += "</div>";

    s += "<div class='row'><span class='label'>MAC:</span>";
    s += WiFi.macAddress();
    s += "</div>";

    s += "<div class='row'><span class='label'>Uptime:</span>";
    s += String(millis() / 1000);
    s += " s</div>";

    s += "</div>";

    /*
     * Two-column main dashboard
     */
    s += "<div class='grid' style='margin-top:20px;'>";

    /*
     * LEFT COLUMN - System health
     */
    s += "<div class='card'>";

    s += "<h2>System health</h2>";

    bool healthOk =
        (!apMode) &&
        (WiFi.status() == WL_CONNECTED) &&
        (ESP.getFreeHeap() > 20000);

    s += "<div class='row'><span class='label'>Status:</span>";

    if (healthOk)
        s += "<span class='ok'>Healthy</span>";
    else
        s += "<span class='warn'>Check</span>";

    s += "</div>";

    s += "<div class='row'><span class='label'>Reset reason:</span>";
    s += resetReason();
    s += "</div>";

    s += "<div class='row'><span class='label'>Free heap:</span>";
    s += String(ESP.getFreeHeap());
    s += " bytes</div>";

    s += "<div class='row'><span class='label'>Min free heap:</span>";
    s += String(ESP.getMinFreeHeap());
    s += " bytes</div>";

    s += "<div class='row'><span class='label'>WiFi RSSI:</span>";

    if (!apMode && WiFi.status() == WL_CONNECTED)
    {
        s += String(WiFi.RSSI());
        s += " dBm";
    }
    else
    {
        s += "n/a";
    }

    s += "</div>";

    s += "<div class='row'><span class='label'>Flash:</span>";
    s += String(ESP.getFlashChipSize() / 1024 / 1024);
    s += " MB</div>";

    s += "<div class='row'><span class='label'>Firmware size:</span>";
    s += String(ESP.getSketchSize() / 1024);
    s += " KB</div>";

    s += "<div class='row'><span class='label'>OTA partition:</span>";
    s += String(ESP.getFreeSketchSpace() / 1024);
    s += " KB</div>";

    s += "<div class='row'><span class='label'>KNX traffic:</span>";

    uint32_t rx = getKnxRx ? getKnxRx() : 0;
    uint32_t tx = getKnxTx ? getKnxTx() : 0;

    if ((rx + tx) > 0)
        s += "<span class='ok'>Active</span>";
    else
        s += "<span class='warn'>No traffic</span>";

    s += "</div>";

    s += "</div>";

    /*
     * RIGHT COLUMN - Bridge statistics
     */
    s += "<div class='card'>";

    s += "<h2>Bridge statistics</h2>";

    s += "<div class='row'><span class='label'>USB RX:</span>";
    s += String(getUsbRx ? getUsbRx() : 0);
    s += "</div>";

    s += "<div class='row'><span class='label'>USB TX:</span>";
    s += String(getUsbTx ? getUsbTx() : 0);
    s += "</div>";

    s += "<div class='row'><span class='label'>KNX RX:</span>";
    s += String(getKnxRx ? getKnxRx() : 0);
    s += "</div>";

    s += "<div class='row'><span class='label'>KNX TX:</span>";
    s += String(getKnxTx ? getKnxTx() : 0);
    s += "</div>";

    s += "<h2>Management</h2>";

    s += "<a href='/wifi'>WiFi configuration</a>";
    s += "<a href='/update'>Firmware update</a>";
    s += "<a href='/serial'>Serial monitor</a>";

    s += "<form method='POST' action='/restart' "
         "onsubmit=\"return confirm('Restart BUSWARE TUL?');\" "
         "style='margin-top:15px;'>";

    s += "<button type='submit'>Restart ESP32</button>";

    s += "</form>";

    s += "</div>";

    s += "</div>";

    /*
     * About
     */
    s += "<div class='card' style='margin-top:20px;'>";

    s += "<h2>About this project</h2>";

    s += "<p>";
    s += "BUSWARE TUL is based on the open-source BUSWARE ESP32 project.";
    s += "</p>";

    s += "<p>";
    s += "<b>Original project:</b><br>";
    s += "<a href='https://github.com/tostmann/busware-esp32' "
         "target='_blank'>tostmann/busware-esp32</a>";
    s += "</p>";

    s += "<p>";
    s += "<b>TUL ESP32-C3 development and enhancements:</b><br>";
    s += "<a href='https://github.com/JackyKNX/busware-esp32' "
         "target='_blank'>JackyKNX/busware-esp32</a>";
    s += "</p>";

    s += "</div>";

    s += htmlFooter();

    server.send(200, "text/html", s);
}

bool WebManager::connectWiFi(
    const String &ssid,
    const String &password)
{
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(deviceName.c_str());

    WiFi.disconnect();
    delay(100);

    WiFi.begin(ssid.c_str(), password.c_str());

    Serial.print("WiFi: testing connection to ");
    Serial.println(ssid);

    uint32_t start = millis();

    while (WiFi.status() != WL_CONNECTED &&
           millis() - start < WIFI_CONNECT_TIMEOUT_MS)
    {
        delay(100);
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.print("WiFi: connection successful, IP: ");
        Serial.println(WiFi.localIP());

        if (MDNS.begin(deviceName.c_str()))
        {
            Serial.print("mDNS: http://");
            Serial.print(deviceName);
            Serial.println(".local/");
        }

        return true;
    }

    Serial.println("WiFi: connection failed");

    return false;
}


void WebManager::handleWiFi()
{
    String currentSsid = preferences.getString("ssid", "");

    String s = htmlHeader("WiFi Configuration");

    s += "<h1>WiFi Configuration</h1>";

    if (apMode)
    {
        s += "<p class='warn'><b>Configuration mode</b></p>";
        s += "<p class='muted'>";
        s += "Connect your computer or phone to the TUL access point "
             "and select a WiFi network below.";
        s += "</p>";
    }
    else if (WiFi.status() == WL_CONNECTED)
    {
        s += "<div class='card'>";
        s += "<h2>Current connection</h2>";

        s += "<div class='row'><span class='label'>SSID:</span>";
        s += htmlEscape(WiFi.SSID());
        s += "</div>";

        s += "<div class='row'><span class='label'>IP:</span>";
        s += WiFi.localIP().toString();
        s += "</div>";

        s += "<div class='row'><span class='label'>RSSI:</span>";
        s += String(WiFi.RSSI());
        s += " dBm</div>";

        s += "</div>";
    }

    s += "<h2>Available networks</h2>";

    s += "<button type='button' class='secondary' "
         "onclick='scanWiFi()'>Scan for networks</button>";

    s += "<div id='wifiList' class='wifi-list'>";
    s += "<p class='muted'>Press Scan for networks.</p>";
    s += "</div>";

    s += "<h2>Connect</h2>";

    s += "<form method='POST' action='/wifi/save'>";

    s += "<label>SSID</label>";
    s += "<input id='ssid' type='text' name='ssid' value='";
    s += htmlEscape(currentSsid);
    s += "' required>";

    s += "<label>Password</label>";
    s += "<input type='password' name='password'>";

    s += "<p class='muted'>";
    s += "Leave password empty to keep the currently stored password.";
    s += "</p>";

    s += "<button type='submit'>Connect & Save</button>";

    s += "</form>";

    if (currentSsid.length() > 0)
    {
        s += "<form method='POST' action='/wifi/forget' "
             "style='margin-top:20px;' "
             "onsubmit=\"return confirm('Forget stored WiFi configuration?');\">";

        s += "<button type='submit' class='danger'>";
        s += "Forget WiFi configuration";
        s += "</button>";

        s += "</form>";
    }

    s += "<p><a href='/'>&larr; Back</a></p>";

    s += R"rawliteral(
<script>
function selectWiFi(ssid)
{
    document.getElementById('ssid').value = ssid;
    window.scrollTo({
        top: document.getElementById('ssid').offsetTop - 20,
        behavior: 'smooth'
    });
}

function scanWiFi()
{
    const list = document.getElementById('wifiList');

    list.innerHTML =
        "<p class='muted'>Scanning for WiFi networks...</p>";

    fetch('/wifi/scan')
        .then(response => response.json())
        .then(networks => {

            if (!networks.length)
            {
                list.innerHTML =
                    "<p class='muted'>No WiFi networks found.</p>";
                return;
            }

            list.innerHTML = "";

            networks.forEach(network => {

                const div = document.createElement('div');
                div.className = 'wifi-net';

                div.onclick = function()
                {
                    selectWiFi(network.ssid);
                };

                const info = document.createElement('div');
                info.className = 'wifi-info';

                const ssid = document.createElement('span');
                ssid.className = 'wifi-ssid';
                ssid.textContent =
                    network.ssid || '(hidden network)';

                const meta = document.createElement('span');
                meta.className = 'wifi-meta';
                meta.textContent =
                    network.encryption;

                info.appendChild(ssid);
                info.appendChild(meta);

                const rssi = document.createElement('span');
                rssi.className = 'wifi-rssi';
                rssi.textContent =
                    network.rssi + " dBm";

                div.appendChild(info);
                div.appendChild(rssi);

                list.appendChild(div);
            });
        })
        .catch(error => {
            list.innerHTML =
                "<p class='warn'>WiFi scan failed.</p>";
        });
}
</script>
)rawliteral";

    s += htmlFooter();

    server.send(200, "text/html", s);
}


String WebManager::wifiEncryptionName(uint8_t encryption)
{
    switch (encryption)
    {
        case WIFI_AUTH_OPEN:
            return "Open";

        case WIFI_AUTH_WEP:
            return "WEP";

        case WIFI_AUTH_WPA_PSK:
            return "WPA";

        case WIFI_AUTH_WPA2_PSK:
            return "WPA2";

        case WIFI_AUTH_WPA_WPA2_PSK:
            return "WPA/WPA2";

        case WIFI_AUTH_WPA2_ENTERPRISE:
            return "WPA2 Enterprise";

        case WIFI_AUTH_WPA3_PSK:
            return "WPA3";

        case WIFI_AUTH_WPA2_WPA3_PSK:
            return "WPA2/WPA3";

        default:
            return "Secured";
    }
}


void WebManager::handleWiFiScan()
{
    Serial.println("WiFi: scanning networks...");

if (apMode)
    WiFi.mode(WIFI_AP_STA);
else
    WiFi.mode(WIFI_STA);

    int count = WiFi.scanNetworks(false, true);

    String json = "[";

    for (int i = 0; i < count; i++)
    {
        if (i > 0)
            json += ",";

        json += "{";

        json += "\"ssid\":\"";
        json += jsonEscape(WiFi.SSID(i));
        json += "\",";

        json += "\"rssi\":";
        json += String(WiFi.RSSI(i));
        json += ",";

        json += "\"encryption\":\"";
        json += jsonEscape(
            wifiEncryptionName(
                (uint8_t)WiFi.encryptionType(i)
            )
        );
        json += "\"";

        json += "}";
    }

    json += "]";

    WiFi.scanDelete();

    server.send(
        200,
        "application/json",
        json
    );
}

void WebManager::handleWiFiSave()
{
    if (!server.hasArg("ssid"))
    {
        server.send(
            400,
            "text/plain",
            "Missing SSID"
        );

        return;
    }

    String ssid = server.arg("ssid");
    String password = server.arg("password");

    ssid.trim();

    if (ssid.length() == 0)
    {
        server.send(
            400,
            "text/plain",
            "SSID cannot be empty"
        );

        return;
    }

    /*
     * If password is empty, use the currently stored password.
     */
    if (password.length() == 0)
    {
        password = preferences.getString(
            "password",
            ""
        );
    }

    String oldSsid =
        preferences.getString("ssid", "");

    String oldPassword =
        preferences.getString("password", "");

    /*
     * Test the new connection BEFORE saving it.
     */
    bool connected =
        connectWiFi(ssid, password);

    if (!connected)
    {
        /*
         * Restore previous connection if possible.
         */
        if (oldSsid.length() > 0)
        {
            Serial.println(
                "WiFi: restoring previous configuration"
            );

            connectWiFi(
                oldSsid,
                oldPassword
            );
        }
        else
        {
            startAP();
        }

        String s =
            htmlHeader("WiFi");

        s += "<h1>Connection failed</h1>";

        s += "<p class='warn'>";
        s += "Could not connect to the selected WiFi network.";
        s += "</p>";

        s += "<p>";
        s += "The previous WiFi configuration was not changed.";
        s += "</p>";

        s += "<p><a href='/wifi'>";
        s += "Try again";
        s += "</a></p>";

        s += htmlFooter();

        server.send(
            400,
            "text/html",
            s
        );

        return;
    }

    /*
     * Connection successful.
     * Now store the credentials.
     */
    preferences.putString(
        "ssid",
        ssid
    );

    preferences.putString(
        "password",
        password
    );

    String s =
        htmlHeader("WiFi");

    s += "<h1>WiFi connected</h1>";

    s += "<p class='ok'>";
    s += "Connection successful.";
    s += "</p>";

    s += "<p>";
    s += "WiFi configuration has been saved.";
    s += "</p>";

    s += "<p>";
    s += "Device will restart.";
    s += "</p>";

    s += htmlFooter();

    server.send(
        200,
        "text/html",
        s
    );

    delay(800);

    ESP.restart();
}


void WebManager::handleWiFiForget()
{
    preferences.remove("ssid");
    preferences.remove("password");

    Serial.println(
        "WiFi: stored configuration removed"
    );

    String s =
        htmlHeader("WiFi");

    s += "<h1>WiFi configuration removed</h1>";

    s += "<p>";
    s += "Stored WiFi credentials have been deleted.";
    s += "</p>";

    s += "<p>";
    s += "Device will restart in configuration mode.";
    s += "</p>";

    s += htmlFooter();

    server.send(
        200,
        "text/html",
        s
    );

    delay(800);

    ESP.restart();
}


String WebManager::resetReason()
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


String WebManager::htmlEscape(const String &value)
{
    String result;

    for (size_t i = 0; i < value.length(); i++)
    {
        char c = value[i];

        switch (c)
        {
            case '&':
                result += "&amp;";
                break;

            case '<':
                result += "&lt;";
                break;

            case '>':
                result += "&gt;";
                break;

            case '"':
                result += "&quot;";
                break;

            case '\'':
                result += "&#39;";
                break;

            default:
                result += c;
                break;
        }
    }

    return result;
}

String WebManager::jsonEscape(const String &value)
{
    String result;

    for (size_t i = 0; i < value.length(); i++)
    {
        char c = value[i];

        if (c == '"')
            result += "\\\"";
        else if (c == '\\')
            result += "\\\\";
        else
            result += c;
    }

    return result;
}

void WebManager::handleStatus()
{
    String ip;

    bool healthOk =
        (!apMode) &&
        (WiFi.status() == WL_CONNECTED) &&
        (ESP.getFreeHeap() > 20000);

    if (apMode)
        ip = WiFi.softAPIP().toString();
    else
        ip = WiFi.localIP().toString();

    String json = "{";

    json += "\"device\":\"";
    json += jsonEscape(deviceName);
    json += "\",";

    json += "\"version\":\"";
    json += jsonEscape(version);
    json += "\",";

    json += "\"ip\":\"";
    json += jsonEscape(ip);
    json += "\",";

    json += "\"mac\":\"";
    json += WiFi.macAddress();
    json += "\",";

    json += "\"ap\":";
    json += apMode ? "true" : "false";
    json += ",";

    json += "\"uptime\":";
    json += String(millis() / 1000);
    json += ",";

    json += "\"usbRx\":";
    json += String(getUsbRx ? getUsbRx() : 0);
    json += ",";

    json += "\"usbTx\":";
    json += String(getUsbTx ? getUsbTx() : 0);
    json += ",";

    json += "\"knxRx\":";
    json += String(getKnxRx ? getKnxRx() : 0);
    json += ",";

    json += "\"knxTx\":";
    json += String(getKnxTx ? getKnxTx() : 0);

json += ",\"health\":";
json += healthOk ? "\"Healthy\"" : "\"Check\"";

json += ",\"resetReason\":\"";
json += jsonEscape(resetReason());
json += "\"";

json += ",\"freeHeap\":";
json += String(ESP.getFreeHeap());

json += ",\"minFreeHeap\":";
json += String(ESP.getMinFreeHeap());

json += ",\"rssi\":";
if (!apMode && WiFi.status() == WL_CONNECTED)
    json += String(WiFi.RSSI());
else
    json += "null";

json += ",\"flashSize\":";
json += String(ESP.getFlashChipSize());

json += ",\"firmwareSize\":";
json += String(ESP.getSketchSize());

json += ",\"otaPartitionSize\":";
json += String(ESP.getFreeSketchSpace());

    json += "}";

    server.send(200, "application/json", json);
}

bool WebManager::otaAuthorized()
{
    if (!server.authenticate(OTA_USER, OTA_PASSWORD))
    {
        server.requestAuthentication();
        return false;
    }

    return true;
}

void WebManager::handleUpdatePage()
{
    if (!otaAuthorized())
        return;

    String s = htmlHeader("Firmware Update");

    s += "<h1>Firmware Update</h1>";

    s += "<p>Current firmware:</p>";
    s += "<p><b>";
    s += version;
    s += "</b></p>";

s += "<p class='muted'>";
s += "Target: ESP32-C3<br>";
s += "Available OTA space: ";
s += String(ESP.getFreeSketchSpace() / 1024);
s += " KB";
s += "</p>";

    s += "<form method='POST' action='/update' ";
    s += "enctype='multipart/form-data'>";

    s += "<input type='file' name='firmware' accept='.bin' required>";

    s += "<br>";
    s += "<button type='submit'>Upload Firmware</button>";

    s += "</form>";

    s += "<p class='muted'>";
    s += "Use the OTA firmware binary generated by PlatformIO.";
    s += "</p>";

    s += "<p><a href='/'>&larr; Back</a></p>";

    s += htmlFooter();

    server.send(200, "text/html", s);
}





void WebManager::handleUpdateUpload()
{
    if (!otaAuthorized())
        return;

    HTTPUpload &upload = server.upload();


    if (upload.status == UPLOAD_FILE_START)
    {
        otaStarted = false;
        otaFailed = false;
        otaReceived = 0;

        Serial.println();
        Serial.println("================================");
        Serial.print("OTA: ");
        Serial.println(upload.filename);

        Serial.print("OTA: expected size = ");
        Serial.print(upload.totalSize);
        Serial.println(" bytes");

        /*
         * Validate filename.
         */
        String filename = upload.filename;
        filename.toLowerCase();

        if (!filename.endsWith(".bin"))
        {
            otaFailed = true;

            Serial.println(
                "OTA ERROR: file is not a .bin image"
            );

            return;
        }

        /*
         * Start OTA update.
         *
         * ESP32 Arduino Update library will select
         * the next OTA application partition.
         */

otaPartition = esp_ota_get_next_update_partition(NULL);

if (otaPartition == nullptr)
{
    otaFailed = true;

    Serial.println(
        "OTA ERROR: no OTA target partition"
    );

    return;
}

Serial.print("OTA: target partition: ");
Serial.println(otaPartition->label);


        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH))
        {
            otaFailed = true;

            Serial.print("OTA ERROR: Update.begin failed: ");
            Update.printError(Serial);

            return;
        }

        otaStarted = true;

        Serial.println(
            "OTA: update started"
        );
    }

    else if (upload.status == UPLOAD_FILE_WRITE)
    {
        if (!otaStarted || otaFailed)
            return;

        size_t written =
            Update.write(
                upload.buf,
                upload.currentSize
            );

        if (written != upload.currentSize)
        {
            otaFailed = true;

            Serial.print(
                "OTA ERROR: write failed: "
            );

            Update.printError(Serial);

            Update.abort();

            return;
        }

        otaReceived += written;

        /*
         * Progress information.
         */
        Serial.print("OTA: received ");
        Serial.print(otaReceived);
        Serial.println(" bytes");
    }

    else if (upload.status == UPLOAD_FILE_END)
    {
        if (!otaStarted || otaFailed)
        {
            Serial.println(
                "OTA ERROR: upload finished without active update"
            );

            return;
        }

        /*
         * Finalize and verify the firmware.
         */

if (Update.end(true))
{
    /*
     * Update.end(true) has verified the image.
     *
     * Explicitly select the partition written by
     * this OTA operation as the next boot partition.
     */

    if (otaPartition == nullptr)
    {
        otaFailed = true;

        Serial.println(
            "OTA ERROR: OTA partition reference lost"
        );

        return;
    }

    Serial.print("OTA: written partition: ");
    Serial.println(otaPartition->label);

    esp_err_t result =
        esp_ota_set_boot_partition(otaPartition);

    if (result != ESP_OK)
    {
        otaFailed = true;

        Serial.print(
            "OTA ERROR: esp_ota_set_boot_partition failed: "
        );
        Serial.println(result);

        return;
    }

    const esp_partition_t *boot =
        esp_ota_get_boot_partition();

    Serial.print("OTA: boot partition: ");
    Serial.println(
        boot ? boot->label : "NONE"
    );

    Serial.print("OTA: update verified, ");
    Serial.print(otaReceived);
    Serial.println(" bytes");

    Serial.println(
        "OTA: validation successful"
    );
}
else
{
    otaFailed = true;

    Serial.print(
        "OTA ERROR: final validation failed: "
    );

    Update.printError(Serial);
    Update.abort();
}
    }

    else if (upload.status == UPLOAD_FILE_ABORTED)
    {
        otaFailed = true;

        if (otaStarted)
            Update.abort();

        Serial.println(
            "OTA: upload aborted"
        );

        Serial.println(
            "================================"
        );
    }
}


void WebManager::handleUpdateResult()
{
    if (!otaAuthorized())
        return;

    String s = htmlHeader("Firmware Update");

    if (otaFailed || !otaStarted || Update.hasError())
    {
        s += "<h1>Firmware update failed</h1>";

        s += "<p class='warn'>";
        s += "Firmware update was not successful.";
        s += "</p>";

        s += "<p>";
        s += "The current firmware has NOT been replaced.";
        s += "</p>";

        s += "<p>";
        s += "<a href='/update'>Try again</a>";
        s += "</p>";
    }
    else
    {
        s += "<h1>Firmware update successful</h1>";

        s += "<p class='ok'>";
        s += "Firmware has been uploaded and verified successfully.";
        s += "</p>";

        s += "<p>";
        s += "Received: ";
        s += String(otaReceived);
        s += " bytes";
        s += "</p>";

        s += "<p>";
        s += "BUSWARE TUL will restart now.";
        s += "</p>";
    }

    s += htmlFooter();

    server.send(200, "text/html", s);

    if (!otaFailed && otaStarted && !Update.hasError())
    {
        delay(1000);
        ESP.restart();
    }
}


void WebManager::logByte(const char direction, uint8_t value)
{
    String &line = (direction == 'R') ? serialLineRx : serialLineTx;
    uint32_t &last = (direction == 'R') ? lastRxByte : lastTxByte;

    uint32_t now = millis();

    /*
     * New frame after 5 ms idle time.
     */
    if (line.length() > 0 && now - last > 5)
        flushSerialLine(direction);

    if (line.length() == 0)
    {
        if (direction == 'R')
            line = "RX ";
        else
            line = "TX ";
    }

    char hex[4];
    snprintf(hex, sizeof(hex), "%02X ", value);
    line += hex;

    last = now;
}

void WebManager::flushSerialLine(char direction)
{
    String &line = (direction == 'R') ? serialLineRx : serialLineTx;

    if (line.length() == 0)
        return;

    uint16_t len = line.length();

    for (uint16_t i = 0; i < len; i++)
    {
        serialLog[serialLogHead] = line[i];
        serialLogHead = (serialLogHead + 1) % SERIAL_LOG_SIZE;

        if (serialLogHead == serialLogTail)
            serialLogTail = (serialLogTail + 1) % SERIAL_LOG_SIZE;
    }

    serialLog[serialLogHead] = '\n';
    serialLogHead = (serialLogHead + 1) % SERIAL_LOG_SIZE;

    if (serialLogHead == serialLogTail)
        serialLogTail = (serialLogTail + 1) % SERIAL_LOG_SIZE;

    line = "";
}

void WebManager::handleSerialApi()
{
    flushSerialLine('R');
    flushSerialLine('T');

    String json = "[";

    uint16_t pos = serialLogTail;
    bool first = true;

    while (pos != serialLogHead)
    {
        String line;

        while (pos != serialLogHead)
        {
            char c = serialLog[pos];
            pos = (pos + 1) % SERIAL_LOG_SIZE;

            if (c == '\n')
                break;

            line += c;
        }

        if (line.length() == 0)
            continue;

        if (!first)
            json += ",";

        first = false;

        json += "\"";
        json += jsonEscape(line);
        json += "\"";
    }

    /*
     * Everything currently buffered has been delivered.
     */
    serialLogTail = serialLogHead;

    json += "]";

    server.send(200, "application/json", json);
}



void WebManager::handleSerialPage()
{
    String s = htmlHeader("Serial Monitor");

    s += "<h1>BUSWARE TUL - Serial Monitor</h1>";

    s += "<div style='margin-bottom:12px'>";
    s += "<button onclick='clearLog()'>Clear</button> ";
    s += "<label>";
    s += "<input type='checkbox' id='scroll' checked "
         "style='width:auto;margin:0 5px 0 0'>";
    s += "Auto scroll";
    s += "</label>";
    s += "</div>";

    s += "<pre id='log' style='";
    s += "background:#111;color:#ddd;";
    s += "padding:12px;";
    s += "height:500px;";
    s += "overflow:auto;";
    s += "font-family:monospace;";
    s += "font-size:13px;";
    s += "border-radius:5px;";
    s += "'></pre>";

    s += "<p><a href='/'>&larr; Back</a></p>";

    s += "<script>";

    s += "async function poll(){";
    s += "try{";
    s += "const r=await fetch('/api/serial');";
    s += "const a=await r.json();";
    s += "const l=document.getElementById('log');";

    s += "a.forEach(x=>{";
    s += "l.textContent+=new Date().toLocaleTimeString()+' '+x+'\\n';";
    s += "});";

    s += "if(document.getElementById('scroll').checked)";
    s += "l.scrollTop=l.scrollHeight;";

    s += "}catch(e){}";

    s += "setTimeout(poll,250);";
    s += "}";

    s += "function clearLog(){";
    s += "document.getElementById('log').textContent='';";
    s += "}";

    s += "poll();";

    s += "</script>";

    s += htmlFooter();

    server.send(200, "text/html", s);
}

