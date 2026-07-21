# ⚡ DECENTRALIZED MESH CHATROOM & SECURE CRYPTOGRAPHY SYSTEM
### *An offline, encrypted, peer-to-peer communication network operating on an isolated hardware mesh.*

---

## 🚀 The Vision
This project is an **offline-first, hardware-isolated communications network** built using two custom ESP32 server nodes and an nRF24L01 radio frequency mesh. It acts as an independent communication bridge, allowing secure rooms, instant messaging, and turn-based multiplayer games to function entirely without internet connectivity, cellular networks, or central servers.

---

## 🏆 Key Features (Portfolio Highlights)

### 🔒 1. Seamless Diffie-Hellman Fallback Cryptography (E2EE)
*   **The Problem:** ESP32 is served over insecure HTTP (`192.168.4.1`), causing modern browsers to disable Web Crypto APIs (`crypto.subtle`), rendering standard P-256 ECDH unusable.
*   **The Engineering Solution:** Built an inline **Diffie-Hellman (DH) key exchange** in pure JavaScript utilizing BigInt modular exponentiation ($g^x \pmod p$). Clients automatically agree on matching shared secrets offline, encrypting and decrypting data using AES-GCM (fallback) to ensure absolute confidentiality.
*   **Zero-Loop Handshake:** Implemented a bi-directional handshake validation (`isResponse` flags) that completes in a single round-trip, preventing handshake loops.

### 📡 2. Dual-Node nRF24L01 RF Mesh Network
*   **Decentralized Relaying:** Links separated communities by relaying WebSocket chat messages, IP bans, pins, and E2EE key exchanges node-to-node over an nRF24L01 transceiver mesh.
*   **Alternate Pipe Channel Alignment:** Symmetrically swapped transmitter/receiver pipe channels to guarantee collision-free, bi-directional radio communication.

### 🎮 3. Turn-Aware Real-time Gaming Engine
*   **Play Anywhere:** Play fully offline Tic-Tac-Toe, Chess, and Rock-Paper-Scissors.
*   **Intelligent Turn-Based Timers:** Move timers (45 seconds per turn) are turn-aware—only ticking down for the active player. The timer automatically pauses and resets when the turn shifts, resolving issues with premature forfeiting.

### 🛡️ 4. Anti-Ghost Client Pruner & Security Panel
*   **Ghost Client Purger:** A custom background task detects disconnected/inactive WebSockets and cleanly frees server slots after 3 minutes.
*   **Admin Dashboard:** An authenticated (password-protected) ban manager allowing real-time IP kicks and permanent bans.
*   **Smart State Reconnections:** Seamlessly redirects users back to their active private room (e.g. `#darknet` or `#vault`) after a temporary connection drop.

---

## 🛠️ Technical Stack & Hardware Specification
*   **SoC Board:** Dual ESP32-D0WD-V3 (Dual Core, 240MHz, isolated SoftAP hotspots).
*   **RF Module:** nRF24L01 transceivers operating on a custom 2.476 GHz channel to avoid standard Wi-Fi interference.
*   **Web Services:** Async TCP Web Server, multi-client WebSockets, and Offline Service Worker caching (`sw.js`).
*   **Algorithms:** Diffie-Hellman key agreement, AES-GCM encryption, BigInt arithmetic, and SHA-256 password hashing.
