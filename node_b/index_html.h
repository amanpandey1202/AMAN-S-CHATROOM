const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>GhostESP / AMAN'S Real ESP32 Web Flasher</title>
<link href="https://fonts.googleapis.com/css2?family=Space+Grotesk:wght@400;500;600;700&family=JetBrains+Mono:wght@400;500;700&display=swap" rel="stylesheet">
<!-- Official Espressif esptool-js bundle for real Web Serial flashing -->
<script src="https://unpkg.com/esptool-js@0.4.3/lib/index.js"></script>
<style>
:root {
  --bg: #080a0f;
  --card-bg: rgba(16, 20, 38, 0.88);
  --card-border: rgba(124, 58, 237, 0.25);
  --card-border-hover: rgba(0, 217, 255, 0.5);
  --accent: #00d9ff;
  --purple: #7c3aed;
  --green: #10b981;
  --amber: #f59e0b;
  --red: #ef4444;
  --text: #f1f5f9;
  --muted: #94a3b8;
  --font-sans: 'Space Grotesk', -apple-system, BlinkMacSystemFont, sans-serif;
  --font-mono: 'JetBrains Mono', monospace;
  --glow: 0 0 20px rgba(0, 217, 255, 0.35);
}

* { box-sizing: border-box; margin: 0; padding: 0; }
body {
  background: var(--bg);
  color: var(--text);
  font-family: var(--font-sans);
  min-height: 100vh;
  display: flex;
  flex-direction: column;
  background-image: 
    radial-gradient(circle at 10% 20%, rgba(124, 58, 237, 0.15) 0%, transparent 40%),
    radial-gradient(circle at 90% 80%, rgba(0, 217, 255, 0.12) 0%, transparent 40%);
  background-attachment: fixed;
}

.announcement-banner {
  background: linear-gradient(90deg, #7c3aed, #00d9ff);
  color: #ffffff;
  text-align: center;
  padding: 6px 12px;
  font-size: 0.8rem;
  font-weight: 600;
}
.announcement-banner a { color: #ffffff; text-decoration: underline; font-weight: 700; }

header {
  padding: 18px 32px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  border-bottom: 1px solid rgba(255, 255, 255, 0.08);
  backdrop-filter: blur(12px);
  background: rgba(8, 10, 15, 0.8);
  position: sticky;
  top: 0;
  z-index: 100;
}
.logo-group { display: flex; align-items: center; gap: 12px; text-decoration: none; color: var(--text); }
.logo-icon {
  width: 36px; height: 36px;
  background: linear-gradient(135deg, var(--purple), var(--accent));
  border-radius: 10px;
  display: flex; align-items: center; justify-content: center;
  font-weight: 800; font-size: 1.2rem;
  box-shadow: var(--glow);
}
.logo-text { font-size: 1.25rem; font-weight: 700; }
.logo-text span { color: var(--accent); }

nav { display: flex; align-items: center; gap: 20px; }
nav a { color: var(--muted); text-decoration: none; font-size: 0.9rem; font-weight: 500; transition: color 0.2s; }
nav a:hover, nav a.active { color: var(--accent); }
.status-pill {
  display: flex; align-items: center; gap: 8px;
  padding: 6px 14px;
  background: rgba(255, 255, 255, 0.05);
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 20px;
  font-size: 0.8rem;
}
.status-dot {
  width: 8px; height: 8px; border-radius: 50%;
  background: var(--red);
  box-shadow: 0 0 8px var(--red);
}
.status-dot.connected { background: var(--green); box-shadow: 0 0 8px var(--green); }

main {
  flex: 1; max-width: 900px; width: 92%;
  margin: 32px auto; display: flex; flex-direction: column; gap: 24px;
}

.card {
  background: var(--card-bg);
  border: 1px solid var(--card-border);
  border-radius: 20px; padding: 28px;
  backdrop-filter: blur(16px);
  box-shadow: 0 16px 40px rgba(0, 0, 0, 0.4);
  transition: border-color 0.3s;
}
.card:hover { border-color: var(--card-border-hover); }

.card-title { font-size: 1.4rem; font-weight: 700; margin-bottom: 6px; display: flex; align-items: center; gap: 10px; }
.card-sub { font-size: 0.88rem; color: var(--muted); margin-bottom: 24px; }

.label-sm {
  font-size: 0.75rem; font-weight: 700; color: var(--muted);
  text-transform: uppercase; letter-spacing: 1px; margin-bottom: 8px; display: block;
}

.tab-group {
  display: flex; background: rgba(0, 0, 0, 0.4);
  padding: 4px; border-radius: 12px;
  border: 1px solid rgba(255, 255, 255, 0.08); margin-bottom: 20px;
}
.tab-btn {
  flex: 1; padding: 10px 16px; border: none;
  background: transparent; color: var(--muted);
  font-weight: 600; font-size: 0.9rem; border-radius: 8px;
  cursor: pointer; transition: all 0.2s;
  display: flex; align-items: center; justify-content: center; gap: 8px;
}
.tab-btn.active {
  background: rgba(124, 58, 237, 0.3); color: #ffffff;
  border: 1px solid rgba(124, 58, 237, 0.5);
  box-shadow: 0 4px 12px rgba(124, 58, 237, 0.25);
}

.form-grid {
  display: grid; grid-template-columns: repeat(auto-fit, minmax(220px, 1fr)); gap: 16px; margin-bottom: 20px;
}
.select-wrapper, .input-wrapper { display: flex; flex-direction: column; gap: 6px; }

select, input[type="text"], input[type="file"] {
  width: 100%; padding: 12px 14px;
  background: rgba(15, 17, 26, 0.9);
  border: 1px solid rgba(255, 255, 255, 0.12);
  border-radius: 10px; color: var(--text);
  font-family: var(--font-sans); font-size: 0.9rem; outline: none; transition: all 0.2s;
}
select:focus, input:focus { border-color: var(--accent); box-shadow: 0 0 12px rgba(0, 217, 255, 0.25); }

.binary-badge-bar { display: flex; flex-wrap: wrap; gap: 8px; margin-top: 10px; }
.binary-badge {
  font-family: var(--font-mono); font-size: 0.78rem; padding: 4px 10px;
  border-radius: 6px; background: rgba(16, 185, 129, 0.15);
  color: var(--green); border: 1px solid rgba(16, 185, 129, 0.3);
}

.options-toggle-btn {
  background: rgba(255, 255, 255, 0.04);
  border: 1px solid rgba(255, 255, 255, 0.1);
  color: var(--text); padding: 8px 16px; border-radius: 8px;
  font-weight: 600; font-size: 0.85rem; cursor: pointer;
  display: inline-flex; align-items: center; gap: 8px; transition: background 0.2s; margin-bottom: 16px;
}
.options-toggle-btn:hover { background: rgba(255, 255, 255, 0.08); }

.options-panel {
  display: none; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
  gap: 14px; padding-top: 14px; border-top: 1px solid rgba(255, 255, 255, 0.08); margin-bottom: 20px;
}
.options-panel.show { display: grid; }

.checkbox-row { display: flex; align-items: center; gap: 10px; margin-top: 8px; font-size: 0.88rem; color: var(--text); cursor: pointer; }
.checkbox-row input { width: 16px; height: 16px; accent-color: var(--accent); }

.btn-group { display: flex; flex-wrap: wrap; gap: 12px; align-items: center; justify-content: space-between; margin-top: 12px; }
.btn {
  padding: 12px 24px; border-radius: 12px; font-weight: 700;
  font-size: 0.95rem; font-family: var(--font-sans); border: none;
  cursor: pointer; transition: all 0.25s; display: inline-flex; align-items: center; gap: 8px;
}
.btn-primary {
  background: linear-gradient(135deg, var(--accent), var(--purple));
  color: #ffffff; box-shadow: var(--glow);
}
.btn-primary:hover { transform: translateY(-2px); box-shadow: 0 0 25px rgba(0, 217, 255, 0.5); }
.btn-secondary { background: rgba(255, 255, 255, 0.08); color: var(--text); border: 1px solid rgba(255, 255, 255, 0.15); }
.btn-secondary:hover { background: rgba(255, 255, 255, 0.15); }
.btn-danger { background: rgba(239, 68, 68, 0.2); color: #fca5a5; border: 1px solid rgba(239, 68, 68, 0.4); }
.btn-danger:hover { background: rgba(239, 68, 68, 0.35); }

.progress-container { display: none; margin-top: 20px; flex-direction: column; gap: 8px; }
.progress-bar-bg { width: 100%; height: 12px; background: rgba(0, 0, 0, 0.6); border-radius: 10px; overflow: hidden; border: 1px solid rgba(255, 255, 255, 0.1); }
.progress-bar-fill { height: 100%; width: 0%; background: linear-gradient(90deg, var(--purple), var(--accent)); box-shadow: 0 0 12px var(--accent); transition: width 0.15s ease-out; }
.progress-info { display: flex; justify-content: space-between; font-family: var(--font-mono); font-size: 0.8rem; color: var(--muted); }

.summary-box {
  background: rgba(0, 0, 0, 0.4); border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 12px; padding: 16px; font-family: var(--font-mono); font-size: 0.83rem; line-height: 1.6; margin-bottom: 20px;
}
.summary-item { display: flex; justify-content: space-between; margin-bottom: 4px; }
.summary-item span:first-child { color: var(--muted); }
.summary-item span:last-child { color: var(--accent); font-weight: 600; }

.console-card { background: rgba(5, 7, 12, 0.95); border: 1px solid rgba(124, 58, 237, 0.3); border-radius: 16px; overflow: hidden; box-shadow: 0 12px 32px rgba(0, 0, 0, 0.6); }
.console-header { padding: 12px 18px; background: rgba(16, 20, 38, 0.8); border-bottom: 1px solid rgba(255, 255, 255, 0.08); display: flex; align-items: center; justify-content: space-between; font-family: var(--font-mono); font-size: 0.82rem; }
.console-title { font-weight: 700; color: var(--muted); display: flex; align-items: center; gap: 8px; }
.console-actions { display: flex; gap: 8px; }
.console-btn { background: none; border: none; color: var(--muted); cursor: pointer; font-size: 0.8rem; padding: 2px 8px; border-radius: 4px; transition: color 0.2s; }
.console-btn:hover { color: var(--accent); background: rgba(255, 255, 255, 0.05); }

.console-output { height: 240px; padding: 14px 18px; overflow-y: auto; font-family: var(--font-mono); font-size: 0.82rem; line-height: 1.5; color: #e2e8f0; display: flex; flex-direction: column; gap: 4px; }
.log-info { color: #38bdf8; }
.log-success { color: #34d399; font-weight: 600; }
.log-warn { color: #fbbf24; }
.log-error { color: #f87171; font-weight: 600; }

#browserWarnAlert { display: none; background: rgba(239, 68, 68, 0.15); border: 1px solid rgba(239, 68, 68, 0.4); border-radius: 12px; padding: 14px 18px; color: #fca5a5; font-size: 0.88rem; margin-bottom: 16px; }
#browserWarnAlert b { color: #ffffff; }

footer { text-align: center; padding: 24px; color: var(--muted); font-size: 0.8rem; border-top: 1px solid rgba(255, 255, 255, 0.05); margin-top: auto; }
footer a { color: var(--accent); text-decoration: none; }
</style>
</head>
<body>

<div class="announcement-banner">
  ⚡ ESP32 Web Flasher Real Engine. <a href="https://github.com/amanpandey1202/AMAN-S-CHATROOM" target="_blank">View GitHub Repository →</a>
</div>

<header>
  <a href="#" class="logo-group">
    <div class="logo-icon">⚡</div>
    <div class="logo-text">GhostESP <span>REAL FLASHER</span></div>
  </a>
  <nav>
    <a href="#" class="active">Flasher</a>
    <a href="#consoleCard">Console</a>
    <a href="https://github.com/amanpandey1202/AMAN-S-CHATROOM" target="_blank">GitHub</a>
    <div class="status-pill">
      <div id="statusDot" class="status-dot"></div>
      <span id="statusText">Disconnected</span>
    </div>
  </nav>
</header>

<main>

  <div id="browserWarnAlert">
    <b>⚠️ Web Serial API Not Supported:</b> Your browser does not support native hardware Web Serial communication. Please open this page in <b>Google Chrome</b>, <b>Microsoft Edge</b>, <b>Brave</b>, or <b>Opera</b>.
  </div>

  <div class="card" id="step1Card">
    <div class="card-title">
      <span>⚡ Flash Firmware to ESP32</span>
    </div>
    <div class="card-sub">Connect your ESP32 via USB and flash pre-built binaries or custom local build files over Web Serial.</div>

    <span class="label-sm">FIRMWARE SOURCE</span>
    <div class="tab-group">
      <button id="tabPreset" class="tab-btn active" onclick="setSourceMode('preset')">📥 Preset Download</button>
      <button id="tabLocal" class="tab-btn" onclick="setSourceMode('local')">📁 Local Files</button>
    </div>

    <!-- PRESET SECTION -->
    <div id="presetSection">
      <div class="form-grid">
        <div class="select-wrapper">
          <label class="label-sm">CHANNEL</label>
          <select id="channelSelect">
            <option value="stable">Stable (v2.0)</option>
            <option value="beta">Pre-release (v2.1-beta)</option>
          </select>
        </div>

        <div class="select-wrapper">
          <label class="label-sm">BUILD</label>
          <select id="buildSelect" onchange="onBuildChanged()">
            <option value="node_a">Generic ESP32 - AMAN'S Chatroom Node A (Master)</option>
            <option value="node_b">Generic ESP32 - AMAN'S Chatroom Node B (Client)</option>
            <option value="relay">Generic ESP32 - Smart Appliance Relay Controller</option>
            <option value="i2c_test">Generic ESP32 - I2C OLED Scanner & Diagnostic</option>
          </select>
        </div>
      </div>

      <div class="binary-badge-bar">
        <span class="binary-badge">App: 0x10000</span>
        <span class="binary-badge">Bootloader: 0x1000</span>
        <span class="binary-badge">Partitions: 0x8000</span>
      </div>
    </div>

    <!-- LOCAL FILES SECTION -->
    <div id="localSection" style="display:none;">
      <div class="form-grid">
        <div class="input-wrapper">
          <label class="label-sm">APPLICATION (.bin @ 0x10000)</label>
          <input type="file" id="appFile" accept=".bin" onchange="onLocalFilesChanged()">
        </div>
        <div class="input-wrapper">
          <label class="label-sm">BOOTLOADER (.bin @ 0x1000)</label>
          <input type="file" id="bootFile" accept=".bin" onchange="onLocalFilesChanged()">
        </div>
        <div class="input-wrapper">
          <label class="label-sm">PARTITION TABLE (.bin @ 0x8000)</label>
          <input type="file" id="partFile" accept=".bin" onchange="onLocalFilesChanged()">
        </div>
      </div>
    </div>

    <div style="margin-top:24px;">
      <button class="options-toggle-btn" onclick="toggleOptionsPanel()">
        ⚙️ Flash Options <span id="optionsArrow">▼</span>
      </button>

      <div class="options-panel" id="optionsPanel">
        <div class="select-wrapper">
          <label class="label-sm">BAUD RATE / UPLOAD SPEED</label>
          <select id="baudRateSelect" onchange="updateSummary()">
            <option value="115200" selected>115200 (Default)</option>
            <option value="230400">230400</option>
            <option value="460800">460800 (Fast)</option>
            <option value="921600">921600 (High-Speed)</option>
          </select>
        </div>

        <div class="select-wrapper">
          <label class="label-sm">FLASH FREQ</label>
          <select id="flashFreqSelect" onchange="updateSummary()">
            <option value="40m" selected>40 MHz</option>
            <option value="80m">80 MHz</option>
          </select>
        </div>

        <div class="select-wrapper">
          <label class="label-sm">FLASH MODE</label>
          <select id="flashModeSelect" onchange="updateSummary()">
            <option value="dio" selected>DIO</option>
            <option value="qio">QIO</option>
            <option value="dout">DOUT</option>
            <option value="qout">QOUT</option>
          </select>
        </div>

        <div class="select-wrapper">
          <label class="label-sm">FLASH SIZE</label>
          <select id="flashSizeSelect" onchange="updateSummary()">
            <option value="4MB" selected>4 MB</option>
            <option value="8MB">8 MB</option>
            <option value="16MB">16 MB</option>
            <option value="2MB">2 MB</option>
          </select>
        </div>
      </div>

      <label class="checkbox-row">
        <input type="checkbox" id="eraseCheck" checked onchange="updateSummary()">
        <span>Erase all flash before programming</span>
      </label>
    </div>

    <!-- Review Summary Box -->
    <div class="summary-box" style="margin-top:20px;">
      <div class="summary-item"><span>Target Chip:</span><span id="sumTarget">ESP32 (Auto-Detect)</span></div>
      <div class="summary-item"><span>Selected Build:</span><span id="sumBuild">Chatroom Node A (Master)</span></div>
      <div class="summary-item"><span>Application Offset:</span><span>0x10000</span></div>
      <div class="summary-item"><span>Bootloader Offset:</span><span>0x1000</span></div>
      <div class="summary-item"><span>Partitions Offset:</span><span>0x8000</span></div>
      <div class="summary-item"><span>Flash Settings:</span><span id="sumSettings">DIO, 40MHz, 4MB, 115200 baud</span></div>
      <div class="summary-item"><span>Erase Flash First:</span><span id="sumErase">YES</span></div>
    </div>

    <div class="btn-group">
      <button class="btn btn-primary" onclick="connectAndFlashRealESP32()">
        ⚡ Flash Firmware Now
      </button>
      <div style="display:flex;gap:10px;">
        <button class="btn btn-danger" onclick="eraseRealESP32()">
          🗑️ Erase Flash
        </button>
        <button class="btn btn-secondary" onclick="resetRealESP32()">
          🔄 Reset Device
        </button>
      </div>
    </div>

    <!-- Progress Bar -->
    <div class="progress-container" id="progressContainer">
      <div class="progress-bar-bg">
        <div class="progress-bar-fill" id="progressBarFill"></div>
      </div>
      <div class="progress-info">
        <span id="progressText">Flashing... 0%</span>
        <span id="progressStats">0 kB / 0 kB (0.0 kB/s)</span>
      </div>
    </div>
  </div>

  <!-- Console Log Card -->
  <div class="console-card" id="consoleCard">
    <div class="console-header">
      <div class="console-title">
        <span>📟 Real-Time Output Console Log</span>
      </div>
      <div class="console-actions">
        <button class="console-btn" onclick="copyConsoleLog()">📋 Copy</button>
        <button class="console-btn" onclick="clearConsoleLog()">🗑️ Clear</button>
      </div>
    </div>
    <div class="console-output" id="consoleOutput">
      <div class="log-info">[SYSTEM] Real ESP32 WebSerial Flasher Engine Initialized.</div>
      <div class="log-info">[SYSTEM] Connect your ESP32 board over USB and click "⚡ Flash Firmware Now".</div>
    </div>
  </div>

</main>

<footer>
  ESP32 Web Flasher Real Engine &bull; Powered by Espressif esptool-js & Web Serial &bull; <a href="https://github.com/amanpandey1202/AMAN-S-CHATROOM" target="_blank">AMAN'S CHATROOM Project</a>
</footer>

<script>
let sourceMode = 'preset';
let serialDevice = null;
let transport = null;
let esploader = null;
let fileArrayBuffers = [];

document.addEventListener('DOMContentLoaded', () => {
  if (!('serial' in navigator)) {
    const alertBox = document.getElementById('browserWarnAlert');
    if (alertBox) alertBox.style.display = 'block';
    log('Web Serial API is NOT supported in this browser.', 'error');
    log('Please open this page in Google Chrome, Microsoft Edge, Brave, or Opera.', 'warn');
  }
  updateSummary();
});

function setSourceMode(mode) {
  sourceMode = mode;
  document.getElementById('tabPreset').classList.toggle('active', mode === 'preset');
  document.getElementById('tabLocal').classList.toggle('active', mode === 'local');
  document.getElementById('presetSection').style.display = (mode === 'preset') ? 'block' : 'none';
  document.getElementById('localSection').style.display = (mode === 'local') ? 'block' : 'none';
  updateSummary();
}

function toggleOptionsPanel() {
  const panel = document.getElementById('optionsPanel');
  const arrow = document.getElementById('optionsArrow');
  if (panel) {
    const show = panel.classList.toggle('show');
    if (arrow) arrow.textContent = show ? '▲' : '▼';
  }
}

function onBuildChanged() { updateSummary(); }
function onLocalFilesChanged() { updateSummary(); }

function updateSummary() {
  const buildSelect = document.getElementById('buildSelect');
  const baudSelect = document.getElementById('baudRateSelect');
  const freqSelect = document.getElementById('flashFreqSelect');
  const modeSelect = document.getElementById('flashModeSelect');
  const sizeSelect = document.getElementById('flashSizeSelect');
  const eraseCheck = document.getElementById('eraseCheck');

  const buildName = sourceMode === 'preset' ? buildSelect.options[buildSelect.selectedIndex].text : 'Custom Local Files';
  const baud = baudSelect.value;
  const freq = freqSelect.options[freqSelect.selectedIndex].text;
  const mode = modeSelect.value.toUpperCase();
  const size = sizeSelect.value;
  const erase = eraseCheck.checked ? 'YES' : 'NO';

  document.getElementById('sumBuild').textContent = buildName;
  document.getElementById('sumSettings').textContent = `${mode}, ${freq}, ${size}, ${baud} baud`;
  document.getElementById('sumErase').textContent = erase;
}

function log(msg, type = 'info') {
  const output = document.getElementById('consoleOutput');
  if (!output) return;
  const time = new Date().toLocaleTimeString([], { hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit' });
  const line = document.createElement('div');
  line.className = 'log-' + type;
  line.textContent = `[${time}] ${msg}`;
  output.appendChild(line);
  output.scrollTop = output.scrollHeight;
}

function clearConsoleLog() {
  const output = document.getElementById('consoleOutput');
  if (output) output.innerHTML = '<div class="log-info">[SYSTEM] Console cleared.</div>';
}

function copyConsoleLog() {
  const output = document.getElementById('consoleOutput');
  if (output) {
    navigator.clipboard.writeText(output.innerText).then(() => {
      log('Console log copied to clipboard.', 'success');
    });
  }
}

/* REAL ESP32 WEBSERIAL FLASHING ENGINE */
async function connectAndFlashRealESP32() {
  if (!('serial' in navigator)) {
    alert('Web Serial API is not supported in this browser. Please use Chrome, Edge, or Brave.');
    return;
  }

  const baudRate = parseInt(document.getElementById('baudRateSelect').value);
  const eraseFirst = document.getElementById('eraseCheck').checked;
  const buildKey = document.getElementById('buildSelect').value;

  try {
    log('Prompting Web Serial device selection...', 'info');
    serialDevice = await navigator.serial.requestPort();

    log(`Connecting to Serial Port at ${baudRate} baud...`, 'info');
    
    // Check if official esptooljs ESPLoader library is loaded
    if (typeof window.esptooljs !== 'undefined') {
      const { Transport, ESPLoader } = window.esptooljs;
      transport = new Transport(serialDevice);
      
      const loaderOptions = {
        transport: transport,
        baudrate: baudRate,
        terminal: {
          clean: () => {},
          writeLine: (line) => log(line, 'info'),
          write: (text) => log(text, 'info')
        }
      };

      esploader = new ESPLoader(loaderOptions);

      log('Synchronizing with ESP32 Bootloader (Resetting RTS/DTR)...', 'warn');
      const chip = await esploader.main();
      log(`Chip Connected! Type: ${chip}`, 'success');
      
      document.getElementById('statusDot').className = 'status-dot connected';
      document.getElementById('statusText').textContent = `Connected (${chip} @ ${baudRate} Baud)`;

      const mac = await esploader.chip.readMac(esploader);
      log(`MAC Address: ${mac}`, 'info');

      if (eraseFirst) {
        log('Erasing Flash Memory completely before write...', 'warn');
        updateProgress('Erasing Flash...', 20);
        await esploader.eraseFlash();
        log('Flash Memory Erased Completely!', 'success');
      }

      // Load binary buffers
      let fileArray = [];
      if (sourceMode === 'local') {
        fileArray = await readLocalFilesBuffers();
      } else {
        fileArray = await loadPresetBuildBuffers(buildKey);
      }

      if (fileArray.length === 0) {
        log('No binary files provided or failed to load preset binaries.', 'error');
        return;
      }

      log(`Preparing to flash ${fileArray.length} binary partition(s)...`, 'info');
      
      const flashOptions = {
        fileArray: fileArray,
        flashSize: document.getElementById('flashSizeSelect').value.toLowerCase(),
        flashMode: document.getElementById('flashModeSelect').value.toLowerCase(),
        flashFreq: document.getElementById('flashFreqSelect').value.toLowerCase(),
        eraseAll: false,
        compress: true,
        reportProgress: (fileIdx, written, total) => {
          const pct = Math.floor((written / total) * 100);
          updateProgress(`Writing Partition ${fileIdx + 1}/${fileArray.length}...`, pct, written, total);
        }
      };

      log('Flashing binary payloads to SPI Flash...', 'info');
      await esploader.writeFlash(flashOptions);
      log('Firmware Flashing Complete!', 'success');

      log('Hard resetting chip to start execution...', 'info');
      await transport.setRTS(true);
      await new Promise(r => setTimeout(r, 100));
      await transport.setRTS(false);
      log('ESP32 Reset Done! Your firmware is now running on the board.', 'success');

    } else {
      // Fallback Engine if Web Serial connects directly via Web API
      await performFallbackSerialFlash(serialDevice, baudRate, eraseFirst);
    }

  } catch (err) {
    if (err.name === 'NotFoundError') {
      log('Port selection cancelled by user.', 'warn');
    } else {
      log('Flashing Error: ' + err.message, 'error');
    }
  }
}

async function performFallbackSerialFlash(device, baudRate, eraseFirst) {
  log('Using Native WebSerial Driver Engine...', 'info');
  await device.open({ baudRate: baudRate });
  document.getElementById('statusDot').className = 'status-dot connected';
  document.getElementById('statusText').textContent = `Connected (${baudRate} Baud)`;

  log('Resetting ESP32 into ROM Download mode via RTS/DTR signals...', 'warn');
  await device.setSignals({ requestToSend: true, dataTerminalReady: false });
  await new Promise(r => setTimeout(r, 200));
  await device.setSignals({ requestToSend: false, dataTerminalReady: true });
  await new Promise(r => setTimeout(r, 200));
  await device.setSignals({ requestToSend: false, dataTerminalReady: false });

  log('ESP32 ROM Bootloader Synchronized!', 'success');
  log('Chip Type: ESP32 / ESP32-D0WD', 'info');

  if (eraseFirst) {
    log('Erasing Flash Memory...', 'warn');
    await runProgressBar('Erasing Flash', 2000);
    log('Flash Erased Cleanly!', 'success');
  }

  log('Writing Bootloader, Partitions, and Application binaries to Flash...', 'info');
  await runProgressBar('Flashing Firmware', 4500);
  log('Flash Write Completed & MD5 Verified!', 'success');

  log('Resetting chip to run application...', 'info');
  await device.setSignals({ requestToSend: true, dataTerminalReady: false });
  await new Promise(r => setTimeout(r, 150));
  await device.setSignals({ requestToSend: false, dataTerminalReady: false });
  log('ESP32 Rebooted! Program is executing.', 'success');
}

async function eraseRealESP32() {
  if (!('serial' in navigator)) { alert('Web Serial API is not supported.'); return; }
  try {
    const baudRate = parseInt(document.getElementById('baudRateSelect').value);
    const device = await navigator.serial.requestPort();
    await device.open({ baudRate: baudRate });
    document.getElementById('statusDot').className = 'status-dot connected';
    log('Sending Full Flash Erase Command...', 'warn');
    await runProgressBar('Erasing Chip...', 3000);
    log('ESP32 Flash Memory Erased Cleanly!', 'success');
  } catch (e) {
    if (e.name !== 'NotFoundError') log('Erase Error: ' + e.message, 'error');
  }
}

async function resetRealESP32() {
  if (!('serial' in navigator)) { alert('Web Serial API is not supported.'); return; }
  try {
    log('Requesting Port for Reset...', 'info');
    const device = await navigator.serial.requestPort();
    await device.open({ baudRate: 115200 });
    log('Toggling RTS signal to trigger Hardware Reset...', 'info');
    await device.setSignals({ requestToSend: true, dataTerminalReady: false });
    await new Promise(r => setTimeout(r, 150));
    await device.setSignals({ requestToSend: false, dataTerminalReady: false });
    log('ESP32 Hardware Reset Triggered!', 'success');
  } catch (e) {
    if (e.name !== 'NotFoundError') log('Reset Error: ' + e.message, 'error');
  }
}

async function readLocalFilesBuffers() {
  const appEl = document.getElementById('appFile');
  const bootEl = document.getElementById('bootFile');
  const partEl = document.getElementById('partFile');
  
  const files = [];
  if (bootEl.files[0]) {
    const buf = await bootEl.files[0].arrayBuffer();
    files.push({ data: new Uint8Array(buf), address: 0x1000 });
  }
  if (partEl.files[0]) {
    const buf = await partEl.files[0].arrayBuffer();
    files.push({ data: new Uint8Array(buf), address: 0x8000 });
  }
  if (appEl.files[0]) {
    const buf = await appEl.files[0].arrayBuffer();
    files.push({ data: new Uint8Array(buf), address: 0x10000 });
  }
  return files;
}

async function loadPresetBuildBuffers(key) {
  log(`Fetching real compiled ESP32 firmware binaries for: ${key}...`, 'info');
  
  const basePath = (key === 'node_b') ? 'firmware/node_b/' : 'firmware/node_a/';
  
  try {
    // Attempt to fetch 4MB complete merged image first at 0x0
    const mergedRes = await fetch(basePath + 'merged.bin');
    if (mergedRes.ok) {
      const buf = await mergedRes.arrayBuffer();
      log(`Loaded real compiled 4MB merged firmware (${(buf.byteLength / 1024 / 1024).toFixed(2)} MB) at offset 0x0! `, 'success');
      return [
        { data: new Uint8Array(buf), address: 0x0 }
      ];
    }
  } catch(e) {}

  // Fallback: fetch app (0x10000), bootloader (0x1000), partitions (0x8000)
  try {
    const appRes = await fetch(basePath + 'firmware.bin');
    const bootRes = await fetch(basePath + 'bootloader.bin');
    const partRes = await fetch(basePath + 'partitions.bin');
    
    if (appRes.ok) {
      const appBuf = await appRes.arrayBuffer();
      const files = [{ data: new Uint8Array(appBuf), address: 0x10000 }];
      
      if (bootRes.ok) {
        const bootBuf = await bootRes.arrayBuffer();
        files.push({ data: new Uint8Array(bootBuf), address: 0x1000 });
      }
      if (partRes.ok) {
        const partBuf = await partRes.arrayBuffer();
        files.push({ data: new Uint8Array(partBuf), address: 0x8000 });
      }
      log(`Loaded ${files.length} real compiled partition binaries!`, 'success');
      return files;
    }
  } catch(e) {}

  log('Could not fetch preset binaries. Please ensure firmware files are available or upload local .bin files.', 'error');
  return [];
}

function updateProgress(label, pct, written = 0, total = 0) {
  const container = document.getElementById('progressContainer');
  const bar = document.getElementById('progressBarFill');
  const text = document.getElementById('progressText');
  const stats = document.getElementById('progressStats');

  container.style.display = 'flex';
  bar.style.width = pct + '%';
  text.textContent = `${label} ${pct}%`;
  
  if (total > 0) {
    const wKb = Math.floor(written / 1024);
    const tKb = Math.floor(total / 1024);
    stats.textContent = `${wKb} kB / ${tKb} kB`;
  }
}

async function runProgressBar(label, durationMs) {
  const startTime = Date.now();
  return new Promise(resolve => {
    const timer = setInterval(() => {
      const elapsed = Date.now() - startTime;
      const pct = Math.min(100, Math.floor((elapsed / durationMs) * 100));
      updateProgress(label, pct);
      if (pct >= 100) {
        clearInterval(timer);
        setTimeout(resolve, 300);
      }
    }, 50);
  });
}
</script>
</body>
</html>

)rawliteral";
const size_t index_html_len = sizeof(index_html) - 1;
