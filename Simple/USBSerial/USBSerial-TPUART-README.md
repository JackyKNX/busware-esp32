# BUSWARE ESP32-C3 TUL — KNX TPUART USB Serial Bridge

Modified `USBSerial` firmware for **BUSWARE TUL / ESP32-C3 + NCN5130 TPUART**.

The ESP32-C3 acts as a **transparent USB CDC ↔ KNX TPUART bridge**. KNX
protocol handling remains on Linux in `knxd`.

```text
KNX TP
  │
  ▼
NCN5130 TPUART
  │ 38400 8E1
  ▼
ESP32-C3
  │ USB CDC
  ▼
/dev/ttyACM0
  │
  ▼
/dev/knx1
  │
  ▼
knxd
  │
  ▼
openHAB / KNXnet/IP
```

Tested `knxd` configuration:

```text
-b tpuarts:/dev/knx1
```

## Features

- BUSWARE TUL / ESP32-C3 support
- NCN5130 TPUART at `38400 8E1`
- transparent bidirectional USB ↔ TPUART forwarding
- USB/KNX traffic counters and activity LED
- WiFi management and configuration AP
- WebManager with health and diagnostic information
- Web OTA firmware update with A/B partition switching
- browser serial monitor
- MQTT status and diagnostics
- automatic firmware versioning and build numbering
- mDNS

The ESP32 does **not** implement the KNX application layer. `knxd` handles
KNX protocol processing.

## Important: USB Serial is the KNX transport

In the `busware-tul-c3-serial-transparent` build, the USB CDC `Serial`
interface is the transparent KNX transport channel.

**Do not write debug or diagnostic messages to `Serial`.**

Any text written to `Serial` becomes part of the KNX transport stream and can
corrupt communication with `knxd`.

WebManager, MQTT and other diagnostics therefore use separate logging
mechanisms and must not write diagnostic text to the USB transport stream.

## Build

Build from `Simple/USBSerial`:

```bash
cd Simple/USBSerial
pio run -e busware-tul-c3-serial-transparent
```

Generated images:

```text
firmware/busware-tul-c3-serial-transparent.factory.bin
firmware/busware-tul-c3-serial-transparent.ota.bin
```

Use the factory image for initial/full flashing and the OTA image for normal
WebManager updates.

## KNX / Linux integration

Use a persistent udev name instead of relying on `/dev/ttyACM0`.

Example:

```text
ACTION=="add", SUBSYSTEM=="tty", ATTRS{idVendor}=="303a", ATTRS{idProduct}=="1001", KERNELS=="<USB_PORT>", SYMLINK+="knx1", OWNER="knxd", GROUP="dialout", MODE="0660"
```

Replace `<USB_PORT>` with the stable USB port identifier obtained with
`udevadm info`.

Then:

```text
-b tpuarts:/dev/knx1
```

Do not open the same serial device with another program while `knxd` owns it.

For KNX diagnostics:

```bash
knxtool groupsocketlisten local:/run/knx
```

## Web Management

Open:

```text
http://<ESP32-IP>/
```

The WebManager provides:

- firmware and version information
- WiFi SSID, IP and RSSI
- system health
- USB/KNX counters
- MQTT diagnostics
- WiFi configuration
- MQTT configuration
- OTA firmware update
- browser serial monitor
- ESP32 restart

### WiFi

Credentials are stored in ESP32 NVS and survive normal OTA updates.

If no WiFi configuration exists, the TUL starts a configuration access point:

```text
SSID: TUL-XXXXXX
Password: tulsetup
IP: 192.168.4.1
```

Open:

```text
http://192.168.4.1/
```

and select **WiFi configuration**.

## MQTT

MQTT is an optional **management/telemetry** channel. It does not replace
the KNX USB transport path.

Configure MQTT at:

```text
/mqtt
```

Parameters:

- enable/disable
- broker hostname/IP
- port
- username/password
- base topic

Default base topic:

```text
busware/TUL
```

Topics:

```text
busware/TUL/availability
busware/TUL/status
busware/TUL/event
busware/TUL/error
busware/TUL/knx/bytes
```

### Availability

`availability` uses retained `online/offline` state and MQTT Last Will and
Testament (LWT).

### Status

`status` is a retained JSON message published every 60 seconds.

It contains:

- firmware version
- uptime
- IP address
- SSID
- RSSI
- USB RX/TX counters
- KNX RX/TX counters
- free heap
- minimum free heap
- reset reason

Example:

```json
{
  "firmware": "v1.4+65",
  "uptime": 123456,
  "ip": "10.192.160.57",
  "ssid": "Centralna",
  "rssi": -57,
  "knx_rx": 12345,
  "knx_tx": 6789,
  "usb_rx": 2345,
  "usb_tx": 3456,
  "free_heap": 184320,
  "min_free_heap": 172456,
  "reset_reason": "Software reset"
}
```

The exact firmware version and runtime values depend on the running device.

### KNX byte counter

```text
busware/TUL/knx/bytes
```

This is a retained numeric value containing:

```text
KNX RX bytes + KNX TX bytes
```

The counter represents transported KNX bytes, not telegram count.

### Events

```text
busware/TUL/event
```

`event` is a non-retained event stream for significant device events.

### Errors

```text
busware/TUL/error
```

`error` stores the last reported error as a retained message.

## OTA / A-B update

The TUL uses two application partitions:

```text
0x10000   app0   1280K
0x150000  app1   1280K
```

Normal WebManager OTA writes the inactive slot, completes the update and
selects it for the next boot.

Both directions have been tested:

```text
app0 → app1 → reboot → app1
app1 → app0 → reboot → app0
```

For WebManager OTA use:

```text
firmware/busware-tul-c3-serial-transparent.ota.bin
```

## Serial monitor

Open:

```text
/serial
```

The Serial Monitor provides a diagnostic view of raw TPUART traffic.

It does not write diagnostic text into the USB transport stream.

## Firmware versioning

Automatic versioning is generated by:

```text
pio-tools/buildscript_versioning.py
```

Generated header:

```text
Simple/USBSerial/include/version.h
```

Version format:

```text
v<major.minor>+<build>
```

For example:

```text
v1.4+65
```

The build counter is stored in:

```text
.buildcounter
```

and excluded from Git.

`version.h` should not be edited manually.

## Tested

Production target:

```text
busware-tul-c3-serial-transparent
```

Tested with:

- ESP32-C3 / 4 MB flash
- BUSWARE TUL / NCN5130 TPUART
- PlatformIO / Espressif32 7.0.1
- Arduino-ESP32
- `welteki/knxd:latest`
- real KNX traffic in both directions
- ETS device discovery and programming

The bridge has been tested with normal KNX traffic while MQTT diagnostics
were active.

## Project origin

Based on the original **BUSWARE ESP32** project:

https://github.com/tostmann/busware-esp32

This fork adds the TUL / ESP32-C3 TPUART USB bridge, WebManager, OTA
handling, diagnostics and MQTT telemetry.

## Security

WebManager OTA authentication currently uses development/test credentials
embedded in the firmware source.

These credentials must be changed before production deployment.

Do not commit WiFi credentials, MQTT passwords or other secrets.
