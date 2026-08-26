#include "WebManager.h"

#include <WiFi.h>
#include <Update.h>
#include <ESPmDNS.h>

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
    WiFi.softAP(apName.c_str(), AP_PASSWORD);

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
    s += ".box{background:white;max-width:700px;margin:auto;";
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
    String s = htmlHeader("BUSWARE TUL");

    s += "<h1>BUSWARE TUL</h1>";

    s += "<div class='row'><span class='label'>Firmware:</span>";
    s += version;
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
        "style='margin-top:20px;'>";

    s += "<button type='submit'>Restart ESP32</button>";

    s += "</form>";

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

    s += htmlFooter();

    server.send(200, "text/html", s);
}

void WebManager::handleWiFi()
{
    String currentSsid = preferences.getString("ssid", "");

    String s = htmlHeader("WiFi Configuration");

    s += "<h1>WiFi Configuration</h1>";

    if (apMode)
    {
        s += "<p class='warn'>Device is currently running as an access point.</p>";
        s += "<p class='muted'>";
        s += "After saving, the device will restart and try to connect.";
        s += "</p>";
    }

    s += "<form method='POST' action='/wifi/save'>";

    s += "<label>SSID</label>";
    s += "<input type='text' name='ssid' value='";
    s += currentSsid;
    s += "' required>";

    s += "<label>Password</label>";
    s += "<input type='password' name='password'>";

    s += "<p class='muted'>";
    s += "Leave password empty to keep the currently stored password.";
    s += "</p>";

    s += "<button type='submit'>Save & Restart</button>";

    s += "</form>";

    s += "<p><a href='/'>&larr; Back</a></p>";

    s += htmlFooter();

    server.send(200, "text/html", s);
}

void WebManager::handleWiFiSave()
{
    if (!server.hasArg("ssid"))
    {
        server.send(400, "text/plain", "Missing SSID");
        return;
    }

    String ssid = server.arg("ssid");
    String password = server.arg("password");

    if (ssid.length() == 0)
    {
        server.send(400, "text/plain", "SSID cannot be empty");
        return;
    }

    preferences.putString("ssid", ssid);

    if (password.length() > 0)
        preferences.putString("password", password);

    String s = htmlHeader("WiFi");

    s += "<h1>WiFi saved</h1>";
    s += "<p>Device will restart and connect to the configured network.</p>";

    s += htmlFooter();

    server.send(200, "text/html", s);

    delay(500);
    ESP.restart();
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
        Serial.print("OTA: ");
        Serial.print(upload.filename);
        Serial.println();

        if (!Update.begin(UPDATE_SIZE_UNKNOWN))
        {
            Update.printError(Serial);
        }
    }
    else if (upload.status == UPLOAD_FILE_WRITE)
    {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
        {
            Update.printError(Serial);
        }
    }
    else if (upload.status == UPLOAD_FILE_END)
    {
        if (Update.end(true))
        {
            Serial.print("OTA: upload complete, ");
            Serial.print(upload.totalSize);
            Serial.println(" bytes");
        }
        else
        {
            Update.printError(Serial);
        }
    }
}

void WebManager::handleUpdateResult()
{
    if (!otaAuthorized())
        return;

    if (!Update.isFinished())
    {
        server.send(
            500,
            "text/plain",
            "OTA update failed"
        );

        return;
    }

    server.send(
        200,
        "text/html",
        "<html><body>"
        "<h1>Update successful</h1>"
        "<p>Device is restarting...</p>"
        "</body></html>"
    );

    delay(500);

    ESP.restart();
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

