// On-device Settings, reached by swiping down from home. Mirrors the
// ESP32-P4's setup screen field for field -- location, room name, device ID,
// house messages, home screen, time format, leading zero, units, night shift,
// plus Wi-Fi / Cancel / Save & Restart -- so both clocks are configured the
// same way. Changes are only a draft until Save & Restart; the server
// (POST /api/settings, localhost-only) validates, looks up a new location,
// writes config.json, and restarts the app.

const SETTINGS_TEXT_FIELDS = {
  location: { label: 'Location', hint: 'City, State — like Cincinnati, Ohio' },
  roomName: { label: 'Room Name', hint: 'Shown when sending house messages' },
  deviceId: { label: 'Device ID', hint: 'Must be unique on your network' }
};

// Toggles cycle between two values, labelled the way the ESP32 labels them.
const SETTINGS_TOGGLES = {
  messageSharing: { label: 'House Messages', values: [['single', 'Single'], ['shared', 'Shared']] },
  defaultClockFace: { label: 'Home Screen', values: [['digital', 'Digital'], ['analog', 'Analog']] },
  timeFormat: { label: 'Time Format', values: [['12', '12-hour'], ['24', '24-hour']] },
  leadingZero12h: { label: 'Leading Zero', values: [[true, 'On'], [false, 'Off']] },
  units: { label: 'Units', values: [['imperial', 'Imperial'], ['metric', 'Metric']] },
  nightShift: { label: 'Night Shift', values: [[false, 'Off'], [true, 'On']] }
};

// Same order as the ESP32 screen.
const SETTINGS_ORDER = [
  'location', 'roomName', 'deviceId',
  'messageSharing', 'defaultClockFace', 'timeFormat', 'leadingZero12h', 'units', 'nightShift'
];

const settingsState = {
  saved: null,
  draft: null,
  editingKey: null,
  saving: false,
  // True while the user is on the Wi-Fi sub-page, so their unsaved edits are
  // still there when they come back. Cleared on leaving for anywhere else.
  resumeDraft: false
};

let settingsViewBuilt = false;
let settingsKeyboard = null;

function settingsEl(id) {
  return document.getElementById(id);
}

// Built on first open, like the WiFi screen, to keep it out of memory on a
// device that rarely needs it.
function ensureSettingsViewBuilt() {
  if (settingsViewBuilt) return;
  const root = document.querySelector('.view-settings');
  if (!root) return;

  root.innerHTML = `
    <div class="settings-face">
      <div class="settings-title">Settings</div>
      <div id="settings-list" class="settings-list" data-swipe-exempt></div>
      <div id="settings-edit" class="settings-edit" hidden>
        <div id="settings-edit-label" class="settings-edit-label"></div>
        <div id="settings-edit-hint" class="settings-edit-hint"></div>
        <input id="settings-edit-input" class="wifi-password-input settings-edit-input" type="text" readonly inputmode="none" maxlength="64"/>
        <div id="settings-keyboard" class="osk" data-swipe-exempt></div>
        <div class="wifi-action-row">
          <button id="settings-edit-cancel" class="wifi-secondary-button" type="button">Cancel</button>
          <button id="settings-edit-done" class="wifi-primary-button" type="button">Done</button>
        </div>
      </div>
      <div id="settings-status" class="settings-status" aria-live="polite"></div>
      <div id="settings-actions" class="settings-actions">
        <button id="settings-wifi" class="wifi-secondary-button" type="button">Wi-Fi</button>
        <button id="settings-cancel" class="wifi-secondary-button" type="button">Cancel</button>
        <button id="settings-save" class="wifi-primary-button" type="button">Save &amp; Restart</button>
      </div>
    </div>
  `;

  settingsKeyboard = window.createOnScreenKeyboard(settingsEl('settings-keyboard'), () => settingsEl('settings-edit-input'));
  settingsEl('settings-edit-cancel').addEventListener('click', closeTextEditor);
  settingsEl('settings-edit-done').addEventListener('click', commitTextEditor);
  settingsEl('settings-wifi').addEventListener('click', () => {
    settingsState.resumeDraft = true;
    window.wifiScreen?.openFrom(window.appView.VIEW_MODES.SETTINGS);
  });
  settingsEl('settings-cancel').addEventListener('click', () => {
    settingsState.draft = settingsState.saved ? { ...settingsState.saved } : null;
    window.appView?.returnToHome?.();
  });
  settingsEl('settings-save').addEventListener('click', saveSettings);
  settingsViewBuilt = true;
}

function setSettingsStatus(message, tone) {
  const el = settingsEl('settings-status');
  if (!el) return;
  el.textContent = message || '';
  el.classList.toggle('is-error', tone === 'error');
  el.classList.toggle('is-success', tone === 'success');
}

function toggleValueLabel(key, value) {
  const match = SETTINGS_TOGGLES[key].values.find(([v]) => v === value);
  return match ? match[1] : SETTINGS_TOGGLES[key].values[0][1];
}

function renderSettingsList() {
  const list = settingsEl('settings-list');
  if (!list) return;
  list.innerHTML = '';

  if (!settingsState.draft) {
    const empty = document.createElement('div');
    empty.className = 'wifi-network-empty';
    empty.textContent = 'Loading settings…';
    list.appendChild(empty);
    return;
  }

  for (const key of SETTINGS_ORDER) {
    const isText = Object.prototype.hasOwnProperty.call(SETTINGS_TEXT_FIELDS, key);
    const row = document.createElement('button');
    row.type = 'button';
    row.className = 'wifi-network-row settings-row';

    const name = document.createElement('span');
    name.className = 'settings-row-label';
    name.textContent = isText ? SETTINGS_TEXT_FIELDS[key].label : SETTINGS_TOGGLES[key].label;

    const value = document.createElement('span');
    value.className = 'wifi-network-row-signal settings-row-value';
    value.textContent = isText
      ? (settingsState.draft[key] || 'Not set')
      : toggleValueLabel(key, settingsState.draft[key]);

    row.appendChild(name);
    row.appendChild(value);
    row.addEventListener('click', () => (isText ? openTextEditor(key) : cycleToggle(key)));
    list.appendChild(row);
  }
}

function cycleToggle(key) {
  if (settingsState.saving) return;
  const values = SETTINGS_TOGGLES[key].values.map(([v]) => v);
  const index = values.indexOf(settingsState.draft[key]);
  settingsState.draft[key] = values[(index + 1) % values.length];
  setSettingsStatus('');
  renderSettingsList();
}

function showEditor(open) {
  settingsEl('settings-edit').hidden = !open;
  settingsEl('settings-list').hidden = open;
  settingsEl('settings-actions').hidden = open;
}

function openTextEditor(key) {
  if (settingsState.saving) return;
  settingsState.editingKey = key;
  settingsEl('settings-edit-label').textContent = SETTINGS_TEXT_FIELDS[key].label;
  settingsEl('settings-edit-hint').textContent = SETTINGS_TEXT_FIELDS[key].hint;
  const input = settingsEl('settings-edit-input');
  input.value = settingsState.draft[key] || '';
  input.placeholder = SETTINGS_TEXT_FIELDS[key].label;
  settingsKeyboard?.reset();
  setSettingsStatus('');
  showEditor(true);
}

function closeTextEditor() {
  settingsState.editingKey = null;
  showEditor(false);
}

function commitTextEditor() {
  const key = settingsState.editingKey;
  if (key) settingsState.draft[key] = settingsEl('settings-edit-input').value.trim();
  closeTextEditor();
  renderSettingsList();
}

function setSettingsControlsDisabled(disabled) {
  settingsEl('settings-list').style.pointerEvents = disabled ? 'none' : '';
  ['settings-wifi', 'settings-cancel', 'settings-save'].forEach((id) => {
    settingsEl(id).disabled = disabled;
  });
}

async function saveSettings() {
  if (settingsState.saving || !settingsState.draft) return;

  const locationChanged = settingsState.draft.location !== settingsState.saved?.location;
  settingsState.saving = true;
  setSettingsControlsDisabled(true);
  setSettingsStatus(locationChanged ? 'Looking up your location…' : 'Saving…');

  try {
    const response = await fetch('/api/settings', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(settingsState.draft)
    });
    const data = await response.json().catch(() => ({}));
    if (!response.ok) throw new Error(data.error || 'Could not save settings.');

    settingsState.saved = { ...data.settings };
    setSettingsStatus('Settings saved. Restarting…', 'success');
    // The server restarts the whole app when running as the kiosk; anywhere
    // else (a dev preview) just reload so the new settings take effect.
    if (!data.restarting) window.setTimeout(() => window.location.reload(), 800);
  } catch (error) {
    settingsState.saving = false;
    setSettingsControlsDisabled(false);
    setSettingsStatus(error.message || 'Could not save settings.', 'error');
  }
}

async function loadSettings() {
  try {
    const response = await fetch('/api/settings', { cache: 'no-store' });
    const data = await response.json();
    if (!response.ok) throw new Error(data.error || 'Could not load settings.');
    settingsState.saved = { ...data };
    settingsState.draft = { ...data };
    renderSettingsList();
  } catch (error) {
    console.error('Settings load failed:', error.message);
    setSettingsStatus('Could not load settings.', 'error');
  }
}

function setupSettingsVisibilityWatcher() {
  const appShell = document.querySelector('.app-shell');
  if (!appShell) return;

  let wasOpen = false;
  const observer = new MutationObserver(() => {
    const open = appShell.classList.contains('mode-settings');
    if (!open && !appShell.classList.contains('mode-wifi')) settingsState.resumeDraft = false;
    if (open === wasOpen) return;
    wasOpen = open;
    if (!open || settingsState.saving) return;

    ensureSettingsViewBuilt();
    closeTextEditor();
    setSettingsStatus('');
    // Coming back from the Wi-Fi sub-page keeps unsaved edits; any other
    // visit starts from what's saved.
    if (settingsState.draft && settingsState.resumeDraft) {
      settingsState.resumeDraft = false;
      renderSettingsList();
    } else {
      settingsState.draft = null;
      renderSettingsList();
      loadSettings();
    }
  });

  observer.observe(appShell, { attributes: true, attributeFilter: ['class'] });
}

setupSettingsVisibilityWatcher();
