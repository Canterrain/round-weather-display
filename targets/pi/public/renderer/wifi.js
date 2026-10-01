// On-device WiFi setup, mirroring the ESP32-P4's on-screen network picker so
// the clock can recover from a lost WiFi connection without any other
// device, keyboard, or monitor. Reached from the Settings screen's "Wi-Fi"
// button (settings.js), and auto-surfaced whenever the device actually loses
// its WiFi connection (see pollWifiLinkStatus below).

const WIFI_STATUS_POLL_INTERVAL_MS = 5000;
const WIFI_SCAN_POLL_INTERVAL_MS = 15 * 1000;
const WIFI_DISCONNECTED_POLLS_BEFORE_AUTO_SURFACE = 2;

const wifiState = {
  // 'list' | 'password' | 'connecting'
  step: 'list',
  networks: [],
  selectedNetwork: null,
  // Where Done / a successful connect goes: Settings when opened from there,
  // home when auto-surfaced by an outage.
  returnView: null,
  consecutiveDisconnectedPolls: 0,
  dismissedForThisOutage: false,
  scanPollHandle: null,
  fullStatusPollHandle: null
};

function wifiEl(id) {
  return document.getElementById(id);
}

function isWifiViewOpen() {
  return window.appView?.getCurrentViewMode?.() === window.appView?.VIEW_MODES?.WIFI;
}

let wifiViewBuilt = false;
let wifiKeyboard = null;

function leaveWifiView() {
  const returnView = wifiState.returnView;
  if (returnView) {
    window.appView?.setViewMode?.(returnView);
  } else {
    window.appView?.returnToHome?.();
  }
}

// Builds this screen's DOM (title, status line, network list, password
// panel, ~40 keyboard buttons) on first open rather than at page load. This
// screen is rarely used -- WiFi recovery -- and this device is
// memory-constrained enough that keeping it resident at all times competed
// with the views actually in regular rotation, causing them to get paged
// out to swap when idle and lag on the next swipe back to them.
function ensureWifiViewBuilt() {
  if (wifiViewBuilt) return;
  const root = document.querySelector('.view-wifi');
  if (!root) return;

  root.innerHTML = `
    <div class="wifi-face">
      <div class="wifi-title">Wi-Fi</div>
      <div id="wifi-status-line" class="wifi-status-line">Checking connection&hellip;</div>
      <div id="wifi-network-list" class="wifi-network-list" data-swipe-exempt></div>
      <div id="wifi-password-panel" class="wifi-password-panel" hidden>
        <div id="wifi-selected-ssid" class="wifi-selected-ssid"></div>
        <div class="wifi-password-row">
          <input id="wifi-password-input" class="wifi-password-input" type="password" readonly inputmode="none" placeholder="Password"/>
          <button id="wifi-password-toggle" class="wifi-icon-button" type="button">Show</button>
        </div>
        <div id="wifi-connect-error" class="wifi-connect-error" hidden></div>
        <div class="wifi-action-row">
          <button id="wifi-back-button" class="wifi-secondary-button" type="button">Back</button>
          <button id="wifi-connect-button" class="wifi-primary-button" type="button">Connect</button>
        </div>
        <div id="wifi-keyboard" class="osk" data-swipe-exempt></div>
      </div>
      <button id="wifi-done-button" class="wifi-secondary-button wifi-done-button" type="button">Done</button>
    </div>
  `;

  setupWifiViewEvents();
  wifiKeyboard = window.createOnScreenKeyboard(wifiEl('wifi-keyboard'), () => wifiEl('wifi-password-input'));
  wifiViewBuilt = true;
}

// Only a non-2xx response is treated as a hard failure. GET /api/wifi/status
// and /api/wifi/scan intentionally respond 200 with a soft `error` field
// (e.g. "nmcli not available") rather than a 500, so callers always get a
// parseable, renderable result -- throwing on that field here would defeat
// the point of that design. POST /api/wifi/connect's failures already use a
// real 400, so they're still caught correctly via response.ok.
async function fetchWifiJson(path, options) {
  const response = await fetch(path, { cache: 'no-store', ...options });
  const data = await response.json().catch(() => ({}));
  if (!response.ok) {
    throw new Error(data.error || response.statusText || 'Request failed');
  }
  return data;
}

function renderWifiStatusLine(status) {
  const line = wifiEl('wifi-status-line');
  if (!line) return;

  if (!status) {
    line.textContent = 'Checking connection…';
  } else if (status.error) {
    line.textContent = 'WiFi management unavailable on this device';
  } else if (status.connected) {
    line.textContent = status.ssid ? `Connected to ${status.ssid}` : 'Connected';
  } else {
    line.textContent = 'Not connected';
  }
}

function renderNetworkList(networks, emptyMessage) {
  const list = wifiEl('wifi-network-list');
  if (!list) return;

  list.innerHTML = '';

  if (!networks.length) {
    const empty = document.createElement('div');
    empty.className = 'wifi-network-empty';
    empty.textContent = emptyMessage || 'No networks found nearby.';
    list.appendChild(empty);
    return;
  }

  networks.forEach((network) => {
    const row = document.createElement('div');
    row.className = 'wifi-network-row';

    const name = document.createElement('span');
    name.textContent = network.ssid;

    const signal = document.createElement('span');
    signal.className = 'wifi-network-row-signal';
    signal.textContent = `${network.secured ? '🔒 ' : ''}${network.signal}%`;

    row.appendChild(name);
    row.appendChild(signal);
    row.addEventListener('click', () => selectNetwork(network));
    list.appendChild(row);
  });
}

async function refreshNetworkList() {
  try {
    const data = await fetchWifiJson('/api/wifi/scan');
    wifiState.networks = Array.isArray(data.networks) ? data.networks : [];
    renderNetworkList(wifiState.networks, data.error ? 'WiFi management unavailable on this device.' : null);
  } catch (error) {
    console.error('WiFi scan failed:', error.message);
    renderNetworkList([], 'Could not scan for networks.');
  }
}

function showListStep() {
  wifiState.step = 'list';
  wifiState.selectedNetwork = null;
  wifiEl('wifi-password-panel').hidden = true;
  wifiEl('wifi-network-list').hidden = false;
  wifiEl('wifi-connect-error').hidden = true;
  refreshNetworkList();
}

function showPasswordStep(network) {
  wifiState.step = 'password';
  wifiState.selectedNetwork = network;
  wifiKeyboard?.reset();

  wifiEl('wifi-network-list').hidden = true;
  wifiEl('wifi-password-panel').hidden = false;
  wifiEl('wifi-connect-error').hidden = true;
  wifiEl('wifi-selected-ssid').textContent = network.ssid;

  const input = wifiEl('wifi-password-input');
  input.value = '';
  input.type = 'password';
  input.placeholder = network.secured ? 'Password' : 'Password (open network — can stay blank)';
  wifiEl('wifi-password-toggle').textContent = 'Show';

  const connectButton = wifiEl('wifi-connect-button');
  connectButton.disabled = false;
  connectButton.textContent = 'Connect';
}

function selectNetwork(network) {
  showPasswordStep(network);
}

function setWifiControlsDisabled(disabled) {
  wifiEl('wifi-connect-button').disabled = disabled;
  wifiEl('wifi-back-button').disabled = disabled;
  wifiKeyboard?.setDisabled(disabled);
}

async function attemptConnect() {
  if (!wifiState.selectedNetwork) return;

  wifiState.step = 'connecting';
  setWifiControlsDisabled(true);
  wifiEl('wifi-connect-button').textContent = 'Connecting…';
  wifiEl('wifi-connect-error').hidden = true;

  try {
    await fetchWifiJson('/api/wifi/connect', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        ssid: wifiState.selectedNetwork.ssid,
        password: wifiEl('wifi-password-input').value
      })
    });

    wifiState.consecutiveDisconnectedPolls = 0;
    wifiState.dismissedForThisOutage = false;
    leaveWifiView();
  } catch (error) {
    wifiState.step = 'password';
    setWifiControlsDisabled(false);
    wifiEl('wifi-connect-button').textContent = 'Connect';
    const errorEl = wifiEl('wifi-connect-error');
    errorEl.textContent = error.message || 'Could not join network';
    errorEl.hidden = false;
  }
}

function setupWifiViewEvents() {
  wifiEl('wifi-back-button').addEventListener('click', showListStep);

  wifiEl('wifi-connect-button').addEventListener('click', attemptConnect);

  wifiEl('wifi-done-button').addEventListener('click', leaveWifiView);

  wifiEl('wifi-password-toggle').addEventListener('click', () => {
    const input = wifiEl('wifi-password-input');
    const toggle = wifiEl('wifi-password-toggle');
    const showing = input.type === 'text';
    input.type = showing ? 'password' : 'text';
    toggle.textContent = showing ? 'Show' : 'Hide';
  });
}

// --- Auto-surface / auto-return on real WiFi loss ---
//
// Polls the device's actual local WiFi connection state (not weather-fetch
// success, which could fail for unrelated reasons like an Open-Meteo
// outage) so a real outage automatically brings up this screen instead of
// requiring the swipe gesture to be remembered mid-emergency.
//
// This runs forever, from every screen, so it deliberately hits the cheap
// /api/wifi/link-status endpoint (an in-process check, no subprocess) rather
// than /api/wifi/status (which shells out to nmcli). The accurate,
// nmcli-backed status -- SSID, IP -- is only fetched while this screen is
// actually open, via refreshFullWifiStatus() below.

async function pollWifiLinkStatus() {
  let status = null;
  try {
    status = await fetchWifiJson('/api/wifi/link-status');
  } catch (error) {
    console.error('WiFi link status check failed:', error.message);
  }

  if (!status || status.error) return;

  if (status.connected) {
    // Only auto-return if this poll is actually recovering from a real
    // outage (we'd previously seen it disconnected) -- not just because
    // "currently connected" happens to be true on every tick, which would
    // otherwise force the user back out of this screen within a few seconds
    // of opening it any time they're already online (e.g. just browsing the
    // network list, or mid-typing a password for a network switch).
    const wasRecovering = wifiState.consecutiveDisconnectedPolls > 0;
    wifiState.consecutiveDisconnectedPolls = 0;
    wifiState.dismissedForThisOutage = false;
    if (wasRecovering && isWifiViewOpen() && wifiState.step === 'list') {
      leaveWifiView();
    }
    return;
  }

  wifiState.consecutiveDisconnectedPolls += 1;
  const shouldAutoSurface = wifiState.consecutiveDisconnectedPolls >= WIFI_DISCONNECTED_POLLS_BEFORE_AUTO_SURFACE
    && !wifiState.dismissedForThisOutage
    && !isWifiViewOpen();

  if (shouldAutoSurface) {
    wifiState.returnView = null;
    window.appView?.setViewMode?.(window.appView.VIEW_MODES.WIFI);
  }
}

// Accurate status (SSID, IP) for display on the screen itself -- only runs
// while the screen is actually open, since it costs an nmcli subprocess
// spawn per call.
async function refreshFullWifiStatus() {
  try {
    renderWifiStatusLine(await fetchWifiJson('/api/wifi/status'));
  } catch (error) {
    console.error('WiFi status check failed:', error.message);
  }
}

function setupWifiViewVisibilityWatcher() {
  // window.appView.setViewMode doesn't emit an event, so watch the
  // .app-shell's mode-wifi class directly to know when to start/stop
  // scan-polling and reset per-visit state -- avoids scanning constantly
  // in the background when nobody's looking at the screen.
  const appShell = document.querySelector('.app-shell');
  if (!appShell) return;

  let wasOpen = false;
  const observer = new MutationObserver(() => {
    const open = appShell.classList.contains('mode-wifi');
    if (open === wasOpen) return;
    wasOpen = open;

    if (open) {
      ensureWifiViewBuilt();
      showListStep();
      refreshFullWifiStatus();
      window.clearInterval(wifiState.scanPollHandle);
      wifiState.scanPollHandle = window.setInterval(refreshNetworkList, WIFI_SCAN_POLL_INTERVAL_MS);
      window.clearInterval(wifiState.fullStatusPollHandle);
      wifiState.fullStatusPollHandle = window.setInterval(refreshFullWifiStatus, WIFI_STATUS_POLL_INTERVAL_MS);
    } else {
      window.clearInterval(wifiState.scanPollHandle);
      wifiState.scanPollHandle = null;
      window.clearInterval(wifiState.fullStatusPollHandle);
      wifiState.fullStatusPollHandle = null;
      if (wifiState.consecutiveDisconnectedPolls > 0) {
        wifiState.dismissedForThisOutage = true;
      }
      wifiState.returnView = null;
    }
  });

  observer.observe(appShell, { attributes: true, attributeFilter: ['class'] });
}

// Opened from Settings, so Done comes back to Settings.
function openWifiFrom(returnView) {
  wifiState.returnView = returnView || null;
  window.appView?.setViewMode?.(window.appView.VIEW_MODES.WIFI);
}

window.wifiScreen = { openFrom: openWifiFrom };

function initializeWifiUi() {
  // Deliberately does NOT call ensureWifiViewBuilt() here -- the whole point
  // is to defer that DOM cost until the screen is actually opened. Only the
  // lightweight, DOM-free auto-surface poller runs unconditionally from
  // page load.
  setupWifiViewVisibilityWatcher();
  pollWifiLinkStatus();
  window.setInterval(pollWifiLinkStatus, WIFI_STATUS_POLL_INTERVAL_MS);
}

initializeWifiUi();
