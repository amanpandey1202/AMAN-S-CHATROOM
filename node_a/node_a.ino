#include <WiFi.h>
#include <DNSServer.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <RF24.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_task_wdt.h"
#include <Preferences.h>

// -- Configuration -------------------------------------------
#include "secret/secrets.h"

namespace Config {
  // Credentials are dynamically decrypted from XOR storage to prevent firmware binary analysis extraction
  inline String getSSID() { return Secrets::get(Secrets::AP_SSID_A, sizeof(Secrets::AP_SSID_A)); }
  inline String getAPPass() { return Secrets::get(Secrets::AP_PASS, sizeof(Secrets::AP_PASS)); }
  inline String getWebPass() { return Secrets::get(Secrets::WEB_PASS, sizeof(Secrets::WEB_PASS)); }
  inline String getAdminPass() { return Secrets::get(Secrets::ADMIN_PASS, sizeof(Secrets::ADMIN_PASS)); }
  
  const uint8_t MAX_CLIENTS      = 4;  // ESP32 softAP reliably supports max 4 stations
  const uint8_t MAX_HISTORY      = 20;           // Keep in RAM
  const uint8_t RATE_LIMIT_MSG   = 5;            // Max messages per client in interval
  const uint16_t RATE_LIMIT_MS   = 3000;         // Rate limit interval
  
  // nRF24L01 SPI Pins (Standard VSPI)
  const uint8_t NRF_CE_PIN  = 4;
  const uint8_t NRF_CSN_PIN = 5;
  const uint8_t NRF_CHANNEL = 76;                // 2.476 GHz (avoids standard Wi-Fi interference)
  const rf24_pa_dbm_e NRF_PA_LEVEL = RF24_PA_HIGH; // PA+LNA module handles HIGH safely with capacitor
}

// Node Identity Configuration
const char NODE_ID = 'A';                        // Identifies this board as Node A

// ====== Radio Packets Structs (Packed for 32-byte limit) ======
struct __attribute__((packed)) RadioHeader {
  uint8_t type;         // 0 = Ping (Keepalive), 1 = Data (Chat JSON chunk)
  uint8_t msgId;        // Unique packet sequence identifier
  uint8_t chunkIdx;     // 0-indexed packet order
  uint8_t totalChunks;  // Total packets in sequence
  uint8_t payloadLen;   // Length of chunk in data buffer
};

struct __attribute__((packed)) RadioPacket {
  RadioHeader header;
  uint8_t data[27];     // 32-byte packet boundary (5 header + 27 data)
};

// ====== Pipe Addresses ======
const uint64_t RADIO_PIPE_1 = 0xF0F0F0F0E1LL;   // Pipe 1 (Node A Tx / Node B Rx)
const uint64_t RADIO_PIPE_2 = 0xF0F0F0F0D2LL;   // Pipe 2 (Node B Tx / Node A Rx)
// -- Core Data Structures and Diagnostics (Must be at the top) ------
struct ChatClient {
  uint32_t id        = 0;
  String   username;
  String   room      = "comms";                  // Default room is comms
  uint32_t lastMsgTs = 0;
  uint8_t  msgCount  = 0;
  bool     isAdmin   = false;
  String   wsBuffer  = "";                       // Buffer for fragmented WebSocket frames
  uint32_t lastActivity = 0;
  uint8_t  adminAttempts = 0;
};

struct HistMsg {
  String room;
  String user;
  String text;
  String ts;                                     // HH:MM
  String reply;
  bool   mesh = false;                           // True if received via nRF radio
};

ChatClient  clients[Config::MAX_CLIENTS];
HistMsg history[Config::MAX_HISTORY];
uint8_t histHead = 0;
uint8_t histCount = 0;

// Ban management system
struct BanEntry {
  IPAddress ip;
  String username;
  bool active = false;
};
const uint8_t MAX_BANS = 16;
BanEntry banList[MAX_BANS];

uint32_t packetsSent = 0;
uint32_t packetsFailed = 0;
uint32_t packetsReceived = 0;

// -- Runtime State --------------------------------------------------
// FreeRTOS SPI mutex -- guards radio from dual-core race
SemaphoreHandle_t radioMutex = nullptr;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
DNSServer dnsServer;
RF24 radio(Config::NRF_CE_PIN, Config::NRF_CSN_PIN);
uint8_t nextMsgId = 0;

// -- Non-blocking Radio TX Queue (Fix: prevents loop() blocking) --
struct RadioTxItem {
  RadioPacket packet;
  uint8_t retries = 0;
  uint32_t lastAttempt = 0;
};
const uint8_t TX_QUEUE_SIZE = 24;
RadioTxItem txQueue[TX_QUEUE_SIZE];
volatile uint8_t txHead = 0;
volatile uint8_t txTail = 0;

inline bool txQueueEmpty() { return txHead == txTail; }
inline bool txQueueFull()  { return ((txTail + 1) % TX_QUEUE_SIZE) == txHead; }

void enqueueTxPacket(const RadioPacket &pkt) {
  if (txQueueFull()) { packetsFailed++; return; }
  txQueue[txTail].packet = pkt;
  txQueue[txTail].retries = 0;
  txQueue[txTail].lastAttempt = 0;
  txTail = (txTail + 1) % TX_QUEUE_SIZE;
}

// Call once per loop() -- sends ONE queued packet, never blocks
void drainTxQueue() {
  if (txQueueEmpty()) return;
  
  uint32_t now = millis();
  // Wait at least 50ms before retrying a failed packet
  if (txQueue[txHead].retries > 0 && (now - txQueue[txHead].lastAttempt < 50)) {
    return; // Wait for next loop
  }
  
  if (xSemaphoreTake(radioMutex, 0) != pdTRUE) return; // skip if busy
  radio.stopListening();
  txQueue[txHead].lastAttempt = now;
  bool ok = radio.write(&txQueue[txHead].packet, sizeof(RadioPacket));
  radio.startListening();
  xSemaphoreGive(radioMutex);
  
  if (ok) {
    packetsSent++;
    txHead = (txHead + 1) % TX_QUEUE_SIZE;
  } else {
    txQueue[txHead].retries++;
    if (txQueue[txHead].retries >= 5) {
      packetsFailed++;
      txHead = (txHead + 1) % TX_QUEUE_SIZE; // discard after 5 failed attempts
    }
  }
}

bool isBanned(IPAddress ip, const String &username) {
  for (int i = 0; i < MAX_BANS; i++) {
    if (banList[i].active) {
      if (banList[i].ip != IPAddress(0,0,0,0) && banList[i].ip == ip) return true;
      if (username.length() > 0 && banList[i].username.equalsIgnoreCase(username)) return true;
    }
  }
  return false;
}

bool addBan(IPAddress ip, const String &username) {
  if (isBanned(ip, username)) return true;
  for (int i = 0; i < MAX_BANS; i++) {
    if (!banList[i].active) {
      banList[i].ip = ip;
      banList[i].username = username;
      banList[i].active = true;
      saveBansToNVS();
      return true;
    }
  }
  return false;
}

bool removeBan(const String &query) {
  bool removed = false;
  for (int i = 0; i < MAX_BANS; i++) {
    if (banList[i].active) {
      if (banList[i].username.equalsIgnoreCase(query) || banList[i].ip.toString() == query) {
        banList[i].active = false;
        removed = true;
      }
    }
  }
  if (removed) {
    saveBansToNVS();
  }
  return removed;
}

// -- Tic-Tac-Toe Game Engine -------------------------------------
struct GameRoom {
  bool     active       = false;
  bool     waitingForP2 = false;
  uint32_t playerA      = 0; // Local WS client ID (0 if peer player)
  uint32_t playerB      = 0; // Local WS client ID (0 if peer player)
  bool     peerPlayerA  = false; // True if Player A is on the peer node
  bool     peerPlayerB  = false; // True if Player B is on the peer node
  String   nameA;
  String   nameB;
  int8_t   board[9];
  uint8_t  turn         = 0;   // 0=A(X), 1=B(O)
  uint8_t  scoreA       = 0;
  uint8_t  scoreB       = 0;
  GameRoom() { memset(board, -1, sizeof(board)); }
};

const uint8_t MAX_QUEUE = 4;
struct QueueEntry { uint32_t id = 0; String name; };
GameRoom   gameRoom;
QueueEntry waitQueue[MAX_QUEUE];
uint8_t    queueLen = 0;

// nRF Radio Peer Users registry to display cross-node users
struct PeerUser {
  String name;
  String room;
  bool isAdmin = false;
  uint32_t lastSeen = 0;
  bool active = false;
};
const uint8_t MAX_PEER_USERS = 8;
PeerUser peerUsers[MAX_PEER_USERS];

void addPeerUser(const String &name, const String &room, bool isAdmin) {
  uint32_t now = millis();
  // Update if exists
  for (int i = 0; i < MAX_PEER_USERS; i++) {
    if (peerUsers[i].active && peerUsers[i].name.equalsIgnoreCase(name)) {
      peerUsers[i].room = room;
      peerUsers[i].isAdmin = isAdmin;
      peerUsers[i].lastSeen = now;
      return;
    }
  }
  // Add new
  for (int i = 0; i < MAX_PEER_USERS; i++) {
    if (!peerUsers[i].active) {
      peerUsers[i].name = name;
      peerUsers[i].room = room;
      peerUsers[i].isAdmin = isAdmin;
      peerUsers[i].lastSeen = now;
      peerUsers[i].active = true;
      return;
    }
  }
}

void removePeerUser(const String &name) {
  for (int i = 0; i < MAX_PEER_USERS; i++) {
    if (peerUsers[i].active && peerUsers[i].name.equalsIgnoreCase(name)) {
      peerUsers[i].active = false;
      peerUsers[i].name = "";
      peerUsers[i].room = "";
      return;
    }
  }
}

void cleanOfflinePeers() {
  uint32_t now = millis();
  for (int i = 0; i < MAX_PEER_USERS; i++) {
    if (peerUsers[i].active && (now - peerUsers[i].lastSeen > 10000)) { // 10 second timeout
      peerUsers[i].active = false;
      peerUsers[i].name = "";
      peerUsers[i].room = "";
    }
  }
}


static const int8_t WIN_LINES[8][3] = {
  {0,1,2},{3,4,5},{6,7,8},
  {0,3,6},{1,4,7},{2,5,8},
  {0,4,8},{2,4,6}
};

// -- Private Vault Room ------------------------------------------
String   vaultKey  = "";                       // Empty = not generated yet
uint32_t vaultAllowed[Config::MAX_CLIENTS];    // Authorised client IDs
bool     vaultAllowedInit = false;

// Time Keeping variables (offline sync with browser clock)
uint32_t timeOffset = 0;
bool timeSynced = false;

// Peer Diagnostics State
uint32_t lastPeerContact = 0;
bool peerOnline = false;

// Radio Reassembly State
#define RX_BUF_SIZE 512
struct ReassemblyBuffer {
  uint8_t msgId = 0xFF;
  uint8_t totalChunks = 0;
  uint32_t chunksReceivedMask = 0;
  char data[RX_BUF_SIZE];
  uint32_t lastActivity = 0;
};
static ReassemblyBuffer rxBuf;

// Preferences and Bans
Preferences prefs;

void saveBansToNVS() {
  if (!prefs.begin("bans", false)) {
    Serial.println("[NVS] Error: Failed to open 'bans' for saving!");
    return;
  }
  prefs.clear();
  uint8_t count = 0;
  for (int i = 0; i < MAX_BANS; i++) {
    if (banList[i].active) {
      String key = "b" + String(count);
      String val = banList[i].ip.toString() + "|" + banList[i].username;
      prefs.putString(key.c_str(), val);
      count++;
    }
  }
  prefs.putUChar("count", count);
  prefs.end();
}

void loadBansFromNVS() {
  for (int i = 0; i < MAX_BANS; i++) {
    banList[i].active = false;
    banList[i].username = "";
    banList[i].ip = IPAddress(0, 0, 0, 0);
  }
  if (!prefs.begin("bans", false)) {
    Serial.println("[NVS] Error: Failed to open 'bans' preferences!");
    return;
  }
  uint8_t count = prefs.getUChar("count", 0);
  for (int i = 0; i < count && i < MAX_BANS; i++) {
    String key = "b" + String(i);
    String val = prefs.getString(key.c_str(), "");
    if (val.length() > 0) {
      int pipeIdx = val.indexOf('|');
      if (pipeIdx > 0) {
        String ipStr = val.substring(0, pipeIdx);
        String userStr = val.substring(pipeIdx + 1);
        IPAddress ip;
        if (ip.fromString(ipStr)) {
          banList[i].ip = ip;
          banList[i].username = userStr;
          banList[i].active = true;
        }
      }
    }
  }
  prefs.end();
}

// Pinned messages state
struct PinnedMsg {
  String text = "";
  String from = "";
  String ts = "";
  bool active = false;
};
PinnedMsg pinnedMsgs[6];

int roomIndex(const String &roomName) {
  if (roomName == "comms") return 0;
  if (roomName == "airwaves") return 1;
  if (roomName == "terminal") return 2;
  if (roomName == "game") return 3;
  if (roomName == "darknet") return 4;
  if (roomName == "vault") return 5;
  return -1;
}

// Poll state
struct PollState {
  bool active = false;
  String creator = "";
  String room = "";
  String question = "";
  String options[4];
  uint8_t optCount = 0;
  uint8_t votes[4] = {0};
  String votedNames[16];
  uint8_t votedCount = 0;
};
PollState activePoll;

// Message deduplication
uint16_t recentMsgHashes[8] = {0};
uint8_t hashIdx = 0;

uint16_t msgHash(const String& user, const String& text) {
  uint16_t h = 0;
  for (char c : user) h = h * 31 + c;
  for (char c : text) h = h * 31 + c;
  return h;
}

bool isDuplicate(uint16_t h) {
  for (int i = 0; i < 8; i++) {
    if (recentMsgHashes[i] == h) return true;
  }
  recentMsgHashes[hashIdx++ % 8] = h;
  return false;
}

uint32_t lastTypingRadio = 0;

// -- Forward Declarations --------------------------------------
void broadcastLinkStatus();
void broadcastBanList();
bool isBanned(IPAddress ip, const String &username);
void sendJsonToRadio(const String &jsonStr);
void processRadioMessage(const char* jsonStr);
void handleGameForfeit(uint32_t disconnectedId);
void removeFromQueue(uint32_t id);
void updateQueuePositions();
void broadcastGameBoard(int8_t wl0 = -1, int8_t wl1 = -1, int8_t wl2 = -1, int8_t winner = -1, bool draw = false);
// startGame declared below at definition
void promoteQueue();
String generateVaultKey();
bool isVaultAllowed(uint32_t id, bool isAdmin);
void addVaultAllowed(uint32_t id);
void removeVaultAllowed(uint32_t id);

// -- Helpers ----------------------------------------------------------

String getTime() {
  uint32_t t = (millis() / 1000) + timeOffset;
  uint32_t h = (t / 3600) % 24;
  uint32_t m = (t / 60) % 60;
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
  return String(buf);
}

ChatClient* findClient(uint32_t id) {
  for (auto &c : clients) if (c.id == id) return &c;
  return nullptr;
}

ChatClient* findClientByName(const String &name) {
  for (auto &c : clients) if (c.username == name) return &c;
  return nullptr;
}

int freeSlot() {
  for (int i = 0; i < Config::MAX_CLIENTS; i++) if (clients[i].id == 0) return i;
  return -1;
}

void removeClient(uint32_t id) {
  for (auto &c : clients) {
    if (c.id == id) {
      c.id = 0;
      c.username = "";
      c.wsBuffer = "";
    }
  }
}

bool rateOk(ChatClient *c) {
  uint32_t now = millis();
  if (now - c->lastMsgTs > Config::RATE_LIMIT_MS) {
    c->msgCount = 0;
    c->lastMsgTs = now;
  }
  return ++c->msgCount <= Config::RATE_LIMIT_MSG;
}

String expandEmoji(String s) {
  s.replace(":)", "😊"); s.replace(":D", "😀");
  s.replace(":(", "😔"); s.replace(":P", "😜");
  s.replace(":heart:", "❤️"); s.replace(":fire:", "🔥");
  s.replace(":+1:", "👍"); s.replace(":100:", "💯");
  s.replace(":wave:", "👋"); s.replace(":ok:", "👌");
  return s;
}

// -- Game Engine Helpers ------------------------------------------

int8_t checkWinner(int8_t b[]) {
  for (int i = 0; i < 8; i++) {
    int8_t a=WIN_LINES[i][0], bi=WIN_LINES[i][1], ci=WIN_LINES[i][2];
    if (b[a] >= 0 && b[a] == b[bi] && b[bi] == b[ci]) return b[a];
  }
  return -1;
}

bool isBoardFull(int8_t b[]) {
  for (int i = 0; i < 9; i++) if (b[i] < 0) return false;
  return true;
}

void sendGameMsg(uint32_t cid, const String& json) {
  if (cid) ws.text(cid, json);
}

void broadcastGameBoard(int8_t wl0, int8_t wl1, int8_t wl2, int8_t winner, bool draw) {
  String out = "{\"type\":\"gameBoard\",\"board\":[";
  for (int i = 0; i < 9; i++) { out += String(gameRoom.board[i]); if (i < 8) out += ',';}  
  out += "],\"turn\":"; out += gameRoom.turn;
  out += ",\"nameA\":\""; out += gameRoom.nameA; out += '"';
  out += ",\"nameB\":\""; out += gameRoom.nameB; out += '"';
  out += ",\"scoreA\":"; out += gameRoom.scoreA;
  out += ",\"scoreB\":"; out += gameRoom.scoreB;
  if (winner >= 0) {
    out += ",\"winner\":"; out += winner;
    out += ",\"winLine\":["; out += wl0; out += ','; out += wl1; out += ','; out += wl2; out += ']';
  }
  if (draw) out += ",\"draw\":true";
  out += '}';
  if (!gameRoom.peerPlayerA) sendGameMsg(gameRoom.playerA, out);
  if (!gameRoom.peerPlayerB) sendGameMsg(gameRoom.playerB, out);
}

void updateQueuePositions() {
  for (int i = 0; i < queueLen; i++) {
    String q = "{\"type\":\"gameQueued\",\"pos\":" + String(i + 1) + "}";
    sendGameMsg(waitQueue[i].id, q);
  }
}

void removeFromQueue(uint32_t id) {
  for (int i = 0; i < queueLen; i++) {
    if (waitQueue[i].id == id) {
      for (int j = i; j < queueLen - 1; j++) waitQueue[j] = waitQueue[j+1];
      waitQueue[queueLen-1] = {0, ""};
      queueLen--;
      updateQueuePositions();
      return;
    }
  }
}

bool dequeuePlayer(uint32_t& outId, String& outName) {
  if (queueLen == 0) return false;
  outId = waitQueue[0].id; outName = waitQueue[0].name;
  for (int i = 0; i < queueLen - 1; i++) waitQueue[i] = waitQueue[i+1];
  waitQueue[queueLen-1] = {0, ""};
  queueLen--;
  updateQueuePositions();
  return true;
}

void sendGameMoveToRadio(int cell) {
  #if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument doc;
  #else
  DynamicJsonDocument doc(128);
  #endif
  doc["type"] = "peerGameMove";
  doc["cell"] = cell;
  String out;
  serializeJson(doc, out);
  sendJsonToRadio(out);
}

void startGame(uint32_t idA, const String& nA, uint32_t idB, const String& nB, uint8_t sA, uint8_t sB, bool isPeerA, bool isPeerB) {
  gameRoom.active = true; gameRoom.waitingForP2 = false;
  gameRoom.playerA = idA; gameRoom.playerB = idB;
  gameRoom.peerPlayerA = isPeerA; gameRoom.peerPlayerB = isPeerB;
  gameRoom.nameA = nA;   gameRoom.nameB = nB;
  gameRoom.turn = 0; gameRoom.scoreA = sA; gameRoom.scoreB = sB;
  memset(gameRoom.board, -1, sizeof(gameRoom.board));
  
  String pa = "{\"type\":\"gameStart\",\"mark\":0,\"nameA\":\"" + nA + "\",\"nameB\":\"" + nB + "\",\"scoreA\":" + String(sA) + ",\"scoreB\":" + String(sB) + "}";
  String pb = "{\"type\":\"gameStart\",\"mark\":1,\"nameA\":\"" + nA + "\",\"nameB\":\"" + nB + "\",\"scoreA\":" + String(sA) + ",\"scoreB\":" + String(sB) + "}";
  
  if (!isPeerA) sendGameMsg(idA, pa);
  if (!isPeerB) sendGameMsg(idB, pb);
  broadcastGameBoard();
}

void handleGameForfeit(uint32_t did) {
  if (gameRoom.waitingForP2 && gameRoom.playerA == did) {
    gameRoom.waitingForP2 = false;
    // Notify peer lobby we are no longer waiting if we left
    sendJsonToRadio("{\"type\":\"peerGameLeaveLobby\"}");
    
    uint32_t nId; String nNm;
    if (dequeuePlayer(nId, nNm)) {
      gameRoom.waitingForP2 = true; gameRoom.playerA = nId; gameRoom.nameA = nNm;
      gameRoom.scoreA = 0; gameRoom.scoreB = 0;
      sendGameMsg(nId, "{\"type\":\"gameWaiting\",\"pos\":0}");
      
      // Advertise our waiting lobby to peer node
      #if ARDUINOJSON_VERSION_MAJOR >= 7
      JsonDocument doc;
      #else
      DynamicJsonDocument doc(128);
      #endif
      doc["type"] = "peerGameLobby";
      doc["name"] = nNm;
      String out; serializeJson(doc, out);
      sendJsonToRadio(out);
    } else { gameRoom.playerA = 0; gameRoom.nameA = ""; gameRoom.scoreA = 0; gameRoom.scoreB = 0; }
    return;
  }
  if (!gameRoom.active) return;
  if (did != gameRoom.playerA && did != gameRoom.playerB) return;
  
  bool aLeft = (did == gameRoom.playerA);
  uint32_t winId = aLeft ? gameRoom.playerB : gameRoom.playerA;
  bool peerWinner = aLeft ? gameRoom.peerPlayerB : gameRoom.peerPlayerA;
  
  String winNm   = aLeft ? gameRoom.nameB   : gameRoom.nameA;
  String losNm   = aLeft ? gameRoom.nameA   : gameRoom.nameB;
  uint8_t sA = gameRoom.scoreA + (aLeft ? 0 : 1);
  uint8_t sB = gameRoom.scoreB + (aLeft ? 1 : 0);
  
  if (!peerWinner) {
    sendGameMsg(winId, "{\"type\":\"gameForfeit\",\"loser\":\"" + losNm + "\",\"scoreA\":" + String(sA) + ",\"scoreB\":" + String(sB) + "}");
  }
  
  // Inform peer node of forfeit
  #if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument doc;
  #else
  DynamicJsonDocument doc(128);
  #endif
  doc["type"] = "peerGameForfeit";
  doc["loser"] = losNm;
  doc["scoreA"] = sA;
  doc["scoreB"] = sB;
  String out; serializeJson(doc, out);
  sendJsonToRadio(out);
  
  gameRoom.active = false;
  gameRoom.playerA = 0; gameRoom.playerB = 0;
  gameRoom.peerPlayerA = false; gameRoom.peerPlayerB = false;
  memset(gameRoom.board, -1, sizeof(gameRoom.board)); gameRoom.turn = 0;
  
  promoteQueue();
}

void promoteQueue() {
  if (gameRoom.active) return;
  if (queueLen >= 2) {
    uint32_t idA, idB; String nA, nB;
    dequeuePlayer(idA, nA);
    dequeuePlayer(idB, nB);
    startGame(idA, nA, idB, nB, 0, 0, false, false);
  } else if (queueLen == 1) {
    uint32_t idA; String nA;
    dequeuePlayer(idA, nA);
    gameRoom.waitingForP2 = true;
    gameRoom.playerA = idA;
    gameRoom.nameA = nA;
    gameRoom.scoreA = 0;
    gameRoom.scoreB = 0;
    sendGameMsg(idA, "{\"type\":\"gameWaiting\",\"pos\":0}");
    
    // Broadcast waiting lobby to other node via radio
    #if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
    #else
    DynamicJsonDocument doc(128);
    #endif
    doc["type"] = "peerGameLobby";
    doc["name"] = nA;
    String out; serializeJson(doc, out);
    sendJsonToRadio(out);
  }
}

// -- Vault Room Helpers --------------------------------------------

String generateVaultKey() {
  if (!vaultAllowedInit) { memset(vaultAllowed, 0, sizeof(vaultAllowed)); vaultAllowedInit = true; }
  const char ch[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  String k = "";
  for (int i = 0; i < 6; i++) k += ch[random(0, 32)];
  return k;
}

bool isVaultAllowed(uint32_t id, bool isAdmin) {
  if (isAdmin) return true;
  for (int i = 0; i < Config::MAX_CLIENTS; i++) if (vaultAllowed[i] == id) return true;
  return false;
}

void addVaultAllowed(uint32_t id) {
  if (!vaultAllowedInit) { memset(vaultAllowed, 0, sizeof(vaultAllowed)); vaultAllowedInit = true; }
  for (int i = 0; i < Config::MAX_CLIENTS; i++) { if (vaultAllowed[i] == 0) { vaultAllowed[i] = id; return; } }
}

void removeVaultAllowed(uint32_t id) {
  for (int i = 0; i < Config::MAX_CLIENTS; i++) { if (vaultAllowed[i] == id) { vaultAllowed[i] = 0; return; } }
}

void addToHistory(const String &room, const String &user,
                  const String &text, const String &reply, bool isMesh = false) {
  history[histHead] = { room, user, text, getTime(), reply, isMesh };
  histHead = (histHead + 1) % Config::MAX_HISTORY;
  if (histCount < Config::MAX_HISTORY) histCount++;
}

// Send user-list update to everyone in a room (including peer users from the other node)
void broadcastUserList(const String &room) {
#if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument doc;
  doc["type"] = "userlist";
  doc["room"] = room;
  JsonArray arr = doc["users"].to<JsonArray>();
  for (auto &c : clients) {
    if (c.id && c.room == room) {
      JsonObject o = arr.add<JsonObject>();
      o["name"]    = c.username;
      o["isAdmin"] = c.isAdmin;
      o["node"]    = String(NODE_ID);
    }
  }
  for (int i = 0; i < MAX_PEER_USERS; i++) {
    if (peerUsers[i].active && peerUsers[i].room == room) {
      JsonObject o = arr.add<JsonObject>();
      o["name"]    = peerUsers[i].name;
      o["isAdmin"] = peerUsers[i].isAdmin;
      o["node"]    = String(NODE_ID == 'A' ? 'B' : 'A');
    }
  }
#else
  DynamicJsonDocument doc(512);
  doc["type"] = "userlist";
  doc["room"] = room;
  JsonArray arr = doc.createNestedArray("users");
  for (auto &c : clients) {
    if (c.id && c.room == room) {
      JsonObject o = arr.createNestedObject();
      o["name"]    = c.username;
      o["isAdmin"] = c.isAdmin;
      o["node"]    = String(NODE_ID);
    }
  }
  for (int i = 0; i < MAX_PEER_USERS; i++) {
    if (peerUsers[i].active && peerUsers[i].room == room) {
      JsonObject o = arr.createNestedObject();
      o["name"]    = peerUsers[i].name;
      o["isAdmin"] = peerUsers[i].isAdmin;
      o["node"]    = String(NODE_ID == 'A' ? 'B' : 'A');
    }
  }
#endif
  
  String out;
  serializeJson(doc, out);
  for (auto &c : clients) {
    if (c.id && c.room == room) ws.text(c.id, out);
  }
}

// Broadcast to all clients in a room
void broadcastRoom(const String &room, const String &payload) {
  for (auto &c : clients) {
    if (c.id && c.room == room) ws.text(c.id, payload);
  }
}

// -- WebSocket event handling and message processing ----------------

void processWsMessage(AsyncWebSocketClient *client, ChatClient *c, const String &raw) {
  c->lastActivity = millis();
#if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument doc;
#else
  DynamicJsonDocument doc(512);
#endif

  if (deserializeJson(doc, raw)) return;

  String msgType = doc["type"] | "";

  // -- setName (first handshake) ----------------------------
  if (msgType == "setName") {
    String name = doc["name"] | "";
    name.trim();
    if (name.length() < 1 || name.length() > 16) {
      client->text("{\"type\":\"error\",\"text\":\"Name must be 1-16 chars\"}");
      return;
    }
    if (isBanned(client->remoteIP(), name)) {
      client->text("{\"type\":\"error\",\"text\":\"BANNED\"}");
      client->close();
      return;
    }
    if (findClientByName(name)) {
      client->text("{\"type\":\"error\",\"text\":\"Name taken\"}");
      return;
    }
    c->username = name;

    // Check if logged in with admin credentials
    String adminPass = doc["adminPass"] | "";
    if (adminPass == Config::getAdminPass()) {
      c->isAdmin = true;
    }

    // Sync client local time to the server if provided
    uint32_t clientTime = doc["time"] | 0;
    if (clientTime > 0) {
      timeOffset = clientTime - (millis() / 1000);
      timeSynced = true;
    }

    // Sync client current room if provided
    String clientRoom = doc["room"] | "comms";
    if (clientRoom == "comms" || clientRoom == "airwaves" || clientRoom == "terminal" ||
        clientRoom == "game" || clientRoom == "darknet" || (clientRoom == "vault" && c->isAdmin)) {
      c->room = clientRoom;
    }

    // Send history for current room
    int start = (histCount < Config::MAX_HISTORY) ? 0 : histHead;
    for (uint8_t i = 0; i < histCount; i++) {
      uint8_t idx = (start + i) % Config::MAX_HISTORY;
      if (history[idx].room != c->room) continue;
      
#if ARDUINOJSON_VERSION_MAJOR >= 7
      JsonDocument h;
#else
      DynamicJsonDocument h(256);
#endif
      h["type"]  = "msg";
      h["user"]  = history[idx].user;
      h["text"]  = history[idx].text;
      h["ts"]    = history[idx].ts;
      h["reply"] = history[idx].reply;
      h["hist"]  = true;
      h["room"]  = c->room;
      h["mesh"]  = history[idx].mesh;
      
      String hs;
      serializeJson(h, hs);
      client->text(hs);
    }

    // Send pinned message and active poll for the current room
    {
      int rIdx = roomIndex(c->room);
      if (rIdx >= 0 && pinnedMsgs[rIdx].active) {
        #if ARDUINOJSON_VERSION_MAJOR >= 7
        JsonDocument pinDoc;
        #else
        DynamicJsonDocument pinDoc(512);
        #endif
        pinDoc["type"] = "pinnedMsg";
        pinDoc["room"] = c->room;
        pinDoc["text"] = pinnedMsgs[rIdx].text;
        pinDoc["from"] = pinnedMsgs[rIdx].from;
        pinDoc["ts"] = pinnedMsgs[rIdx].ts;
        pinDoc["active"] = true;
        String pinOut;
        serializeJson(pinDoc, pinOut);
        client->text(pinOut);
      }
    }
    if (activePoll.active && activePoll.room == c->room) {
      #if ARDUINOJSON_VERSION_MAJOR >= 7
      JsonDocument pollDoc;
      #else
      DynamicJsonDocument pollDoc(512);
      #endif
      pollDoc["type"] = "pollCreate";
      pollDoc["creator"] = activePoll.creator;
      pollDoc["room"] = activePoll.room;
      pollDoc["question"] = activePoll.question;
      #if ARDUINOJSON_VERSION_MAJOR >= 7
      JsonArray arr = pollDoc["options"].to<JsonArray>();
      #else
      JsonArray arr = pollDoc.createNestedArray("options");
      #endif
      for (int i = 0; i < activePoll.optCount; i++) {
        arr.add(activePoll.options[i]);
      }
      
      String pollOut;
      serializeJson(pollDoc, pollOut);
      client->text(pollOut);
      
      #if ARDUINOJSON_VERSION_MAJOR >= 7
      JsonDocument voteDoc;
      #else
      DynamicJsonDocument voteDoc(512);
      #endif
      voteDoc["type"] = "pollUpdate";
      voteDoc["room"] = activePoll.room;
      #if ARDUINOJSON_VERSION_MAJOR >= 7
      JsonArray votesArr = voteDoc["votes"].to<JsonArray>();
      #else
      JsonArray votesArr = voteDoc.createNestedArray("votes");
      #endif
      for (int i = 0; i < activePoll.optCount; i++) {
        votesArr.add(activePoll.votes[i]);
      }
      
      String voteOut;
      serializeJson(voteDoc, voteOut);
      client->text(voteOut);
    }

    // Send the current Link Status to the newly joined user
    broadcastLinkStatus();

    // Announce join
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument sys;
#else
    DynamicJsonDocument sys(128);
#endif
    sys["type"] = "system";
    sys["text"] = name + " joined #" + c->room;
    sys["room"] = c->room;
    
    String sysOut;
    serializeJson(sys, sysOut);
    broadcastRoom(c->room, sysOut);
    broadcastUserList(c->room);
    if (c->isAdmin) {
      broadcastBanList();
    }
    return;
  }

  // All subsequent messages need a valid username
  if (c->username.length() == 0) return;

  // -- changeRoom --------------------------------------------------
  if (msgType == "changeRoom") {
    String newRoom = doc["room"] | "comms";
    if (newRoom != "comms" && newRoom != "airwaves" && newRoom != "terminal" &&
        newRoom != "game"  && newRoom != "darknet"  && newRoom != "vault") return;
    String oldRoom = c->room;

    // -- Vault access check -----------------------------------------
    if (newRoom == "vault" && !c->isAdmin) {
      String key = doc["key"] | "";
      if (vaultKey.length() == 0 || key != vaultKey) {
        client->text("{\"type\":\"error\",\"text\":\"VAULT_DENIED\"}");
        return;
      }
      addVaultAllowed(c->id);
    }

    // -- Leave game / vault cleanly ---------------------------------
    if (oldRoom == "game") { handleGameForfeit(c->id); removeFromQueue(c->id); }
    if (oldRoom == "vault" && !c->isAdmin) removeVaultAllowed(c->id);

    // -- Broadcast "left" to old room (skip for #game) -------------
    if (oldRoom != "game") {
#if ARDUINOJSON_VERSION_MAJOR >= 7
      JsonDocument left;
#else
      DynamicJsonDocument left(128);
#endif
      left["type"] = "system";
      left["text"] = c->username + " left #" + oldRoom;
      left["room"] = oldRoom;
      String leftOut; serializeJson(left, leftOut);
      broadcastRoom(oldRoom, leftOut);
    }

    c->room = newRoom;

    // -- Send history for new room (skip for #game) ----------------
    if (newRoom != "game") {
      int start = (histCount < Config::MAX_HISTORY) ? 0 : histHead;
      for (uint8_t i = 0; i < histCount; i++) {
        uint8_t idx = (start + i) % Config::MAX_HISTORY;
        if (history[idx].room != newRoom) continue;
#if ARDUINOJSON_VERSION_MAJOR >= 7
        JsonDocument h;
#else
        DynamicJsonDocument h(256);
#endif
        h["type"]  = "msg"; h["user"] = history[idx].user; h["text"] = history[idx].text;
        h["ts"]    = history[idx].ts; h["reply"] = history[idx].reply;
        h["hist"]  = true; h["room"] = newRoom; h["mesh"] = history[idx].mesh;
        String hs; serializeJson(h, hs); client->text(hs);
      }
    }

    // Send pinned messages and polls for new room
    {
      int rIdx = roomIndex(newRoom);
      if (rIdx >= 0 && pinnedMsgs[rIdx].active) {
        #if ARDUINOJSON_VERSION_MAJOR >= 7
        JsonDocument pinDoc;
        #else
        DynamicJsonDocument pinDoc(512);
        #endif
        pinDoc["type"] = "pinnedMsg";
        pinDoc["room"] = newRoom;
        pinDoc["text"] = pinnedMsgs[rIdx].text;
        pinDoc["from"] = pinnedMsgs[rIdx].from;
        pinDoc["ts"] = pinnedMsgs[rIdx].ts;
        pinDoc["active"] = true;
        String pinOut;
        serializeJson(pinDoc, pinOut);
        client->text(pinOut);
      }
    }
    if (activePoll.active && activePoll.room == newRoom) {
      #if ARDUINOJSON_VERSION_MAJOR >= 7
      JsonDocument pollDoc;
      #else
      DynamicJsonDocument pollDoc(512);
      #endif
      pollDoc["type"] = "pollCreate";
      pollDoc["creator"] = activePoll.creator;
      pollDoc["room"] = activePoll.room;
      pollDoc["question"] = activePoll.question;
      #if ARDUINOJSON_VERSION_MAJOR >= 7
      JsonArray arr = pollDoc["options"].to<JsonArray>();
      #else
      JsonArray arr = pollDoc.createNestedArray("options");
      #endif
      for (int i = 0; i < activePoll.optCount; i++) {
        arr.add(activePoll.options[i]);
      }
      
      String pollOut;
      serializeJson(pollDoc, pollOut);
      client->text(pollOut);
      
      #if ARDUINOJSON_VERSION_MAJOR >= 7
      JsonDocument voteDoc;
      #else
      DynamicJsonDocument voteDoc(512);
      #endif
      voteDoc["type"] = "pollUpdate";
      voteDoc["room"] = activePoll.room;
      #if ARDUINOJSON_VERSION_MAJOR >= 7
      JsonArray votesArr = voteDoc["votes"].to<JsonArray>();
      #else
      JsonArray votesArr = voteDoc.createNestedArray("votes");
      #endif
      for (int i = 0; i < activePoll.optCount; i++) {
        votesArr.add(activePoll.votes[i]);
      }
      
      String voteOut;
      serializeJson(voteDoc, voteOut);
      client->text(voteOut);
    }

    // -- Broadcast "joined" to new room (skip for #game) ----------
    if (newRoom != "game") {
#if ARDUINOJSON_VERSION_MAJOR >= 7
      JsonDocument joined;
#else
      DynamicJsonDocument joined(128);
#endif
      joined["type"] = "system";
      joined["text"] = c->username + " joined #" + newRoom;
      joined["room"] = newRoom;
      String joinedOut; serializeJson(joined, joinedOut);
      broadcastRoom(newRoom, joinedOut);
    }

    // -- #game: auto-join queue -------------------------------------
    if (newRoom == "game") {
      if (!gameRoom.active && !gameRoom.waitingForP2) {
        gameRoom.waitingForP2 = true; gameRoom.playerA = c->id;
        gameRoom.nameA = c->username; gameRoom.scoreA = 0; gameRoom.scoreB = 0;
        sendGameMsg(c->id, "{\"type\":\"gameWaiting\",\"pos\":0}");
        
        // Broadcast waiting lobby to peer node
        #if ARDUINOJSON_VERSION_MAJOR >= 7
        JsonDocument doc;
        #else
        DynamicJsonDocument doc(128);
        #endif
        doc["type"] = "peerGameLobby";
        doc["name"] = c->username;
        String out; serializeJson(doc, out);
        sendJsonToRadio(out);
      } else if (gameRoom.waitingForP2 && gameRoom.playerA != c->id) {
        startGame(gameRoom.playerA, gameRoom.nameA, c->id, c->username, 0, 0, false, false);
      } else if (gameRoom.active) {
        if (queueLen < MAX_QUEUE) {
          waitQueue[queueLen] = {c->id, c->username}; queueLen++;
          updateQueuePositions();
        } else { sendGameMsg(c->id, "{\"type\":\"error\",\"text\":\"Queue full (max 4)\"}"); }
      }
    }

    // -- #vault: send key to admin, confirm access to user ---------
    if (newRoom == "vault") {
      if (c->isAdmin) {
        if (vaultKey.length() == 0) vaultKey = generateVaultKey();
        client->text("{\"type\":\"vaultKey\",\"key\":\"" + vaultKey + "\"}");
      } else {
        client->text("{\"type\":\"vaultKey\",\"key\":\"\",\"granted\":true}");
      }
    }

    broadcastUserList(oldRoom);
    broadcastUserList(newRoom);
    return;
  }

  // -- typing --------------------------------------------------------
  if (msgType == "typing") {
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument out;
#else
    DynamicJsonDocument out(128);
#endif
    out["type"]  = "typing";
    out["user"]  = c->username;
    out["state"] = doc["state"];
    out["room"]  = c->room;
    
    String outStr;
    serializeJson(out, outStr);
    for (auto &other : clients) {
      if (other.id && other.id != c->id && other.room == c->room)
        ws.text(other.id, outStr);
    }
    return;
  }

  // -- msg ----------------------------------------------------------
  // -- ping (keepalive from client) ------------------------------------
  if (msgType == "ping") {
    client->text("{\"type\":\"pong\"}");
    return;
  }

  if (msgType == "msg") {
    if (!rateOk(c)) {
      client->text("{\"type\":\"error\",\"text\":\"Slow down! Rate limit hit.\"}");
      return;
    }
    String text = doc["text"] | "";
    text.trim();
    if (text.length() == 0 || text.length() > 300) return;
    text = expandEmoji(text);

    // -- /status command (health report, visible only to sender) ------
    if (text == "/status") {
      uint32_t upSec = millis() / 1000;
      uint8_t connCount = 0;
      for (auto& cl : clients) if (cl.id) connCount++;
      uint8_t peerCount = 0;
      for (int i = 0; i < MAX_PEER_USERS; i++) if (peerUsers[i].active) peerCount++;
      uint8_t banCount = 0;
      for (int i = 0; i < MAX_BANS; i++) if (banList[i].active) banCount++;
      
      String st = "[Server Status]\\nNode: ";
      st += NODE_ID;
      st += "  |  Uptime: " + String(upSec/3600) + "h " + String((upSec%3600)/60) + "m\\n";
      st += "Clients: " + String(connCount) + "/4  |  Radio: " + (peerOnline ? "ONLINE" : "OFFLINE") + "\\n";
      st += "Free Heap: " + String(ESP.getFreeHeap()/1024) + "KB  |  TX: " + String(packetsSent) + "  RX: " + String(packetsReceived) + "  Fail: " + String(packetsFailed) + "\\n";
      st += "Peer Users: " + String(peerCount) + "  |  Bans: " + String(banCount);
      client->text("{\"type\":\"system\",\"text\":\"" + st + "\"}");
      return;
    }

    // -- /poll command (admin only) ------------------------------------
    if (text.startsWith("/poll ")) {
      if (!c->isAdmin) {
        client->text("{\"type\":\"error\",\"text\":\"Admins only\"}");
        return;
      }
      if (activePoll.active) {
        client->text("{\"type\":\"error\",\"text\":\"A poll is already active! Use /endpoll first.\"}");
        return;
      }
      int firstQuote = text.indexOf('"');
      int secondQuote = text.indexOf('"', firstQuote + 1);
      if (firstQuote < 0 || secondQuote < 0) {
        client->text("{\"type\":\"error\",\"text\":\"Format: /poll \\\"Question\\\" Opt1 Opt2 ...\"}");
        return;
      }
      activePoll.question = text.substring(firstQuote + 1, secondQuote);
      activePoll.creator = c->username;
      activePoll.room = c->room;
      activePoll.optCount = 0;
      activePoll.votedCount = 0;
      memset(activePoll.votes, 0, sizeof(activePoll.votes));
      for (int i = 0; i < 16; i++) activePoll.votedNames[i] = "";
      
      String remainder = text.substring(secondQuote + 1);
      remainder.trim();
      int optStart = 0;
      while (optStart < remainder.length() && activePoll.optCount < 4) {
        int nextSpace = remainder.indexOf(' ', optStart);
        String opt;
        if (nextSpace < 0) {
          opt = remainder.substring(optStart);
          optStart = remainder.length();
        } else {
          opt = remainder.substring(optStart, nextSpace);
          optStart = nextSpace + 1;
        }
        opt.trim();
        if (opt.length() > 0) {
          activePoll.options[activePoll.optCount++] = opt;
        }
      }
      if (activePoll.optCount < 2) {
        client->text("{\"type\":\"error\",\"text\":\"Must have at least 2 options.\"}");
        return;
      }
      activePoll.active = true;
      #if ARDUINOJSON_VERSION_MAJOR >= 7
      JsonDocument pollDoc;
      #else
      DynamicJsonDocument pollDoc(512);
      #endif
      pollDoc["type"] = "pollCreate";
      pollDoc["creator"] = activePoll.creator;
      pollDoc["room"] = activePoll.room;
      pollDoc["question"] = activePoll.question;
      #if ARDUINOJSON_VERSION_MAJOR >= 7
      JsonArray arr = pollDoc["options"].to<JsonArray>();
      #else
      JsonArray arr = pollDoc.createNestedArray("options");
      #endif
      for (int i = 0; i < activePoll.optCount; i++) {
        arr.add(activePoll.options[i]);
      }
      String pollOut;
      serializeJson(pollDoc, pollOut);
      broadcastRoom(activePoll.room, pollOut);
      sendJsonToRadio(pollOut);
      return;
    }
    
    // -- /endpoll command (admin only) ---------------------------------
    if (text == "/endpoll") {
      if (!c->isAdmin) {
        client->text("{\"type\":\"error\",\"text\":\"Admins only\"}");
        return;
      }
      if (!activePoll.active || activePoll.room != c->room) {
        client->text("{\"type\":\"error\",\"text\":\"No active poll in this room.\"}");
        return;
      }
      activePoll.active = false;
      #if ARDUINOJSON_VERSION_MAJOR >= 7
      JsonDocument endDoc;
      #else
      DynamicJsonDocument endDoc(256);
      #endif
      endDoc["type"] = "pollEnd";
      endDoc["room"] = c->room;
      String endOut;
      serializeJson(endDoc, endOut);
      broadcastRoom(c->room, endOut);
      sendJsonToRadio(endOut);
      return;
    }

    // -- /dm command -------------------------------------------------
    if (text.startsWith("/dm ")) {
      int sp = text.indexOf(' ', 4);
      String target = (sp > 0) ? text.substring(4, sp) : text.substring(4);
      String dmText = (sp > 0) ? text.substring(sp + 1) : "";
      ChatClient *tc = findClientByName(target);
      if (!tc) {
        // Try cross-node peer users
        bool peerFound = false;
        for (int i = 0; i < MAX_PEER_USERS; i++) {
          if (peerUsers[i].active && peerUsers[i].name.equalsIgnoreCase(target)) {
            peerFound = true;
            break;
          }
        }
        if (peerFound) {
          #if ARDUINOJSON_VERSION_MAJOR >= 7
          JsonDocument dmDoc;
          #else
          DynamicJsonDocument dmDoc(256);
          #endif
          dmDoc["type"] = "peerDm";
          dmDoc["to"] = target;
          dmDoc["from"] = c->username;
          dmDoc["text"] = dmText;
          dmDoc["ts"] = getTime();
          String dmOut;
          serializeJson(dmDoc, dmOut);
          sendJsonToRadio(dmOut);
          
          // echo back to sender
          #if ARDUINOJSON_VERSION_MAJOR >= 7
          JsonDocument echoDoc;
          #else
          DynamicJsonDocument echoDoc(256);
          #endif
          echoDoc["type"] = "dm";
          echoDoc["from"] = c->username;
          echoDoc["text"] = "[→ " + target + "] " + dmText;
          echoDoc["ts"] = getTime();
          String echoOut;
          serializeJson(echoDoc, echoOut);
          client->text(echoOut);
          return;
        }
        client->text("{\"type\":\"error\",\"text\":\"User not found\"}");
        return;
      }
      
#if ARDUINOJSON_VERSION_MAJOR >= 7
      JsonDocument dm;
#else
      DynamicJsonDocument dm(256);
#endif
      dm["type"] = "dm";
      dm["from"] = c->username;
      dm["text"] = dmText;
      dm["ts"]   = getTime();
      
      String dmOut;
      serializeJson(dm, dmOut);
      ws.text(tc->id, dmOut);
      client->text(dmOut);   // echo back to sender
      return;
    }

    // -- /clear command (admin) ------------------------------------
    if (text == "/clear") {
      if (!c->isAdmin) {
        client->text("{\"type\":\"error\",\"text\":\"Admins only\"}");
        return;
      }
      
      // Wipe history for this room locally
      for (uint8_t i = 0; i < Config::MAX_HISTORY; i++) {
        if (history[i].room == c->room) history[i].room = "";
      }
      
#if ARDUINOJSON_VERSION_MAJOR >= 7
      JsonDocument clr;
#else
      DynamicJsonDocument clr(128);
#endif
      clr["type"] = "clearRoom";
      clr["room"] = c->room;
      clr["mesh"] = false;
      
      String clrOut;
      serializeJson(clr, clrOut);
      broadcastRoom(c->room, clrOut);
      
      // Sync clear command over Mesh Radio
      sendJsonToRadio(clrOut);
      return;
    }

    // -- /kick command (admin) ---------------------------
    if (text.startsWith("/kick ")) {
      if (!c->isAdmin) {
        client->text("{\"type\":\"error\",\"text\":\"Admins only\"}");
        return;
      }
      String target = text.substring(6);
      target.trim();
      ChatClient *tc = findClientByName(target);
      if (!tc) {
        client->text("{\"type\":\"error\",\"text\":\"User not found\"}");
        return;
      }
      ws.text(tc->id, "{\"type\":\"kicked\",\"text\":\"You have been kicked by an administrator.\"}");
      ws.close(tc->id);
      return;
    }

    // -- /ban command (admin) ----------------------------
    if (text.startsWith("/ban ")) {
      if (!c->isAdmin) {
        client->text("{\"type\":\"error\",\"text\":\"Admins only\"}");
        return;
      }
      String target = text.substring(5);
      target.trim();
      ChatClient *tc = findClientByName(target);
      
      IPAddress targetIP = IPAddress(0,0,0,0);
      if (tc) {
        AsyncWebSocketClient *targetClient = ws.client(tc->id);
        if (targetClient) {
          targetIP = targetClient->remoteIP();
        }
      }
      
      if (!tc && targetIP == IPAddress(0,0,0,0)) {
        client->text("{\"type\":\"error\",\"text\":\"User not found online\"}");
        return;
      }
      
      addBan(targetIP, target);
      
      if (tc) {
        ws.text(tc->id, "{\"type\":\"kicked\",\"text\":\"You have been banned by an administrator.\"}");
        ws.close(tc->id);
      }
      
      client->text("{\"type\":\"system\",\"text\":\"Banned " + target + " (" + targetIP.toString() + ")\"}");
      broadcastBanList();
      
      #if ARDUINOJSON_VERSION_MAJOR >= 7
      JsonDocument syncDoc;
      #else
      DynamicJsonDocument syncDoc(128);
      #endif
      syncDoc["type"] = "syncBan";
      syncDoc["name"] = target;
      syncDoc["ip"]   = targetIP.toString();
      
      String syncOut;
      serializeJson(syncDoc, syncOut);
      sendJsonToRadio(syncOut);
      return;
    }

    // -- /unban command (admin) --------------------------
    if (text.startsWith("/unban ")) {
      if (!c->isAdmin) {
        client->text("{\"type\":\"error\",\"text\":\"Admins only\"}");
        return;
      }
      String target = text.substring(7);
      target.trim();
      
      removeBan(target);
      client->text("{\"type\":\"system\",\"text\":\"Unbanned " + target + "\"}");
      broadcastBanList();
      
      #if ARDUINOJSON_VERSION_MAJOR >= 7
      JsonDocument syncDoc;
      #else
      DynamicJsonDocument syncDoc(128);
      #endif
      syncDoc["type"] = "syncUnban";
      syncDoc["query"] = target;
      
      String syncOut;
      serializeJson(syncDoc, syncOut);
      sendJsonToRadio(syncOut);
      return;
    }

    // -- /admin command ------------------------------------------------
    if (text.startsWith("/admin ")) {
      String pw = text.substring(7);
      pw.trim();
      if (pw == Config::getAdminPass()) {
        c->isAdmin = true;
        c->adminAttempts = 0;
        client->text("{\"type\":\"adminGranted\"}");
        broadcastUserList(c->room);
      } else {
        c->adminAttempts++;
        if (c->adminAttempts >= 3) {
          client->text("{\"type\":\"error\",\"text\":\"Too many attempts. Disconnected.\"}");
          client->close();
        } else {
          client->text("{\"type\":\"error\",\"text\":\"Wrong admin password (" + String(3 - c->adminAttempts) + " left)\"}");
        }
      }
      return;
    }

    // -- /vault commands (admin only) ----------------------------------
    if (text.startsWith("/vault ")) {
      if (!c->isAdmin) { client->text("{\"type\":\"error\",\"text\":\"Admins only\"}"); return; }
      String sub = text.substring(7); sub.trim();
      if (sub == "gen" || sub == "new") {
        vaultKey = generateVaultKey();
        memset(vaultAllowed, 0, sizeof(vaultAllowed));
        for (auto& oc : clients) {
          if (oc.id && oc.room == "vault" && !oc.isAdmin) {
            ws.text(oc.id, "{\"type\":\"kicked\",\"text\":\"Vault re-keyed by admin.\"}");
            ws.close(oc.id);
          }
        }
        client->text("{\"type\":\"vaultKey\",\"key\":\"" + vaultKey + "\"}");

        // Sync over radio
        #if ARDUINOJSON_VERSION_MAJOR >= 7
        JsonDocument syncDoc;
        #else
        DynamicJsonDocument syncDoc(128);
        #endif
        syncDoc["type"] = "syncVaultKey";
        syncDoc["key"] = vaultKey;
        String syncOut; serializeJson(syncDoc, syncOut);
        sendJsonToRadio(syncOut);
      } else if (sub == "key" || sub == "show") {
        if (vaultKey.length() == 0) {
          vaultKey = generateVaultKey();
          // Sync new key over radio
          #if ARDUINOJSON_VERSION_MAJOR >= 7
          JsonDocument syncDoc;
          #else
          DynamicJsonDocument syncDoc(128);
          #endif
          syncDoc["type"] = "syncVaultKey";
          syncDoc["key"] = vaultKey;
          String syncOut; serializeJson(syncDoc, syncOut);
          sendJsonToRadio(syncOut);
        }
        client->text("{\"type\":\"vaultKey\",\"key\":\"" + vaultKey + "\"}");
      } else if (sub.startsWith("invite ")) {
        String tgt = sub.substring(7); tgt.trim();
        ChatClient* tc = findClientByName(tgt);
        if (tc) {
          if (vaultKey.length() == 0) {
            vaultKey = generateVaultKey();
            // Sync new key
            #if ARDUINOJSON_VERSION_MAJOR >= 7
            JsonDocument syncDoc;
            #else
            DynamicJsonDocument syncDoc(128);
            #endif
            syncDoc["type"] = "syncVaultKey";
            syncDoc["key"] = vaultKey;
            String syncOut; serializeJson(syncDoc, syncOut);
            sendJsonToRadio(syncOut);
          }
          ws.text(tc->id, "{\"type\":\"vaultInvite\",\"from\":\"" + c->username + "\",\"key\":\"" + vaultKey + "\"}");
          client->text("{\"type\":\"system\",\"text\":\"Vault invite sent to " + tgt + ".\",\"room\":\"vault\"}");
        } else {
          // Check if peer user exists
          bool peerFound = false;
          for (int i = 0; i < MAX_PEER_USERS; i++) {
            if (peerUsers[i].active && peerUsers[i].name.equalsIgnoreCase(tgt)) {
              peerFound = true;
              break;
            }
          }
          if (peerFound) {
            if (vaultKey.length() == 0) {
              vaultKey = generateVaultKey();
              // Sync new key
              #if ARDUINOJSON_VERSION_MAJOR >= 7
              JsonDocument syncDoc;
              #else
              DynamicJsonDocument syncDoc(128);
              #endif
              syncDoc["type"] = "syncVaultKey";
              syncDoc["key"] = vaultKey;
              String syncOut; serializeJson(syncDoc, syncOut);
              sendJsonToRadio(syncOut);
            }
            // Send peer invite
            #if ARDUINOJSON_VERSION_MAJOR >= 7
            JsonDocument inviteDoc;
            #else
            DynamicJsonDocument inviteDoc(256);
            #endif
            inviteDoc["type"] = "peerVaultInvite";
            inviteDoc["from"] = c->username;
            inviteDoc["to"] = tgt;
            inviteDoc["key"] = vaultKey;
            String inviteOut; serializeJson(inviteDoc, inviteOut);
            sendJsonToRadio(inviteOut);
            client->text("{\"type\":\"system\",\"text\":\"Vault invite sent to peer " + tgt + ".\",\"room\":\"vault\"}");
          } else {
            client->text("{\"type\":\"error\",\"text\":\"User not found\"}");
          }
        }
      } else if (sub == "lock") {
        vaultKey = "";
        memset(vaultAllowed, 0, sizeof(vaultAllowed));
        for (auto& oc : clients) {
          if (oc.id && oc.room == "vault" && !oc.isAdmin) {
            ws.text(oc.id, "{\"type\":\"kicked\",\"text\":\"Vault locked by admin.\"}");
            ws.close(oc.id);
          }
        }
        client->text("{\"type\":\"system\",\"text\":\"Vault locked.\",\"room\":\"vault\"}");

        // Sync over radio
        #if ARDUINOJSON_VERSION_MAJOR >= 7
        JsonDocument syncDoc;
        #else
        DynamicJsonDocument syncDoc(128);
        #endif
        syncDoc["type"] = "syncVaultLock";
        String syncOut; serializeJson(syncDoc, syncOut);
        sendJsonToRadio(syncOut);
      }
      return;
    }

    // -- Normal message ------------------------------------------------
    String reply = doc["reply"] | "";
    addToHistory(c->room, c->username, text, reply, false);

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument out;
#else
    DynamicJsonDocument out(512);
#endif
    out["type"]  = "msg";
    out["user"]  = c->username;
    out["text"]  = text;
    out["reply"] = reply;
    out["ts"]    = getTime();
    out["room"]  = c->room;
    out["mesh"]  = false; // Locally generated message
    
    String outStr;
    serializeJson(out, outStr);
    broadcastRoom(c->room, outStr);
    
    // Broadcast message over Mesh Radio to peer node
    sendJsonToRadio(outStr);
    return;
  }

  // -- leaveGame -----------------------------------------------------
  if (msgType == "leaveGame") {
    handleGameForfeit(c->id); removeFromQueue(c->id);
    return;
  }

  // -- gameSelect ----------------------------------------------------
  if (msgType == "gameSelect") {
    if (!gameRoom.active) return;
    if (c->id != gameRoom.playerA && c->id != gameRoom.playerB) return;
    String game = doc["game"] | "";
    if (game == "ttt" || game == "chess" || game == "rps") {
      #if ARDUINOJSON_VERSION_MAJOR >= 7
      JsonDocument out;
      #else
      DynamicJsonDocument out(256);
      #endif
      out["type"] = "msg";
      out["user"] = c->username;
      out["text"] = "GAME_SELECT:" + game;
      out["room"] = "game";
      out["ts"]   = getTime();
      out["mesh"] = false;

      String outStr;
      serializeJson(out, outStr);

      if (!gameRoom.peerPlayerA) sendGameMsg(gameRoom.playerA, outStr);
      if (!gameRoom.peerPlayerB) sendGameMsg(gameRoom.playerB, outStr);

      sendJsonToRadio(outStr);
    }
    return;
  }

  // -- gameMove ------------------------------------------------------
  if (msgType == "gameMove") {
    if (!gameRoom.active) return;
    if (c->id != gameRoom.playerA && c->id != gameRoom.playerB) return;

    String game = doc["game"] | "ttt";
    if (game == "ttt") {
      uint8_t myMark = (c->id == gameRoom.playerA) ? 0 : 1;
      if (gameRoom.turn != myMark) { client->text("{\"type\":\"error\",\"text\":\"Not your turn\"}"); return; }
      int cell = doc["cell"] | -1;
      if (cell < 0 || cell > 8 || gameRoom.board[cell] >= 0) {
        client->text("{\"type\":\"error\",\"text\":\"Invalid cell\"}"); return;
      }
      gameRoom.board[cell] = (int8_t)myMark;
      gameRoom.turn = 1 - gameRoom.turn;

      // Send this game move via radio to peer node if playing a peer match
      if (gameRoom.peerPlayerA || gameRoom.peerPlayerB) {
        sendGameMoveToRadio(cell);
      }

      int8_t winner = checkWinner(gameRoom.board);
      if (winner >= 0) {
        int8_t wl[3] = {-1,-1,-1};
        for (int i = 0; i < 8; i++) {
          if (gameRoom.board[WIN_LINES[i][0]] == winner &&
              gameRoom.board[WIN_LINES[i][1]] == winner &&
              gameRoom.board[WIN_LINES[i][2]] == winner) {
            wl[0]=WIN_LINES[i][0]; wl[1]=WIN_LINES[i][1]; wl[2]=WIN_LINES[i][2]; break;
          }
        }
        uint8_t sA = gameRoom.scoreA + (winner==0?1:0);
        uint8_t sB = gameRoom.scoreB + (winner==1?1:0);
        gameRoom.scoreA = sA; gameRoom.scoreB = sB;
        broadcastGameBoard(wl[0],wl[1],wl[2],winner);
        gameRoom.active = false;
        gameRoom.playerA = 0; gameRoom.playerB = 0;
        gameRoom.peerPlayerA = false; gameRoom.peerPlayerB = false;
        promoteQueue();
        return;
      }
      if (isBoardFull(gameRoom.board)) {
        broadcastGameBoard(-1,-1,-1,-1,true);
        gameRoom.active = false;
        gameRoom.playerA = 0; gameRoom.playerB = 0;
        gameRoom.peerPlayerA = false; gameRoom.peerPlayerB = false;
        promoteQueue();
        return;
      }
      broadcastGameBoard();
    } else if (game == "chess" || game == "rps") {
      String move = doc["move"] | "";
      #if ARDUINOJSON_VERSION_MAJOR >= 7
      JsonDocument out;
      #else
      DynamicJsonDocument out(256);
      #endif
      out["type"] = "msg";
      out["user"] = c->username;
      out["text"] = "GAME_MOVE:" + game + ":" + move;
      out["room"] = "game";
      out["ts"]   = getTime();
      out["mesh"] = false;

      String outStr;
      serializeJson(out, outStr);

      if (!gameRoom.peerPlayerA) sendGameMsg(gameRoom.playerA, outStr);
      if (!gameRoom.peerPlayerB) sendGameMsg(gameRoom.playerB, outStr);

      sendJsonToRadio(outStr);
    }
  }

  // -- gameReset (admin) ---------------------------------------------
  if (msgType == "gameReset") {
    if (!c->isAdmin) { client->text("{\"type\":\"error\",\"text\":\"Admins only\"}"); return; }
    if (!gameRoom.active) return;
    gameRoom.scoreA = 0; gameRoom.scoreB = 0;
    memset(gameRoom.board, -1, sizeof(gameRoom.board)); gameRoom.turn = 0;
    String ra = "{\"type\":\"gameStart\",\"mark\":0,\"nameA\":\"" + gameRoom.nameA + "\",\"nameB\":\"" + gameRoom.nameB + "\",\"scoreA\":0,\"scoreB\":0}";
    String rb = "{\"type\":\"gameStart\",\"mark\":1,\"nameA\":\"" + gameRoom.nameA + "\",\"nameB\":\"" + gameRoom.nameB + "\",\"scoreA\":0,\"scoreB\":0}";
    sendGameMsg(gameRoom.playerA, ra); sendGameMsg(gameRoom.playerB, rb);
    broadcastGameBoard(); return;
  }

  // (vault commands moved into the msg handler above - this block was unreachable dead code)

  // -- pubKey relay (darknet / vault E2EE) ---------------------------
  if (msgType == "pubKey") {
    String key = doc["key"] | "";
    if (key.length() == 0) return;
    bool isResp = doc["isResponse"] | false;
    String relay = "{\"type\":\"pubKey\",\"from\":\"" + c->username + "\",\"key\":\"" + key + "\",\"isResponse\":" + (isResp ? "true" : "false") + "}";
    for (auto& other : clients) {
      if (other.id && other.id != c->id && other.room == c->room) {
        ws.text(other.id, relay);
      }
    }
    // Relay over radio
    #if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument radioDoc;
    #else
    DynamicJsonDocument radioDoc(512);
    #endif
    radioDoc["type"] = "peerPubKey";
    radioDoc["from"] = c->username;
    radioDoc["key"]  = key;
    radioDoc["room"] = c->room;
    radioDoc["isResponse"] = isResp;
    String radioOut;
    serializeJson(radioDoc, radioOut);
    sendJsonToRadio(radioOut);
    return;
  }

  // -- pollVote ------------------------------------------------------
  if (msgType == "pollVote") {
    if (!activePoll.active || activePoll.room != c->room) return;
    uint8_t optIdx = doc["option"] | 99;
    if (optIdx >= activePoll.optCount) return;
    
    bool alreadyVoted = false;
    for (int i = 0; i < activePoll.votedCount; i++) {
      if (activePoll.votedNames[i].equalsIgnoreCase(c->username)) {
        alreadyVoted = true;
        break;
      }
    }
    if (alreadyVoted) {
      client->text("{\"type\":\"error\",\"text\":\"You have already voted!\"}");
      return;
    }
    
    activePoll.votedNames[activePoll.votedCount++] = c->username;
    activePoll.votes[optIdx]++;
    
    #if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument voteDoc;
    #else
    DynamicJsonDocument voteDoc(512);
    #endif
    voteDoc["type"] = "pollUpdate";
    voteDoc["room"] = activePoll.room;
    #if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonArray votesArr = voteDoc["votes"].to<JsonArray>();
    #else
    JsonArray votesArr = voteDoc.createNestedArray("votes");
    #endif
    for (int i = 0; i < activePoll.optCount; i++) {
      votesArr.add(activePoll.votes[i]);
    }
    
    String voteOut;
    serializeJson(voteDoc, voteOut);
    broadcastRoom(activePoll.room, voteOut);
    sendJsonToRadio(voteOut);
    return;
  }

  // -- pinMsg (admin only) -------------------------------------------
  if (msgType == "pinMsg") {
    if (!c->isAdmin) return;
    int rIdx = roomIndex(c->room);
    if (rIdx < 0) return;
    
    bool unpin = doc["unpin"] | false;
    if (unpin) {
      pinnedMsgs[rIdx].active = false;
      pinnedMsgs[rIdx].text = "";
      pinnedMsgs[rIdx].from = "";
      pinnedMsgs[rIdx].ts = "";
    } else {
      pinnedMsgs[rIdx].text = doc["text"] | "";
      pinnedMsgs[rIdx].from = doc["from"] | "";
      pinnedMsgs[rIdx].ts = doc["ts"] | "";
      pinnedMsgs[rIdx].active = true;
    }
    
    #if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument pinDoc;
    #else
    DynamicJsonDocument pinDoc(512);
    #endif
    pinDoc["type"] = "pinnedMsg";
    pinDoc["room"] = c->room;
    pinDoc["text"] = pinnedMsgs[rIdx].text;
    pinDoc["from"] = pinnedMsgs[rIdx].from;
    pinDoc["ts"] = pinnedMsgs[rIdx].ts;
    pinDoc["active"] = pinnedMsgs[rIdx].active;
    
    String pinOut;
    serializeJson(pinDoc, pinOut);
    broadcastRoom(c->room, pinOut);
    sendJsonToRadio(pinOut);
    return;
  }

  // -- reaction --------------------------------------------------------
  if (msgType == "react") {
    String targetUser = doc["targetUser"] | "";
    String targetText = doc["targetText"] | "";
    String emoji      = doc["emoji"] | "";
    
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument out;
#else
    DynamicJsonDocument out(256);
#endif
    out["type"]       = "react";
    out["from"]       = c->username;
    out["targetUser"] = targetUser;
    out["targetText"] = targetText;
    out["emoji"]      = emoji;
    out["room"]       = c->room;
    out["mesh"]       = false; // Locally generated reaction
    
    String outStr;
    serializeJson(out, outStr);
    broadcastRoom(c->room, outStr);
    
    // Broadcast reaction over Mesh Radio
    sendJsonToRadio(outStr);
    return;
  }
}

// -- WebSocket Event Listener --------------------------------------

void onWsEvent(AsyncWebSocket *srv, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {

  uint32_t cid = client->id();

  // Connect Handler
  if (type == WS_EVT_CONNECT) {
    if (isBanned(client->remoteIP(), "")) {
      client->text("{\"type\":\"error\",\"text\":\"BANNED\"}");
      client->close();
      return;
    }
    int slot = freeSlot();
    if (slot < 0) {
      client->text("{\"type\":\"error\",\"text\":\"Node A is full (4/4). Connect to PURPLE-CHAT-B instead!\"}");
      client->close();
      return;
    }
    clients[slot].id       = cid;
    clients[slot].username = "";
    clients[slot].room     = "comms";            // Default room is comms
    clients[slot].msgCount = 0;
    clients[slot].isAdmin  = false;
    clients[slot].wsBuffer  = "";
    clients[slot].lastActivity = millis();
    clients[slot].adminAttempts = 0;
    client->text("{\"type\":\"needName\"}");
  }

  // Disconnect Handler
  if (type == WS_EVT_DISCONNECT) {
    ChatClient *c = findClient(cid);
    if (c && c->username.length()) {
      String room = c->room;
      // Game forfeit + queue cleanup
      handleGameForfeit(cid);
      removeFromQueue(cid);
      // Vault access cleanup
      if (room == "vault" && !c->isAdmin) removeVaultAllowed(cid);
      // Chat broadcast (skip for game room)
      if (room != "game") {
#if ARDUINOJSON_VERSION_MAJOR >= 7
        JsonDocument doc;
#else
        DynamicJsonDocument doc(128);
#endif
        doc["type"] = "system";
        doc["text"] = c->username + " left the room";
        doc["room"] = room;
        String out; serializeJson(doc, out);
        broadcastRoom(room, out);
      }
      // Broadcast disconnect to Radio Mesh
      #if ARDUINOJSON_VERSION_MAJOR >= 7
      JsonDocument syncDoc;
      #else
      DynamicJsonDocument syncDoc(128);
      #endif
      syncDoc["type"] = "peerDisconnect";
      syncDoc["name"] = c->username;
      String syncOut;
      serializeJson(syncDoc, syncOut);
      sendJsonToRadio(syncOut);

      removeClient(cid);
      broadcastUserList(room);
    } else {
      removeClient(cid);
    }
  }

  // Raw WebSocket data chunk (Fragmentation-Aware)
  if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->opcode != WS_TEXT) return;

    ChatClient *c = findClient(cid);
    if (!c) return;

    // Reset buffer on the first chunk of a WebSocket frame
    if (info->index == 0) {
      c->wsBuffer = "";
      if (info->len > 32768) { // Guard against buffer exhaustion
        client->text("{\"type\":\"error\",\"text\":\"Data payload too large\"}");
        return;
      }
      c->wsBuffer.reserve(info->len);
    }

    // Accumulate the current chunk
    for (size_t i = 0; i < len; i++) {
      c->wsBuffer += (char)data[i];
    }

    // Guard against multi-chunk accumulation overflow (not just first-chunk size)
    if (c->wsBuffer.length() > 32768) {
      client->text("{\"type\":\"error\",\"text\":\"Payload too large\"}");
      c->wsBuffer = "";
      return;
    }

    // Process when message reassembly completes
    if (info->index + len == info->len) {
      if (c->wsBuffer.startsWith("{\"type\":\"voice\"")) {
        // Forward voice chunk directly to all other clients in the same room
        for (auto &other : clients) {
          if (other.id && other.id != c->id && other.room == c->room) {
            ws.text(other.id, c->wsBuffer);
          }
        }
      } else {
        processWsMessage(client, c, c->wsBuffer);
      }
      c->wsBuffer = ""; // Reset buffer
    }
  }
}

// -- HTML (Cyberpunk Glassmorphism Web UI stored in flash) ----

#include "index_html.h"

#include "manifest_json.h"

#include "sw_js.h"

// -- nRF24L01 Radio Initialization & Transmission Helpers --------

void initRadio() {
  radioMutex = xSemaphoreCreateMutex(); // Create SPI guard mutex
  SPI.begin(); // Standard VSPI initialization
  
  if (!radio.begin()) {
    Serial.println("nRF24L01 Hardware initialization failed!");
    return;
  }
  
  radio.setPALevel(Config::NRF_PA_LEVEL);         // Power level (LOW to prevent brownouts)
  radio.setDataRate(RF24_250KBPS);                // 250 Kbps = maximum range (~2x improvement over 1Mbps)
  radio.setChannel(Config::NRF_CHANNEL);          // High-frequency isolation
  radio.setRetries(15, 15);                       // 15 retries, 4ms delay = best reliability at long range
  radio.enableDynamicPayloads();
  
  // Symmetrical read/write pipe mapping configured for Node A
  radio.openWritingPipe(RADIO_PIPE_1);
  radio.openReadingPipe(1, RADIO_PIPE_2);
  
  radio.startListening();
  Serial.println("nRF24L01 Bridge configured successfully on Channel " + String(Config::NRF_CHANNEL));
}

void sendJsonToRadio(const String &jsonStr) {
  int len = jsonStr.length();
  if (len == 0) return;

  // Enqueue all chunks -- actual transmission happens in drainTxQueue() each loop()
  uint8_t totalChunks = (len + 26) / 27;
  uint8_t msgId = nextMsgId++;

  for (uint8_t i = 0; i < totalChunks; i++) {
    RadioPacket packet;
    packet.header.type        = 1;
    packet.header.msgId       = msgId;
    packet.header.chunkIdx    = i;
    packet.header.totalChunks = totalChunks;

    int offset   = i * 27;
    int chunkLen = min(27, len - offset);
    packet.header.payloadLen  = chunkLen;
    memcpy(packet.data, jsonStr.c_str() + offset, chunkLen);

    enqueueTxPacket(packet); // NON-BLOCKING -- no delay, no stopListening here
  }
}

void checkRadioKeepalive() {
  static uint32_t lastPingTime = 0;
  uint32_t now = millis();

  // If the radio hardware is not connected/responsive, force offline
  if (!radio.isChipConnected()) {
    if (peerOnline) {
      peerOnline = false;
      broadcastLinkStatus();
    }
    return;
  }

  // Enqueue a keepalive ping every 3 seconds (no blocking)
  if (now - lastPingTime > 3000) {
    lastPingTime = now;

    RadioPacket pingPacket;
    memset(&pingPacket, 0, sizeof(pingPacket));
    pingPacket.header.type        = 0;
    pingPacket.header.totalChunks = 1;
    enqueueTxPacket(pingPacket);
  }

  // Peer offline detection (10-second timeout -- generous for queue drain latency)
  if (peerOnline && (now - lastPeerContact > 10000)) {
    peerOnline = false;
    broadcastLinkStatus();
  }
}

void handleRadioData() {
  // If the radio hardware is not connected/responsive, don't read garbage
  if (!radio.isChipConnected()) {
    if (peerOnline) {
      peerOnline = false;
      broadcastLinkStatus();
    }
    return;
  }

  if (!radio.available()) return;
  
  RadioPacket packet;
  radio.read(&packet, sizeof(packet));
  
  // Validate packet type first to ignore garbage / floating MISO reads
  if (packet.header.type != 0 && packet.header.type != 1) {
    return;
  }
  
  uint32_t now = millis();
  
  // Any valid packet received means the peer is online
  lastPeerContact = now;
  if (!peerOnline) {
    peerOnline = true;
    broadcastLinkStatus();
  }
  
  // 1. Keepalive Ping Packets
  if (packet.header.type == 0) {
    packetsReceived++;
    return;
  }
  
  // 2. Chat/JSON Stream Packets
  if (packet.header.type == 1) {
    packetsReceived++;
    
    // Check if packet belongs to active sequence or times out (2 seconds)
    if (rxBuf.msgId != packet.header.msgId || (now - rxBuf.lastActivity > 2000)) {
      rxBuf.msgId = packet.header.msgId;
      rxBuf.totalChunks = packet.header.totalChunks;
      rxBuf.chunksReceivedMask = 0;
      memset(rxBuf.data, 0, sizeof(rxBuf.data));
    }
    
    rxBuf.lastActivity = now;
    
    uint8_t chunkIdx = packet.header.chunkIdx;
    if (chunkIdx < 32 && chunkIdx < rxBuf.totalChunks) {
      rxBuf.chunksReceivedMask |= (1UL << chunkIdx);
      
      int offset = chunkIdx * 27;
      if (offset + packet.header.payloadLen < RX_BUF_SIZE) {
        memcpy(rxBuf.data + offset, packet.data, packet.header.payloadLen);
      }
      
      // Check if complete packet array is received
      uint32_t expectedMask = (1UL << rxBuf.totalChunks) - 1;
      if ((rxBuf.chunksReceivedMask & expectedMask) == expectedMask) {
        processRadioMessage(rxBuf.data);
        rxBuf.msgId = 0xFF; // Reset reassembly context
      }
    }
  }
}

void processRadioMessage(const char* jsonStr) {
#if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument doc;
#else
  DynamicJsonDocument doc(512);
#endif

  if (deserializeJson(doc, jsonStr)) return;
  
  String type = doc["type"] | "";

  // Deduplicate regular chat messages to prevent loop echos
  if (type == "msg") {
    String user = doc["user"] | "";
    String text = doc["text"] | "";
    uint16_t h = msgHash(user, text);
    if (isDuplicate(h)) return;
  }
  
  if (type == "msg") {
    String room  = doc["room"] | "comms";
    String user  = doc["user"] | "";
    String text  = doc["text"] | "";
    String reply = doc["reply"] | "";
    
    addToHistory(room, user, text, reply, true); // Log as Mesh-origin message
    
    // Inject Mesh status flag and forward to WebSocket clients
    doc["mesh"] = true;
    String out;
    serializeJson(doc, out);
    broadcastRoom(room, out);
  }
  else if (type == "react") {
    String room = doc["room"] | "comms";
    doc["mesh"] = true;
    
    String out;
    serializeJson(doc, out);
    broadcastRoom(room, out);
  }
  else if (type == "clearRoom") {
    String room = doc["room"] | "comms";
    for (uint8_t i = 0; i < Config::MAX_HISTORY; i++) {
      if (history[i].room == room) history[i].room = "";
    }
    
    doc["mesh"] = true;
    String out;
    serializeJson(doc, out);
    broadcastRoom(room, out);
  }
  else if (type == "syncBan") {
    String name = doc["name"] | "";
    String ipStr = doc["ip"] | "";
    IPAddress ip;
    ip.fromString(ipStr);
    addBan(ip, name);
    
    ChatClient *tc = findClientByName(name);
    if (tc) {
      ws.text(tc->id, "{\"type\":\"kicked\",\"text\":\"You have been banned by an administrator.\"}");
      ws.close(tc->id);
    }
    broadcastBanList();
  }
  else if (type == "syncUnban") {
    String query = doc["query"] | "";
    removeBan(query);
    broadcastBanList();
  }
  else if (type == "syncVaultKey") {
    String key = doc["key"] | "";
    if (key.length() > 0) {
      vaultKey = key;
      memset(vaultAllowed, 0, sizeof(vaultAllowed));
      for (auto& oc : clients) {
        if (oc.id && oc.room == "vault" && !oc.isAdmin) {
          ws.text(oc.id, "{\"type\":\"kicked\",\"text\":\"Vault re-keyed by admin.\"}");
          ws.close(oc.id);
        }
      }
    }
  }
  else if (type == "syncVaultLock") {
    vaultKey = "";
    memset(vaultAllowed, 0, sizeof(vaultAllowed));
    for (auto& oc : clients) {
      if (oc.id && oc.room == "vault" && !oc.isAdmin) {
        ws.text(oc.id, "{\"type\":\"kicked\",\"text\":\"Vault locked by admin.\"}");
        ws.close(oc.id);
      }
    }
  }
  else if (type == "peerVaultInvite") {
    String from = doc["from"] | "Admin";
    String to = doc["to"] | "";
    String key = doc["key"] | "";
    if (to.length() > 0 && key.length() > 0) {
      ChatClient* tc = findClientByName(to);
      if (tc) {
        ws.text(tc->id, "{\"type\":\"vaultInvite\",\"from\":\"" + from + "\",\"key\":\"" + key + "\"}");
      }
    }
  }
  else if (type == "peerPresence") {
    String name = doc["name"] | "";
    String room = doc["room"] | "";
    bool isAdmin = doc["isAdmin"] | false;
    if (name.length() > 0) {
      addPeerUser(name, room, isAdmin);
      broadcastUserList(room);
    }
  }
  else if (type == "peerPresenceBatch") {
    // Batch presence update -- one packet instead of N per-user packets
    JsonArray arr = doc["users"].as<JsonArray>();
    String lastRoom = "";
    for (JsonObject u : arr) {
      String name    = u["name"] | "";
      String room    = u["room"] | "comms";
      bool   isAdmin = u["isAdmin"] | false;
      if (name.length() > 0) {
        addPeerUser(name, room, isAdmin);
        if (room != lastRoom) { broadcastUserList(room); lastRoom = room; }
      }
    }
  }
  else if (type == "peerDisconnect") {
    String name = doc["name"] | "";
    if (name.length() > 0) {
      removePeerUser(name);
      // Only broadcast rooms that actually have local clients (avoids burst of 6 JSON allocs)
      const char* rooms[] = {"comms","airwaves","terminal","game","darknet","vault"};
      for (auto &room : rooms) {
        bool hasClients = false;
        for (auto &c : clients) { if (c.id && c.room == room) { hasClients = true; break; } }
        if (hasClients) broadcastUserList(room);
      }
    }
  }
  else if (type == "peerGameLobby") {
    String peerName = doc["name"] | "Opponent";
    // Check if we have someone in our local game room waiting
    if (gameRoom.waitingForP2 && !gameRoom.active && gameRoom.playerA != 0) {
      // Pair local player A with the incoming peer challenger (as player B)
      startGame(gameRoom.playerA, gameRoom.nameA, 0, peerName, 0, 0, false, true);
      
      // Send handshake back over radio to let peer node know match has started
      #if ARDUINOJSON_VERSION_MAJOR >= 7
      JsonDocument ack;
      #else
      DynamicJsonDocument ack(256);
      #endif
      ack["type"] = "peerGameStartAck";
      ack["nameA"] = gameRoom.nameA;
      ack["nameB"] = peerName;
      String out; serializeJson(ack, out);
      sendJsonToRadio(out);
    }
  }
  else if (type == "peerGameStartAck") {
    String nameA = doc["nameA"] | "Opponent";
    String nameB = doc["nameB"] | "LocalPlayer";
    // If we were waiting in lobby, start game with peer as player A and local as player B
    if (gameRoom.waitingForP2 && !gameRoom.active && gameRoom.playerA != 0) {
      startGame(0, nameA, gameRoom.playerA, nameB, 0, 0, true, false);
    }
  }
  else if (type == "peerGameMove") {
    if (gameRoom.active) {
      int cell = doc["cell"] | -1;
      if (cell >= 0 && cell <= 8 && gameRoom.board[cell] < 0) {
        // Play the move for the active turn player
        gameRoom.board[cell] = (int8_t)gameRoom.turn;
        gameRoom.turn = 1 - gameRoom.turn;
        
        int8_t winner = checkWinner(gameRoom.board);
        if (winner >= 0) {
          int8_t wl[3] = {-1,-1,-1};
          for (int i = 0; i < 8; i++) {
            if (gameRoom.board[WIN_LINES[i][0]] == winner &&
                gameRoom.board[WIN_LINES[i][1]] == winner &&
                gameRoom.board[WIN_LINES[i][2]] == winner) {
              wl[0]=WIN_LINES[i][0]; wl[1]=WIN_LINES[i][1]; wl[2]=WIN_LINES[i][2]; break;
            }
          }
          uint8_t sA = gameRoom.scoreA + (winner==0?1:0);
          uint8_t sB = gameRoom.scoreB + (winner==1?1:0);
          gameRoom.scoreA = sA; gameRoom.scoreB = sB;
          broadcastGameBoard(wl[0],wl[1],wl[2],winner);
          gameRoom.active = false;
          gameRoom.playerA = 0; gameRoom.playerB = 0;
          gameRoom.peerPlayerA = false; gameRoom.peerPlayerB = false;
          promoteQueue();
        } else if (isBoardFull(gameRoom.board)) {
          broadcastGameBoard(-1,-1,-1,-1,true);
          gameRoom.active = false;
          gameRoom.playerA = 0; gameRoom.playerB = 0;
          gameRoom.peerPlayerA = false; gameRoom.peerPlayerB = false;
          promoteQueue();
        } else {
          broadcastGameBoard();
        }
      }
    }
  }
  else if (type == "peerGameForfeit") {
    if (gameRoom.active) {
      String loser = doc["loser"] | "Opponent";
      uint8_t sA = doc["scoreA"] | 0;
      uint8_t sB = doc["scoreB"] | 0;
      
      uint32_t localId = gameRoom.peerPlayerA ? gameRoom.playerB : gameRoom.playerA;
      sendGameMsg(localId, "{\"type\":\"gameForfeit\",\"loser\":\"" + loser + "\",\"scoreA\":" + String(sA) + ",\"scoreB\":" + String(sB) + "}");
      
      gameRoom.active = false;
      gameRoom.playerA = 0; gameRoom.playerB = 0;
      gameRoom.peerPlayerA = false; gameRoom.peerPlayerB = false;
      memset(gameRoom.board, -1, sizeof(gameRoom.board)); gameRoom.turn = 0;
      promoteQueue();
    }
  }
  else if (type == "peerGameLeaveLobby") {
    // If we were waiting for peer, clear waiting status
    if (gameRoom.waitingForP2 && (gameRoom.peerPlayerA || gameRoom.playerB == 0)) {
      // Revert to waiting lobby or check queue
      gameRoom.waitingForP2 = false;
      promoteQueue();
    }
  }
  else if (type == "peerPubKey") {
    String from = doc["from"] | "";
    String key  = doc["key"]  | "";
    String room = doc["room"] | "";
    bool isResp = doc["isResponse"] | false;
    if (from.length() > 0 && key.length() > 0 && room.length() > 0) {
      String relay = "{\"type\":\"pubKey\",\"from\":\"" + from + "\",\"key\":\"" + key + "\",\"isResponse\":" + (isResp ? "true" : "false") + "}";
      for (auto& c : clients) {
        if (c.id && c.room == room) ws.text(c.id, relay);
      }
    }
  }
  else if (type == "peerDm") {
    String to   = doc["to"] | "";
    String from = doc["from"] | "";
    String text = doc["text"] | "";
    String ts   = doc["ts"] | "";
    if (to.length() > 0) {
      ChatClient* tc = findClientByName(to);
      if (tc) {
        #if ARDUINOJSON_VERSION_MAJOR >= 7
        JsonDocument dm;
        #else
        DynamicJsonDocument dm(256);
        #endif
        dm["type"] = "dm";
        dm["from"] = from;
        dm["text"] = text;
        dm["ts"]   = ts;
        String dmOut;
        serializeJson(dm, dmOut);
        ws.text(tc->id, dmOut);
      }
    }
  }
  else if (type == "peerTyping") {
    String user  = doc["user"] | "";
    bool   state = doc["state"] | false;
    String room  = doc["room"] | "";
    if (user.length() > 0 && room.length() > 0) {
      #if ARDUINOJSON_VERSION_MAJOR >= 7
      JsonDocument tDoc;
      #else
      DynamicJsonDocument tDoc(128);
      #endif
      tDoc["type"]  = "typing";
      tDoc["user"]  = user;
      tDoc["state"] = state;
      tDoc["room"]  = room;
      String tOut;
      serializeJson(tDoc, tOut);
      for (auto &other : clients) {
        if (other.id && other.room == room) {
          ws.text(other.id, tOut);
        }
      }
    }
  }
  else if (type == "pollCreate") {
    activePoll.creator = doc["creator"] | "";
    activePoll.room = doc["room"] | "";
    activePoll.question = doc["question"] | "";
    activePoll.active = true;
    activePoll.optCount = 0;
    activePoll.votedCount = 0;
    memset(activePoll.votes, 0, sizeof(activePoll.votes));
    for (int i = 0; i < 16; i++) activePoll.votedNames[i] = "";
    
    JsonArray arr = doc["options"].as<JsonArray>();
    for (String opt : arr) {
      if (activePoll.optCount < 4) {
        activePoll.options[activePoll.optCount++] = opt;
      }
    }
    broadcastRoom(activePoll.room, jsonStr);
  }
  else if (type == "pollUpdate") {
    String room = doc["room"] | "";
    if (activePoll.active && activePoll.room == room) {
      JsonArray votesArr = doc["votes"].as<JsonArray>();
      uint8_t idx = 0;
      for (uint8_t v : votesArr) {
        if (idx < activePoll.optCount) {
          activePoll.votes[idx++] = v;
        }
      }
      broadcastRoom(room, jsonStr);
    }
  }
  else if (type == "pollEnd") {
    String room = doc["room"] | "";
    if (activePoll.active && activePoll.room == room) {
      activePoll.active = false;
      broadcastRoom(room, jsonStr);
    }
  }
  else if (type == "pinnedMsg") {
    String room = doc["room"] | "";
    int rIdx = roomIndex(room);
    if (rIdx >= 0) {
      bool active = doc["active"] | false;
      pinnedMsgs[rIdx].active = active;
      if (active) {
        pinnedMsgs[rIdx].text = doc["text"] | "";
        pinnedMsgs[rIdx].from = doc["from"] | "";
        pinnedMsgs[rIdx].ts = doc["ts"] | "";
      } else {
        pinnedMsgs[rIdx].text = "";
        pinnedMsgs[rIdx].from = "";
        pinnedMsgs[rIdx].ts = "";
      }
      broadcastRoom(room, jsonStr);
    }
  }
}

void broadcastLinkStatus() {
#if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument doc;
#else
  DynamicJsonDocument doc(256);
#endif

  // Check if hardware is responsive
  bool hardwareOk = radio.isChipConnected();

  doc["type"]       = "radioStatus";
  // The link is only online if hardware is OK and we have contact with peer
  doc["online"]     = hardwareOk && peerOnline;
  doc["localNode"]  = String(NODE_ID);
  doc["peerNode"]   = String(NODE_ID == 'A' ? 'B' : 'A');
  doc["txCount"]    = packetsSent;
  doc["rxCount"]    = packetsReceived;
  
  uint8_t quality = 100;
  if (!hardwareOk || !peerOnline) {
    quality = 0;
  } else {
    uint32_t totalTx = packetsSent + packetsFailed;
    if (totalTx > 0) {
      quality = (packetsSent * 100) / totalTx;
    }
  }
  doc["quality"]    = quality;
  
  String out;
  serializeJson(doc, out);
  ws.textAll(out);
}

void broadcastBanList() {
  #if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument doc;
  #else
  DynamicJsonDocument doc(1024);
  #endif
  doc["type"] = "banList";
  
  #if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonArray arr = doc["bans"].to<JsonArray>();
  for (int i = 0; i < MAX_BANS; i++) {
    if (banList[i].active) {
      JsonObject obj = arr.add<JsonObject>();
      obj["name"] = banList[i].username;
      obj["ip"] = banList[i].ip.toString();
    }
  }
  #else
  JsonArray arr = doc.createNestedArray("bans");
  for (int i = 0; i < MAX_BANS; i++) {
    if (banList[i].active) {
      JsonObject obj = arr.createNestedObject();
      obj["name"] = banList[i].username;
      obj["ip"] = banList[i].ip.toString();
    }
  }
  #endif

  String out;
  serializeJson(doc, out);
  for (auto &c : clients) {
    if (c.id && c.isAdmin) {
      ws.text(c.id, out);
    }
  }
}

// -- Setup ---------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(100);
  randomSeed(esp_random()); // True hardware RNG seed
  pinMode(2, OUTPUT); // Built-in LED status indicator

  Serial.println("--- Node A Setup Start ---");
  
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Disable brownout detector
  Serial.println("--- Brownout disabled ---");

  // Initialize memory structures safely (constructor constructs strings)
  for (int i = 0; i < Config::MAX_CLIENTS; i++) {
    clients[i].id = 0;
    clients[i].username = "";
    clients[i].room = "comms";
    clients[i].lastMsgTs = 0;
    clients[i].msgCount = 0;
    clients[i].isAdmin = false;
    clients[i].wsBuffer = "";
    clients[i].lastActivity = 0;
    clients[i].adminAttempts = 0;
  }
  for (int i = 0; i < 6; i++) {
    pinnedMsgs[i].text = "";
    pinnedMsgs[i].from = "";
    pinnedMsgs[i].ts = "";
    pinnedMsgs[i].active = false;
  }
  loadBansFromNVS(); // Load bans from flash
  Serial.println("--- Memory structures cleared and bans loaded ---");

  // Initialize nRF24L01 Radio Configuration
  Serial.println("--- Initializing Radio ---");
  initRadio();
  delay(500); // Stagger power startup

  // Setup Wi-Fi Soft Access Point Configuration
  IPAddress local_IP(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  
  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.softAP(Config::getSSID().c_str(), Config::getAPPass().c_str(), 6, 0, Config::MAX_CLIENTS);
  WiFi.setTxPower(WIFI_POWER_11dBm); // Reduce Wi-Fi TX power to prevent high peak current draw
  
  // Start the DNS server for captive portal
  dnsServer.start(53, "*", local_IP);
  
  Serial.print("Node A Wi-Fi SSID: ");
  Serial.println(Config::getSSID());
  Serial.print("Node A AP IP Address: ");
  Serial.println(WiFi.softAPIP());

  // WebSocket Server setup
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  // HTTP Routes setup - send_P with explicit length (most reliable for PROGMEM on ESP32)
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req){
    req->send_P(200, "text/html", (const uint8_t*)index_html, index_html_len);
  });



  server.on("/manifest.json", HTTP_GET, [](AsyncWebServerRequest *req){
    req->send_P(200, "application/json", (const uint8_t*)manifest_json, manifest_json_len);
  });

  server.on("/sw.js", HTTP_GET, [](AsyncWebServerRequest *req){
    req->send_P(200, "application/javascript", (const uint8_t*)sw_js, sw_js_len);
  });

  server.on("/auth", HTTP_GET, [](AsyncWebServerRequest *req){
    if (!req->client()) {
      AsyncWebServerResponse *response = req->beginResponse(200, "text/plain", "NO");
      response->addHeader("Access-Control-Allow-Origin", "*");
      req->send(response);
      return;
    }
    IPAddress clientIP = req->client()->remoteIP();
    if (isBanned(clientIP, "")) {
      AsyncWebServerResponse *response = req->beginResponse(200, "text/plain", "BANNED");
      response->addHeader("Access-Control-Allow-Origin", "*");
      req->send(response);
      return;
    }
    String res = "NO";
    if (req->hasParam("key")) {
      String key = req->getParam("key")->value();
      if (key == Config::getAdminPass()) {
        res = "ADMIN";
      } else if (key == Config::getWebPass()) {
        res = "OK";
      }
    }
    AsyncWebServerResponse *response = req->beginResponse(200, "text/plain", res);
    response->addHeader("Access-Control-Allow-Origin", "*");
    req->send(response);
  });

  server.onNotFound([](AsyncWebServerRequest *req){
    req->redirect("http://192.168.4.1/");
  });

  server.begin();
  Serial.println("Node A Chat Server listening on port 80");
}

// ====== Loop ======

void loop() {
  dnsServer.processNextRequest();

  uint32_t now = millis();

  // Drain ONE queued radio packet per loop iteration -- never blocks AsyncTCP
  drainTxQueue();

  // WebSocket server-side ping every 20s -- keeps browser connections alive
  static uint32_t lastWsPing = 0;
  if (now - lastWsPing > 20000) {
    lastWsPing = now;
    ws.pingAll();
  }

  // WebSocket cleanup every 5 seconds (was 1s -- too aggressive)
  static uint32_t lastCleanup = 0;
  if (now - lastCleanup > 5000) {
    ws.cleanupClients(Config::MAX_CLIENTS);
    cleanOfflinePeers();

    // Client Inactivity Timeout (3-minute ghost client killer)
    for (auto& c : clients) {
      if (c.id && c.lastActivity > 0 && (now - c.lastActivity > 180000)) {
        ws.close(c.id);
      }
    }
    lastCleanup = now;
  }

  // Broadcast ALL local users in ONE combined radio packet every 5 seconds
  // (was: one packet per user per 3s with delayMicroseconds -- caused loop blocking)
  static uint32_t lastUserBroadcast = 0;
  if (now - lastUserBroadcast > 5000) {
    lastUserBroadcast = now;
    bool anyUser = false;
    for (auto &c : clients) { if (c.id && c.username.length() > 0) { anyUser = true; break; } }
    if (anyUser) {
      #if ARDUINOJSON_VERSION_MAJOR >= 7
      JsonDocument syncDoc;
      #else
      DynamicJsonDocument syncDoc(512);
      #endif
      syncDoc["type"] = "peerPresenceBatch";
      #if ARDUINOJSON_VERSION_MAJOR >= 7
      JsonArray arr = syncDoc["users"].to<JsonArray>();
      for (auto &c : clients) {
        if (c.id && c.username.length() > 0) {
          JsonObject u = arr.add<JsonObject>();
          u["name"]    = c.username;
          u["room"]    = c.room;
          u["isAdmin"] = c.isAdmin;
        }
      }
      #else
      JsonArray arr = syncDoc.createNestedArray("users");
      for (auto &c : clients) {
        if (c.id && c.username.length() > 0) {
          JsonObject u = arr.createNestedObject();
          u["name"]    = c.username;
          u["room"]    = c.room;
          u["isAdmin"] = c.isAdmin;
        }
      }
      #endif
      String syncOut;
      serializeJson(syncDoc, syncOut);
      sendJsonToRadio(syncOut); // Enqueued, not blocking
    }
  }

  // Auto radio re-initialization on chip disconnect (e.g. brownout recovery)
  static uint32_t lastRadioRetry = 0;
  if (!radio.isChipConnected() && now - lastRadioRetry > 15000) {
    lastRadioRetry = now;
    initRadio();
  }

  // LED Status Indicator blinks (GPIO 2)
  static uint32_t lastLed = 0;
  static bool ledState = false;
  bool anyClient = false;
  for (auto& c : clients) if (c.id) { anyClient = true; break; }
  bool hardwareOk = radio.isChipConnected();
  bool linked = hardwareOk && peerOnline;

  if (anyClient && linked) {
    digitalWrite(2, HIGH);               // Solid ON = all working
    ledState = true;
  } else if (!anyClient) {
    // Fast blink (100ms ON / 100ms OFF) when idle/no connections
    if (now - lastLed > 100) {
      ledState = !ledState;
      digitalWrite(2, ledState ? HIGH : LOW);
      lastLed = now;
    }
  } else {
    // Slow blink (1000ms ON / 1000ms OFF) when radio offline or link down
    if (now - lastLed > 1000) {
      ledState = !ledState;
      digitalWrite(2, ledState ? HIGH : LOW);
      lastLed = now;
    }
  }

  // Poll keepalive check
  checkRadioKeepalive();

  // Poll nRF24L01 incoming data queues
  handleRadioData();
}
//1781 1847 1543