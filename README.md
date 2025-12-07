# ESP32 Smart Connect Plug

A HomeKit-enabled smart plug built on the **ESP32 Lifecycle Manager (LCM)**. The firmware uses the LCM to handle Wi‑Fi, OTA, and NVS bookkeeping while exposing an outlet service to Apple HomeKit. A single GPIO drives the relay and blue indicator LED, and an on-board button toggles the plug or performs lifecycle resets.

## Features
- **HomeKit outlet service** with firmware revision and OTA trigger characteristics managed by the LCM.
- **Wi‑Fi startup via LCM**: provisioning-aware `wifi_start()` call starts networking and invokes HomeKit when ready.
- **Relay and indicator control**: central `relay_set_state()` syncs the relay output, the shared blue LED, and HomeKit notifications.
- **Button interactions** using `esp32-button`:
  - Single press: toggle the relay and notify HomeKit clients.
  - Long press (10s): lifecycle factory reset followed by reboot.
- **Identify support**: the blue LED blinks when HomeKit requests identify.

## Hardware defaults (configurable in `menuconfig`)
| Setting | Default | Description |
| --- | --- | --- |
| `CONFIG_ESP_RELAY_GPIO` | `5` | GPIO driving the relay output. |
| `CONFIG_ESP_BLUE_LED_GPIO` | `7` | GPIO for the blue indicator LED (active low). |
| `CONFIG_ESP_BUTTON_GPIO` | `6` | GPIO for the active-low button. |
| `CONFIG_ESP_SETUP_CODE` | `693-41-208` | HomeKit setup code used by the QR code. |
| `CONFIG_ESP_SETUP_ID` | `M4T8` | HomeKit setup ID used by the QR code. |

Update these values from the **StudioPieters** menu in `menuconfig` and regenerate `qrcode.png` if you change the setup code or ID.

## Building
1. Install ESP-IDF **5.0 or newer**.
2. From the project root, select your target and configure options:
   ```bash
   idf.py set-target esp32
   idf.py menuconfig
   ```
3. Build and flash:
   ```bash
   idf.py build
   idf.py flash monitor
   ```

## Pairing with HomeKit
1. Provision Wi‑Fi through the LCM flow if prompted; otherwise the device starts HomeKit automatically when Wi‑Fi is ready.
2. In the Home app, scan `qrcode.png` or enter the setup code from the table above to add the accessory.

## Component requirements
Declared in `main/idf_component.yml`:
- ESP-IDF `>=5.0`
- `achimpieters/esp32-homekit >=1.3.3`
- `achimpieters/esp32-button >=1.2.3`

## Behavior overview
- The relay and blue LED reflect the HomeKit ON characteristic and stay in sync with physical button presses.
- OTA updates can be triggered from HomeKit via the LCM-provided characteristic.
- Holding the button for 10 seconds performs a full lifecycle factory reset and restarts the device.
- The identify routine blinks the blue LED and restores the prior relay state when finished.
