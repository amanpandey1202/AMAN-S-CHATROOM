# 📡 ESP32 Cyberpunk Mesh Chatroom 🎃

An offline, secure, decentralized local-network chatroom and walkie-talkie messaging web application utilizing two **ESP32** microcontrollers connected via an **nRF24L01** radio transceiver bridge.

> [!TIP]
> ### ⚡ Live Interactive Web Demo
> You can preview and test the complete chat client interface directly in your browser without any hardware:
> **[👉 Open Live Interactive Web Preview](https://htmlpreview.github.io/?https://github.com/amanpandey1202/AMAN-S-CHATROOM/blob/main/preview.html)**
> 
> *💡 **Pro-tip**: Type `\v2` or `/v2` inside the chat input box to instantly toggle the **Handcrafted Cyberpunk Midnight Mesh Theme**!*

---

## 📸 Screenshots & Demos

*(Add your setup photos, mesh range tests, or interface screenshots here!)*
* **Demo Video/GIF**: `![Demo Video](./assets/demo.gif)`
* **Node Setup**: `![Node Hardware](./assets/nodes.jpg)`
* **Chat Room UI**: `![Chat UI](./assets/chat_ui.jpg)`

---

## ✨ Features
* **Decentralized Local Server**: Runs a lightweight Asynchronous Web Server and WebSocket server directly on the ESP32 chip.
* **Dual Theme Engine (`\v2` Command)**: Type `\v2` or `/v2` in chat to toggle between Classic Glassmorphism and the Handcrafted Cyberpunk Midnight Mesh theme.
* **Symmetrical Radio Bridge**: Transmits packets between two distant nodes over the nRF24L01+PA+LNA radio bridge on Channel 76 (2.476 GHz) to bypass local network blockages.
* **End-to-End Encryption (E2EE)**: Secures private chats and the `#vault` room using standard browser Cryptography (ECDH and AES-GCM) with a secure RC4 encryption fallback.
* **Walkie-Talkie Voice Messaging**: Downsamples, compresses, and broadcasts real-time mono voice streams (up to 32KB buffers) over WebSockets in the `#airwaves` and `#vault` rooms.
* **Responsive Cyberpunk UI**: Features a beautiful glassmorphism theme with room occupant avatar badges, online user cards, admin control boards, and float emoji pickers.
* **Offline Games**: Built-in P2P games like Chess, Rock-Paper-Scissors, and Tic-Tac-Toe running over WebSockets with custom rate-limit-exempt routing.

---

## 🛠️ Hardware Used
* **2x ESP32 Development Boards** (NodeMCU or similar DevKit).
* **2x nRF24L01+PA+LNA Transceiver Modules** (with external antennas for maximum range).
* **2x nRF24L01 Socket Adapters** (or 10µF bypass capacitors soldered across VCC and GND pins to prevent brownouts).
* **Jumper Wires & Power Banks/USB Cables**.

---

## 🔌 Circuit Diagram / VSPI Pin Connections

Both ESP32 boards must be wired to their respective nRF24L01 transceivers using the standard VSPI hardware bus:

| nRF24L01 Pin | ESP32 Pin | Description |
| :--- | :--- | :--- |
| **VCC** | **3.3V** | Power (Socket adapter or capacitor highly recommended!) |
| **GND** | **GND** | Ground |
| **CE** | **GPIO 4** | Chip Enable (Configured in sketch) |
| **CSN** | **GPIO 5** | SPI Chip Select (Configured in sketch) |
| **SCK** | **GPIO 18** | SPI Clock |
| **MOSI** | **GPIO 23** | SPI Master Out Slave In |
| **MISO** | **GPIO 19** | SPI Master In Slave Out |

---

## ⚡ Setup: Node A vs. Node B

To establish the bi-directional radio link, the two ESP32 boards must run different node configurations:

| Node | Sketch Folder | Role | Wi-Fi SSID | Default IP |
| :--- | :--- | :--- | :--- | :--- |
| **Node A** | [`node_a/`](./node_a/) | **Primary Host** (Acts as main AP & Web UI server) | `PURPLE-CHAT-A` | `192.168.4.1` |
| **Node B** | [`node_b/`](./node_b/) | **Client / Peer** (Bridges secondary users to the mesh) | `PURPLE-CHAT-B` | `192.168.4.1` |

---

## 🚀 How to Run the Project

### 1. Compile and Flash the Sketches
1. Connect your **first ESP32**, open [`node_a/node_a.ino`](./node_a/node_a.ino) in Arduino IDE, and upload the code. (Label this board **Node A**).
2. Connect your **second ESP32**, open [`node_b/node_b.ino`](./node_b/node_b.ino) in Arduino IDE, and upload the code. (Label this board **Node B**).

### 2. Connect to the Chatroom
1. On your phone or laptop, connect to the Wi-Fi hotspot:
   * **SSID**: `PURPLE-CHAT-A` (or `PURPLE-CHAT-B` for the client node)
   * **Password**: `AMANPANDEY99`
2. Open your web browser. The Captive Portal will automatically redirect you to the chat interface at `http://192.168.4.1/`.
3. Log in with the web password: `5676`.

### 🎙️ Enabling Microphone Walkie-Talkie Permissions
Since the ESP32 server runs over unsecure HTTP, modern browsers block microphone access by default. Follow these steps to bypass this block:
1. Open a new tab in Chrome/Edge and go to: `chrome://flags/#unsafely-treat-insecure-origin-as-secure`
2. Search for **"Insecure origins treated as secure"** and set it to **Enabled**.
3. In the text box, enter your node IP: `http://192.168.4.1`
4. Click **Relaunch** to restart the browser.
5. Re-open `http://192.168.4.1/`, go to the `#airwaves` room, and press the Microphone button to communicate!
