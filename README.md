# AMAN'S CHATROOM
ESP32 WHATSAPP STYLED OFFLINE MESH SYSTEM
<br>
# 📡 ESP32 Cyberpunk Mesh Chatroom 🎃

An offline, secure, local-network chatroom and walkie-talkie messaging web application utilizing two **ESP32** microcontrollers connected via an **nRF24L01** radio transceiver bridge.

---

## ⚡ Quick Start: Node A vs. Node B

To prevent confusion when uploading to your ESP32 boards, the project is divided into two distinct configurations:

| Node | Sketch Folder | Role | Wi-Fi SSID | Default IP |
| :--- | :--- | :--- | :--- | :--- |
| **Node A** | [`node_a/`](./node_a/) | **Primary Host** (Acts as main AP & Web UI server) | `PURPLE-CHAT-A` | `192.168.4.1` |
| **Node B** | [`node_b/`](./node_b/) | **Client / Peer** (Bridges secondary users to the mesh) | `PURPLE-CHAT-B` | `192.168.4.1` |

> [!IMPORTANT]
> **Upload Instructions:**
> * Connect your **first ESP32** to your computer, open `node_a/node_a.ino` in Arduino IDE, and upload it. (Label this board **Node A**).
> * Connect your **second ESP32**, open `node_b/node_b.ino` in Arduino IDE, and upload it. (Label this board **Node B**).

---

## 🔌 nRF24L01 Radio Pin Connections

Both ESP32 boards must be wired to their respective nRF24L01 transceivers using the standard VSPI hardware bus as follows:

| nRF24L01 Pin | ESP32 Pin | Description |
| :--- | :--- | :--- |
| **VCC** | **3.3V** | Power (use a bypass capacitor to prevent voltage drops!) |
| **GND** | **GND** | Ground |
| **CE** | **GPIO 4** | Chip Enable (Configured in sketch) |
| **CSN** | **GPIO 5** | SPI Chip Select (Configured in sketch) |
| **SCK** | **GPIO 18** | SPI Clock |
| **MOSI** | **GPIO 23** | SPI Master Out Slave In |
| **MISO** | **GPIO 19** | SPI Master In Slave Out |

---

## 🛠️ Modifying the Web UI Assets

The Web UI (including HTML, CSS, JavaScript, manifest, and service worker) is stored directly in the ESP32 flash memory (`PROGMEM`). 

If you make modifications to the raw source files:
* [index.html](./index.html) (Core Chat interface)
* [manifest.json](./manifest.json) (PWA Configuration & Base64 Pumpkin Icon)
* [sw.js](./sw.js) (PWA Caching & Offline Worker)

Simply run the compiled assets script to auto-generate the C++ header files (`index_html.h`, `manifest_json.h`, `sw_js.h`) in both sketch folders:
```powershell
./build_all_assets.ps1
```

---

## 🎙️ Enabling Microphone Walkie-Talkie Permissions

Because the ESP32 server runs over `http` rather than secure `https`, mobile and desktop browsers block the Microphone API by default. 

To enable the Walkie-Talkie feature in the `#airwaves` and `#vault` rooms:
1. Open a new tab in Chrome/Edge and go to: `chrome://flags/#unsafely-treat-insecure-origin-as-secure`
2. Search for **"Insecure origins treated as secure"** and set it to **Enabled**.
3. In the text box, enter your node IP: `http://192.168.4.1`
4. Click **Relaunch** to restart the browser.
5. Re-open `http://192.168.4.1/`, enter the room, and tap the Microphone button.
