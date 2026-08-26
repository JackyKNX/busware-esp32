/*
 * Busware TUL - ESP32-C3
 *
 * USB CDC <-> NCN5130 TPUART transparent bridge
 *
 * TPUART:
 *   UART: 38400 8E1
 *
 * USB:
 *   USB Serial/JTAG CDC
 *
 * Important:
 *   Do NOT use readBytes(buffer, available()).
 *   available() is only a snapshot of the currently buffered data.
 *   readBytes() may wait for the requested number of bytes.
 *
 *   The bridge therefore transfers data incrementally and never
 *   waits for an entire available() block.
 */

#include "version.h"
#include "busware.h"
#include <WiFi.h>
#include "WebManager.h"
#include "MQTTManager.h"

#ifdef USE_IMPROV
#include <ImprovWiFiLibrary.h>
ImprovWiFi improvSerial(&Serial);
#endif


#define MYNAME "TUL"

#define USB_BAUD 115200

#define STATUS_INTERVAL_MS 10000

#define LED_ACTIVITY_MS 500


#if defined(BUSWARE_TUL)

TPUARTTransceiver Transceiver(&Serial0);

#else

#error "This firmware is intended for BUSWARE_TUL"

#endif


// Statistics
uint32_t usbRx = 0;
uint32_t usbTx = 0;
uint32_t knxRx = 0;
uint32_t knxTx = 0;


WebManager webManager;

MQTTManager mqttManager;

void handleMQTT();
void handleMQTTSave();

uint32_t getUsbRx()
{
    return usbRx;
}

uint32_t getUsbTx()
{
    return usbTx;
}

uint32_t getKnxRx()
{
    return knxRx;
}

uint32_t getKnxTx()
{
    return knxTx;
}

// Timing
uint32_t lastStatus = 0;
uint32_t lastActivity = 0;


// LED state
bool ledActive = false;


/*
 * --------------------------------------------------------------------------
 * setup()
 * --------------------------------------------------------------------------
 */

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);

    /*
     * ESP32-C3 USB Serial/JTAG CDC.
     *
     * Baud rate has no physical significance for USB CDC,
     * but 115200 is convenient for terminal/debug tools.
     */
    Serial.begin(USB_BAUD);

    /*
     * Give USB CDC a moment to become available after boot.
     */
    delay(500);


#ifdef USE_IMPROV

    String UniqueName = String(MYNAME) + "-" + getBase32ID();

    WiFi.config(
        INADDR_NONE,
        INADDR_NONE,
        INADDR_NONE,
        INADDR_NONE
    );

    WiFi.setHostname(UniqueName.c_str());

    improvSerial.setDeviceInfo(
#if defined(CONFIG_IDF_TARGET_ESP32C3)
        ImprovTypes::ChipFamily::CF_ESP32_C3,
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
        ImprovTypes::ChipFamily::CF_ESP32_S2,
#endif
        WiFi.getHostname(),
        VERSION_SHORT,
        MYNAME
    );

#endif


    /*
     * Initialize NCN5130 / TPUART.
     *
     * TPUARTTransceiver::begin() configures:
     *
     *   38400 baud
     *   8 data bits
     *   even parity
     *   1 stop bit
     */
    Transceiver.begin();

webManager.begin(
    MYNAME,
    VERSION,
    getUsbRx,
    getUsbTx,
    getKnxRx,
    getKnxTx,
    &mqttManager
);

mqttManager.begin(
    MYNAME,
    VERSION,
    getUsbRx,
    getUsbTx,
    getKnxRx,
    getKnxTx
);

    /*
     * Watchdog.
     *
     * This is NOT intended to fix the bridge logic.
     * It is the last line of defence against a genuine firmware
     * lock-up. If the task ever stops returning to loop(), ESP32
     * will restart instead of requiring physical power removal.
     */


    lastStatus = millis();
    lastActivity = millis();


#ifdef USE_IMPROV

    Serial.print(WiFi.getHostname());

#else

    Serial.print(MYNAME);

#endif

    Serial.print(" - init succeed - running: ");
    Serial.print(VERSION);
    Serial.print(" @ ");
    Serial.print(getCpuFrequencyMhz());
    Serial.println(" MHz");

    Serial.println("TPUART: 38400 8E1");
    Serial.println("USB: CDC");
}


/*
 * --------------------------------------------------------------------------
 * loop()
 * --------------------------------------------------------------------------
 */

void loop()
{
    webManager.loop();

    mqttManager.loop();

    /*
     * Always feed the watchdog at the beginning of the loop.
     */

    bool activity = false;


    /*
     * ======================================================================
     * USB -> TPUART
     * ======================================================================
     *
     * IMPORTANT:
     *
     * Do NOT do:
     *
     *     av = Serial.available();
     *     Serial.readBytes(buffer, av);
     *
     * available() is only a snapshot. Between available() and readBytes()
     * the stream can change. readBytes() is allowed to wait for the
     * requested amount of data.
     *
     * Instead, consume bytes that are actually available now.
     */

    while (Serial.available() > 0)
    {
        int c = Serial.read();

        if (c < 0)
            break;

        uint8_t b = (uint8_t)c;
        webManager.logByte('T', b);


#ifdef USE_IMPROV

        /*
         * Improv is only relevant to USB input before normal bridge traffic.
         */
        if (!improvSerial.handleBuffer(&b, 1))
        {
            Transceiver.write(&b, 1);
            knxTx++;
        }

#else

        Transceiver.write(&b, 1);
        knxTx++;

#endif

        usbRx++;

        activity = true;

        /*
         * Prevent a pathological continuous USB stream from starving
         * the rest of the firmware.
         */
    }


    /*
     * ======================================================================
     * TPUART -> USB
     * ======================================================================
     *
     * Again, do NOT use:
     *
     *     av = Transceiver.available();
     *     Transceiver.readBytes(buffer, av);
     *
     * Consume only data which is currently available.
     */

while (Transceiver.available() > 0)
{
    int c = Transceiver.read();

    if (c < 0)
        break;

    uint8_t b = (uint8_t)c;

    webManager.logByte('R', b);

    Serial.write(&b, 1);

    knxRx++;
    usbTx++;

    activity = true;

}

    /*
     * ======================================================================
     * Activity LED
     * ======================================================================
     */

    if (activity)
    {
        lastActivity = millis();

        digitalWrite(LED_BUILTIN, HIGH);
        ledActive = true;
    }
    else if (ledActive &&
             (millis() - lastActivity >= LED_ACTIVITY_MS))
    {
        digitalWrite(LED_BUILTIN, LOW);
        ledActive = false;
    }


    /*
     * ======================================================================
     * Diagnostic status
     * ======================================================================
     *
     * Only every 10 seconds.
     *
     * This is deliberately not part of the bridge protocol.
     */

    if (millis() - lastStatus >= STATUS_INTERVAL_MS)
    {
        lastStatus = millis();

        Serial.print("STATUS usbRx=");
        Serial.print(usbRx);

        Serial.print(" usbTx=");
        Serial.print(usbTx);

        Serial.print(" knxRx=");
        Serial.print(knxRx);

        Serial.print(" knxTx=");
        Serial.print(knxTx);

        Serial.print(" avail=");
        Serial.println(Transceiver.available());
    }


    /*
     * Make sure the watchdog is serviced even if no traffic exists.
     */
}
