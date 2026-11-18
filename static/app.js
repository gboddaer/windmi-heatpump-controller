// Configuration
const API_STATUS_URL = '/api/status';
const API_SET_DHW_URL = '/api/set-dhw';
const API_SET_HEATING_URL = '/api/set-heating';
const API_SET_PRIORITY_URL = '/api/set-priority';
const API_SET_OCCUPANCY_URL = '/api/set-occupancy';
const API_PRICE_URL = '/api/price';
const API_SCHEDULE_URL = '/api/schedule';
const API_SCHEDULE_TOGGLE_URL = '/api/schedule/toggle';
const API_SCHEDULE_RESUME_URL = '/api/schedule/resume';
const REFRESH_INTERVAL = 5000; // 5 seconds
const MAX_RETRIES = 3;
const RETRY_DELAY = 1000;

// Track pending user settings to prevent UI from reverting before server updates
let pendingDhwTarget = null;
// Posted tariff, held until /api/status confirms it, so the field does not flicker back.
let pricePending = null;
let pendingHeatingTarget = null;
let pendingMode = null;  // Prevents status poll from reverting mode before command is processed

// Working mode: 0=OFF, 1=DHW Only, 2=Heating Only, 3=Heating+DHW Priority, 4=Heating Priority+DHW
let currentMode = 3; // default: Heating + DHW Priority
let currentOccupancy = 'home'; // 'away', 'sleep', 'home'

// Schedule state
let scheduleData = { enabled: false, paused: false, defaults: null, ranges: [] };
let scheduleDefaultDraft = null;
let scheduleDefaultDirty = false;
let editingRangeIndex = -1; // -1 means adding a new range
let scheduleLoaded = false;
let scheduleBusy = false;
let scheduleLoadError = '';
let scheduleLoadPromise = null;
let scheduleRuntime = null;

// DOM Elements
const elements = {
    deviceStatus: document.getElementById('deviceStatus'),
    runStatus: document.getElementById('runStatus'),
    powerBtn: document.getElementById('powerBtn'),
    modeValue: document.getElementById('modeValue'),
    dhwDemandValue: document.getElementById('dhwDemandValue'),
    dhwEffectiveValue: document.getElementById('dhwEffectiveValue'),
    mode0Btn: document.getElementById('mode0Btn'),
    mode1Btn: document.getElementById('mode1Btn'),
    mode2Btn: document.getElementById('mode2Btn'),
    mode3Btn: document.getElementById('mode3Btn'),
    outdoorTempValue: document.getElementById('outdoorTempValue'),
    powerInputValue: document.getElementById('powerInputValue'),
    powerOutputValue: document.getElementById('powerOutputValue'),
    dailyEnergyValue: document.getElementById('dailyEnergyValue'),
    dailyCostValue: document.getElementById('dailyCostValue'),
    priceCentsInput: document.getElementById('priceCentsInput'),
    savePriceBtn: document.getElementById('savePriceBtn'),
    priceSaveState: document.getElementById('priceSaveState'),
    heatingCycleCopValue: document.getElementById('heatingCycleCopValue'),
    dhwCycleCopValue: document.getElementById('dhwCycleCopValue'),
    dhwTempValue: document.getElementById('dhwTempValue'),
    heatingTempValue: document.getElementById('heatingTempValue'),
    dhwSlider: document.getElementById('dhwSlider'),
    dhwInput: document.getElementById('dhwInput'),
    heatingSlider: document.getElementById('heatingSlider'),
    heatingInput: document.getElementById('heatingInput'),
    setDhwBtn: document.getElementById('setDhwBtn'),
    setHeatingBtn: document.getElementById('setHeatingBtn'),
    refreshCountdown: document.getElementById('refreshCountdown'),
    occupancyAwayBtn: document.getElementById('occupancyAwayBtn'),
    occupancySleepBtn: document.getElementById('occupancySleepBtn'),
    occupancyHomeBtn: document.getElementById('occupancyHomeBtn')
};

// State
let countdown = 5;
let isUpdating = false;

// Sync slider and input for DHW
elements.dhwSlider.addEventListener('input', () => {
    elements.dhwInput.value = elements.dhwSlider.value;
});

elements.dhwInput.addEventListener('input', () => {
    elements.dhwSlider.value = elements.dhwInput.value;
});

// Sync slider and input for Heating
elements.heatingSlider.addEventListener('input', () => {
    elements.heatingInput.value = elements.heatingSlider.value;
});

elements.heatingInput.addEventListener('input', () => {
    elements.heatingSlider.value = elements.heatingInput.value;
});

// Sync slider and input for DHW Hysteresis
// Set DHW Hysteresis
// Set DHW temperature
elements.setDhwBtn.addEventListener('click', async () => {
    const temperature = parseFloat(elements.dhwInput.value);
    if (isNaN(temperature) || temperature < 40 || temperature > 63) {
        alert('DHW temperature must be between 40 and 63°C');
        return;
    }

    // Set pending value immediately to update UI
    pendingDhwTarget = temperature;
    elements.setDhwBtn.disabled = true;
    elements.setDhwBtn.textContent = 'Setting...';

    // Safety: clear pending after 15s if server never confirms
    const dhwTimeout = setTimeout(() => { pendingDhwTarget = null; }, 15000);

    try {
        await apiPost(API_SET_DHW_URL, { temperature });
        showNotification('DHW temperature set successfully', 'success');
    } catch (error) {
        showNotification('Failed to set DHW temperature: ' + error.message, 'error');
        pendingDhwTarget = null; // Clear pending on failure
        clearTimeout(dhwTimeout);
    } finally {
        elements.setDhwBtn.disabled = false;
        elements.setDhwBtn.textContent = 'Set DHW';
    }
});

// Set the energy price used for the daily cost estimate
// A display setting only: it never writes a control value to the unit.
elements.savePriceBtn.addEventListener('click', async () => {
    const cents = parseFloat(elements.priceCentsInput.value);
    if (!Number.isFinite(cents) || cents < 0 || cents > 500) {
        alert('Price must be between 0 and 500 cents per kWh');
        return;
    }
    pricePending = cents;
    elements.savePriceBtn.disabled = true;
    elements.savePriceBtn.textContent = 'Saving...';
    const priceTimeout = setTimeout(() => { pricePending = null; }, 15000);
    try {
        await apiPost(API_PRICE_URL, { priceCentsPerKwh: cents });
        elements.priceSaveState.textContent = "Saved ✓ (used for today's cost estimate)";
    } catch (error) {
        pricePending = null;
        clearTimeout(priceTimeout);
        elements.priceSaveState.textContent = 'Price was not saved: ' + error.message;
        showNotification('Failed to set price: ' + error.message, 'error');
    } finally {
        elements.savePriceBtn.disabled = false;
        elements.savePriceBtn.textContent = 'Save';
    }
});

// Set Heating temperature
elements.setHeatingBtn.addEventListener('click', async () => {
    const temperature = parseFloat(elements.heatingInput.value);
    if (isNaN(temperature) || temperature < 25 || temperature > 63) {
        alert('Heating temperature must be between 25 and 63°C');
        return;
    }

    // Set pending value immediately to update UI
    pendingHeatingTarget = temperature;
    elements.setHeatingBtn.disabled = true;
    elements.setHeatingBtn.textContent = 'Setting...';

    // Safety: clear pending after 15s if server never confirms
    const heatingTimeout = setTimeout(() => { pendingHeatingTarget = null; }, 15000);

    try {
        await apiPost(API_SET_HEATING_URL, { temperature });
        showNotification('Heating temperature set successfully', 'success');
    } catch (error) {
        showNotification('Failed to set heating temperature: ' + error.message, 'error');
        pendingHeatingTarget = null; // Clear pending on failure
        clearTimeout(heatingTimeout);
    } finally {
        elements.setHeatingBtn.disabled = false;
        elements.setHeatingBtn.textContent = 'Set Heating';
    }
});

// Working mode buttons (0=OFF, 1=DHW Only, 2=Heating Only, 3=Heating+DHW Priority, 4=Heating Priority+DHW)
function setWorkingMode(mode) {
    // Even selecting the effective mode is an explicit manual override.
    if (pendingMode !== null) return;

    currentMode = mode;
    pendingMode = mode;
    setTimeout(() => { pendingMode = null; }, 10000);

    // Update button active states
    for (let i = 0; i <= 3; i++) {
        const btn = document.getElementById('mode' + i + 'Btn');
        if (btn) btn.classList.toggle('active', i === mode);
    }

    // The UI mode is the backend working mode, one to one. There is no
    // "heating priority" mode to offer: the unit is arbitrated by demand and
    // domestic hot water always comes first (docs/superpowers/specs/
    // 2026-11-11-native-dhw-only-mode-and-dhw-priority.md).
    const backendMode = mode;

    apiPost('/api/set-mode', { mode: backendMode })
        .then(() => {
            const labels = ['OFF', 'DHW Only', 'Heating Only', 'Heating + DHW (DHW first)'];
            showNotification('Mode set to ' + labels[mode], 'success');
        })
        .catch(error => {
            showNotification('Failed to set mode: ' + error.message, 'error');
            pendingMode = null;
        });
}

elements.mode0Btn.addEventListener('click', () => setWorkingMode(0));
elements.mode1Btn.addEventListener('click', () => setWorkingMode(1));
elements.mode2Btn.addEventListener('click', () => setWorkingMode(2));
elements.mode3Btn.addEventListener('click', () => setWorkingMode(3));

// Power button (ON/OFF toggle)
elements.powerBtn.addEventListener('click', () => {
    if (currentMode === 0) {
        // Turn on in manual combined mode; Schedule defaults are independent.
        setWorkingMode(3);
    } else {
        // Turn off
        setWorkingMode(0);
    }
});

// Priority is now baked into the working mode (modes 3/4).
// setPriority is kept for internal use only (called from setWorkingMode via the API).

// Occupancy mode buttons
function setOccupancyMode(mode) {
    if (mode === currentOccupancy) return;

    currentOccupancy = mode;
    elements.occupancyAwayBtn.classList.toggle('active', mode === 'away');
    elements.occupancySleepBtn.classList.toggle('active', mode === 'sleep');
    elements.occupancyHomeBtn.classList.toggle('active', mode === 'home');

    const modeMap = { 'away': 0, 'sleep': 1, 'home': 2 };
    apiPost(API_SET_OCCUPANCY_URL, { mode: modeMap[mode] })
        .then(() => showNotification('Occupancy set to ' + mode, 'success'))
        .catch(error => showNotification('Failed to set occupancy: ' + error.message, 'error'));
}

elements.occupancyAwayBtn.addEventListener('click', () => setOccupancyMode('away'));
elements.occupancySleepBtn.addEventListener('click', () => setOccupancyMode('sleep'));
elements.occupancyHomeBtn.addEventListener('click', () => setOccupancyMode('home'));

// API functions
async function apiGet(url, retries = 0) {
    try {
        const response = await fetch(url);
        if (!response.ok) {
            throw new Error('HTTP ' + response.status);
        }
        return await response.json();
    } catch (error) {
        if (retries < MAX_RETRIES) {
            await delay(RETRY_DELAY);
            return apiGet(url, retries + 1);
        }
        throw error;
    }
}

async function apiPost(url, data) {
    const response = await fetch(url, {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        },
        body: JSON.stringify(data)
    });

    if (!response.ok) {
        const error = await response.json().catch(() => ({ error: 'Unknown error' }));
        throw new Error(error.error || 'HTTP ' + response.status);
    }

    return await response.json();
}

function delay(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

// Update UI with status data
function updateControlSource(status) {
    const target = document.getElementById('controlSourceValue');
    if (!target) return;
    let text = 'Awaiting controller update';
    if (status.deviceOnline === false) text = 'Device offline — control state unconfirmed';
    else if (status.controlSource === 'manual') text = status.schedulePaused ?
        'Manual override — schedule paused' : 'Manual control';
    else if (status.controlSource === 'default') text = 'Schedule default';
    else if (status.controlSource === 'schedule') text = 'Scheduled period';
    if (status.deviceOnline !== false && status.controlSource !== 'manual' && status.schedulePending)
        text += ' — device update pending';
    if (status.schedulePersistenceError) text += ' · Warning: manual pause could not be saved';
    if (target.textContent !== text) target.textContent = text;
}
function updateUI(status) {
    updateControlSource(status);
    // Device status
    if (status.deviceOnline) {
        elements.deviceStatus.textContent = 'Online';
        elements.deviceStatus.className = 'status-indicator online';
    } else {
        elements.deviceStatus.textContent = 'Offline';
        elements.deviceStatus.className = 'status-indicator offline';
    }

    // Run status - based on actual device running status
    const isRunning = status.runningStatus && status.runningStatus !== 'off';
    if (status.deviceOnline && isRunning) {
        elements.runStatus.textContent = 'Running';
        elements.runStatus.style.color = '#22c55e';
    } else {
        elements.runStatus.textContent = 'Stopped';
        elements.runStatus.style.color = '#9ca3af';
    }

    // Mode display - show the set mode (0x002C) and actual running status (0x002D)
    // If running status differs from set mode (e.g. defrost, anti-freeze), show both
    const setModeStr = status.mode.toUpperCase();
    const runStatus = (status.runningStatus || 'off').toUpperCase();
    if (runStatus !== setModeStr && runStatus !== 'OFF') {
        // Device is in a special state (defrost, anti-freeze, etc.)
        elements.modeValue.textContent = setModeStr + ' (' + runStatus + ')';
    } else {
        elements.modeValue.textContent = setModeStr;
    }

    // DHW demand: what the arbiter is doing for the tank right now
    let demand = status.dhwState || 'unknown';
    if (status.dhwStateKnown && !status.unitActuating) {
        demand += ' (unit idle)';
    }
    if (status.requestedDeviceMode && status.requestedDeviceMode !== status.mode) {
        demand += ' → ' + status.requestedDeviceMode;
    }
    elements.dhwDemandValue.textContent = demand;
    if (status.dhwSensorFaultSuspected) {
        elements.dhwDemandValue.textContent = demand + ' · sensor suspect';
    }

    // The UI mode is the backend working mode
    const serverMode = status.workingMode || 0;
    let displayMode;
    if (serverMode >= 0 && serverMode <= 3) displayMode = serverMode;
    else displayMode = 3; // default

    // If we have a pending mode change, only accept server state once it matches
    if (pendingMode !== null) {
        if (displayMode === pendingMode) {
            pendingMode = null;  // Server confirmed the mode - resume normal updates
        } else {
            // Server hasn't caught up yet - keep the user's choice, don't revert
        }
    } else {
        currentMode = displayMode;
    }


    // Update mode button active states (always reflect currentMode)
    for (let i = 0; i <= 4; i++) {
        const btn = document.getElementById('mode' + i + 'Btn');
        if (btn) btn.classList.toggle('active', i === currentMode);
    }

    // Update power button
    if (currentMode === 0) {
        elements.powerBtn.textContent = 'ON';
        elements.powerBtn.className = 'power-btn off';
    } else {
        elements.powerBtn.textContent = 'OFF';
        elements.powerBtn.className = 'power-btn on';
    }

    // Temperatures
    elements.outdoorTempValue.textContent = status.outdoorTemperature + '°C';
    elements.dhwTempValue.textContent = status.dhwTemperature + '°C';
    elements.heatingTempValue.textContent = status.heatingTemperature + '°C';

    // The backend already applies the raw/2 current hypothesis. Never scale again here.
    elements.powerInputValue.textContent = status.deviceOnline && status.powerValid && Number.isFinite(status.acPowerW)
        ? Math.round(status.acPowerW) + ' W' : 'Unavailable';
    elements.powerOutputValue.textContent = status.deviceOnline && status.heatOutputValid && Number.isFinite(status.heatOutputW)
        ? Math.round(status.heatOutputW) + ' W' : 'Unavailable';

    // Estimated input energy for today and its cost. The backend owns kWh and the
    // tariff; the multiplication lives here so a price change needs no request.
    const priceCents = Number.isFinite(status.pricePerKwhCents) ? status.pricePerKwhCents : 30;
    const energyKnown = status.dailyEnergyValid === true && Number.isFinite(status.dailyEnergyKwh);
    elements.dailyEnergyValue.textContent = energyKnown
        ? status.dailyEnergyKwh.toFixed(2) + ' kWh' : 'Unavailable';
    elements.dailyCostValue.textContent = energyKnown
        ? '€' + (status.dailyEnergyKwh * priceCents / 100).toFixed(2) : 'Unavailable';
    if (pricePending !== null && Math.abs(priceCents - pricePending) < 0.05) {
        pricePending = null; // the server has it
    }
    if (pricePending === null && document.activeElement !== elements.priceCentsInput) {
        elements.priceCentsInput.value = String(priceCents);
    }
    elements.heatingCycleCopValue.textContent = Number.isFinite(status.heatingCycleCop)
        ? status.heatingCycleCop.toFixed(2) : 'Awaiting cycle';
    elements.dhwCycleCopValue.textContent = Number.isFinite(status.dhwCycleCop)
        ? status.dhwCycleCop.toFixed(2) : 'Awaiting cycle';

    // Use pending values if set, otherwise use server values
    if (pendingDhwTarget !== null) {
        // Clear pending once server confirms the value (allow for rounding)
        if (Math.abs(status.dhwTarget - pendingDhwTarget) < 1) {
            pendingDhwTarget = null;
        }
    }
    if (pendingHeatingTarget !== null) {
        if (Math.abs(status.heatingTarget - pendingHeatingTarget) < 1) {
            pendingHeatingTarget = null;
        }
    }
    const dhwTargetDisplay = pendingDhwTarget !== null ? pendingDhwTarget : status.dhwTarget;

    // Always show the effective setpoint, including when both unit targets agree.
    elements.dhwEffectiveValue.textContent = status.deviceOnline && Number.isFinite(status.effectiveDhwTarget) && status.effectiveDhwTarget > 0
        ? status.effectiveDhwTarget.toFixed(1) + ' °C' : 'Unavailable';
    const heatingTargetDisplay = pendingHeatingTarget !== null ? pendingHeatingTarget : status.heatingTarget;


    DiagnosticsUI.update(status);

    // Occupancy mode from server (0=Away, 1=Sleep, 2=Home)
    if (status.occupancyMode !== undefined) {
        const occupancyMap = ['away', 'sleep', 'home'];
        const serverOccupancy = occupancyMap[status.occupancyMode] || 'home';
        if (serverOccupancy !== currentOccupancy) {
            currentOccupancy = serverOccupancy;
        }
        elements.occupancyAwayBtn.classList.toggle('active', currentOccupancy === 'away');
        elements.occupancySleepBtn.classList.toggle('active', currentOccupancy === 'sleep');
        elements.occupancyHomeBtn.classList.toggle('active', currentOccupancy === 'home');
    }

    // Schedule state from status
    if (status.scheduleActive !== undefined) updateScheduleStatus(status);

    // Last update
    const now = new Date();

    // Update sliders if values are within range (use pending values if set)
    if (dhwTargetDisplay >= 40 && dhwTargetDisplay <= 63) {
        elements.dhwSlider.value = dhwTargetDisplay;
        elements.dhwInput.value = dhwTargetDisplay;
    }
    if (heatingTargetDisplay >= 25 && heatingTargetDisplay <= 63) {
        elements.heatingSlider.value = heatingTargetDisplay;
        elements.heatingInput.value = heatingTargetDisplay;
    }

}

function formatMode(mode) {
    const modes = {
        'off': 'Off',
        'cool': 'Cool',
        'heat': 'Heat',
        'dhw': 'DHW',
        'defrost': 'Defrost',
        'antifreeze': 'Antifreeze',
        'unknown': 'Unknown'
    };
    const text = modes[mode] || mode;
    return text.charAt(0).toUpperCase() + text.slice(1);
}

// Show notification
function showNotification(message, type) {
    // Create notification element
    const notification = document.createElement('div');
    notification.className = 'notification ' + type;
    notification.textContent = message;
    notification.style.cssText = `
        position: fixed;
        top: 20px;
        right: 20px;
        padding: 16px 24px;
        border-radius: 8px;
        color: #fff;
        font-weight: 500;
        z-index: 1000;
        animation: slideIn 0.3s ease;
        background: ${type === 'success' ? '#10b981' : '#ef4444'};
    `;

    document.body.appendChild(notification);

    // Remove after 3 seconds
    setTimeout(() => {
        notification.style.animation = 'slideOut 0.3s ease';
        setTimeout(() => notification.remove(), 300);
    }, 3000);
}

// Fetch and update
async function fetchStatus() {
    if (isUpdating) return;

    isUpdating = true;

    try {
        const status = await apiGet(API_STATUS_URL);
        updateUI(status);
    } catch (error) {
        console.error('Failed to fetch status:', error);
        DiagnosticsUI.connectionError();
        elements.deviceStatus.textContent = 'Error';
        elements.deviceStatus.className = 'status-indicator offline';
        // Clear pending values so they don't block server updates
        pendingDhwTarget = null;
        pendingHeatingTarget = null;
        pendingMode = null;
    } finally {
        isUpdating = false;
    }
}

// Countdown timer
function startCountdown() {
    setInterval(() => {
        countdown--;
        if (countdown <= 0) {
            countdown = 5;
            fetchStatus();
        }
        elements.refreshCountdown.textContent = countdown;
    }, 1000);
}

// Tab switching
document.querySelectorAll('.tab-btn').forEach(btn => {
    btn.addEventListener('click', () => {
        const tabId = btn.dataset.tab;
        document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
        document.querySelectorAll('.tab-panel').forEach(p => p.classList.remove('active'));
        btn.classList.add('active');
        document.getElementById('tab-' + tabId).classList.add('active');
    });
});

// ---- Schedule Functions ----

function applyScheduleResponse(data) {
    if (!data || typeof data.enabled !== 'boolean' || typeof data.paused !== 'boolean' || !Array.isArray(data.ranges))
        throw new Error('Invalid schedule response');
    if (data.instanceId && scheduleData.instanceId && data.instanceId !== scheduleData.instanceId) {
        scheduleLoadError = 'Controller restarted — reload page before editing';
        throw new Error(scheduleLoadError);
    }
    if (Number.isInteger(data.revision) && Number.isInteger(scheduleData.revision) && data.revision < scheduleData.revision) return false;
    if (!validScheduleDefaults(data.defaults)) throw new Error('Invalid schedule defaults in response');
    scheduleData = {...data, defaults: {...data.defaults}, ranges: data.ranges.map(r => ({...r}))};
    if (!scheduleDefaultDirty) scheduleDefaultDraft = {...data.defaults};
    scheduleLoaded = true;
    scheduleLoadError = '';
    if (!scheduleRuntime || scheduleRuntime.revision !== data.revision) scheduleRuntime = null;
    return true;
}

function loadSchedule() {
    if (scheduleLoadPromise) return scheduleLoadPromise;
    scheduleLoadPromise = (async () => {
        try {
            applyScheduleResponse(await apiGet(API_SCHEDULE_URL));
        } catch (e) {
            scheduleLoadError = 'Schedule unavailable — reload before editing';
            console.error('Failed to load schedule:', e);
        } finally {
            scheduleLoadPromise = null;
            renderSchedule();
        }
    })();
    return scheduleLoadPromise;
}

function updateScheduleStatus(status) {
    if (scheduleBusy) return;
    const revision = status.scheduleRevision;
    if (status.scheduleInstanceId && scheduleData.instanceId && status.scheduleInstanceId !== scheduleData.instanceId) {
        scheduleLoadError = 'Controller restarted — reload page before editing';
        renderSchedule();
        return;
    }
    if (Number.isInteger(revision) && Number.isInteger(scheduleData.revision) && revision < scheduleData.revision) return;
    if (typeof status.scheduleEnabled === 'boolean') scheduleData.enabled = status.scheduleEnabled;
    scheduleData.paused = status.schedulePaused === true;
    scheduleRuntime = {revision, source: status.controlSource, active: status.scheduleActive, pending: status.schedulePending === true,
        online: status.deviceOnline, persistenceError: status.schedulePersistenceError === true};
    if (!scheduleLoaded || (Number.isInteger(revision) && revision !== scheduleData.revision)) loadSchedule();
    renderSchedule();
}

function minutesToTime(m) {
    const hh = String(Math.floor(m / 60)).padStart(2, '0');
    const mm = String(m % 60).padStart(2, '0');
    return hh + ':' + mm;
}

function timeToMinutes(t) {
    if (!/^([01]\d|2[0-3]):[0-5]\d$/.test(t)) return NaN;
    const [h, m] = t.split(':').map(Number);
    return h * 60 + m;
}

function validateScheduleRanges(ranges) {
    const occupied = new Uint8Array(1440);
    for (const r of ranges) {
        if (!Number.isInteger(r.start) || r.start < 0 || r.start >= 1440 || !Number.isInteger(r.end) ||
            r.end < 0 || r.end > 1440 || r.start === r.end) return 'Choose a valid, non-empty time range';
        if (!Number.isInteger(r.mode) || r.mode < 0 || r.mode > 3) return 'Choose a valid mode';
        if (!Number.isFinite(r.heatingTemp) || r.heatingTemp < 25 || r.heatingTemp > 63 ||
            !Number.isFinite(r.dhwTemp) || r.dhwTemp < 40 || r.dhwTemp > 63)
            return 'Heating must be 25–63 °C and DHW 40–63 °C';
        for (let m = 0; m < 1440; m++) {
            const inside = r.start < r.end ? m >= r.start && m < r.end : m >= r.start || m < r.end;
            if (inside && occupied[m]) return 'Time ranges overlap, including across midnight';
            if (inside) occupied[m] = 1;
        }
    }
    return '';
}

function modeBadgeClass(mode) {
    if (mode === 0) return 'off';
    if (mode === 1) return 'dhw';
    if (mode === 2) return 'heating';
    return 'both';
}

function modeLabel(mode) {
    const labels = ['OFF', 'DHW Only', 'Heating Only', 'Heating + DHW'];
    return labels[mode] || 'Unknown';
}

function validScheduleDefaults(value) {
    return value && Number.isInteger(value.mode) && value.mode >= 0 && value.mode <= 3 &&
        Number.isFinite(value.heatingTemp) && value.heatingTemp >= 25 && value.heatingTemp <= 63 &&
        Number.isFinite(value.dhwTemp) && value.dhwTemp >= 40 && value.dhwTemp <= 63;
}
function captureScheduleDefaultDraft() {
    scheduleDefaultDraft = {
        mode: document.getElementById('scheduleDefaultMode').value,
        heatingTemp: document.getElementById('scheduleDefaultHeating').value,
        dhwTemp: document.getElementById('scheduleDefaultDhw').value
    };
    scheduleDefaultDirty = true;
    const note = document.getElementById('scheduleDefaultState');
    if (note) note.textContent = 'Unsaved default changes — current default unchanged';
}
function renderScheduleDefaults(unavailable) {
    if (!scheduleDefaultDraft && scheduleLoaded && validScheduleDefaults(scheduleData.defaults))
        scheduleDefaultDraft = {...scheduleData.defaults};
    for (const [id,key] of [['scheduleDefaultMode','mode'],['scheduleDefaultHeating','heatingTemp'],['scheduleDefaultDhw','dhwTemp']]) {
        const input = document.getElementById(id);
        if (!input) continue;
        input.disabled = scheduleBusy || unavailable;
        if (scheduleDefaultDraft && String(input.value) !== String(scheduleDefaultDraft[key])) input.value = scheduleDefaultDraft[key];
    }
    const save = document.getElementById('saveScheduleDefaultBtn');
    if (save) save.disabled = scheduleBusy || unavailable;
    const note = document.getElementById('scheduleDefaultState');
    if (note) note.textContent = unavailable ? 'Waiting for saved default settings' : scheduleDefaultDirty ?
        'Unsaved default changes — current default unchanged' : 'Saved default — used outside periods while scheduling is enabled and not paused';
}
async function saveScheduleDefault() {
    if (scheduleBusy || !scheduleLoaded || scheduleLoadError) return;
    captureScheduleDefaultDraft();
    const draft = scheduleDefaultDraft;
    const defaults = {mode:Number(draft.mode),heatingTemp:Number(draft.heatingTemp),dhwTemp:Number(draft.dhwTemp)};
    if (Object.values(draft).some(v => String(v).trim() === '') || !validScheduleDefaults(defaults)) {
        showNotification('Default requires a valid mode, heating 25–63°C and DHW 40–63°C', 'error'); return;
    }
    if (await performScheduleAction(API_SCHEDULE_URL + '/defaults', defaults, 'Schedule default saved')) {
        scheduleDefaultDirty = false;
        scheduleDefaultDraft = {...scheduleData.defaults};
        renderSchedule();
    }
}
function renderSchedule() {
    // Toggle
    const toggle = document.getElementById('scheduleEnabled');
    const unavailable = !scheduleLoaded || Boolean(scheduleLoadError);
    renderScheduleDefaults(unavailable);
    if (toggle) {
        toggle.checked = scheduleData.enabled;
        toggle.disabled = scheduleBusy || unavailable;
    }
    const resume = document.getElementById('resumeScheduleBtn');
    if (resume) {
        resume.hidden = !scheduleData.paused;
        resume.disabled = scheduleBusy || unavailable || !scheduleData.enabled;
    }
    for (const id of ['addRangeBtn', 'editSaveBtn', 'editDeleteBtn']) {
        const button = document.getElementById(id);
        if (button) button.disabled = scheduleBusy || unavailable;
    }
    for (const id of ['editStart', 'editEnd', 'editMode', 'editHeatingTemp', 'editDhwTemp', 'editCancelBtn']) {
        const input = document.getElementById(id);
        if (input) input.disabled = scheduleBusy;
    }

    // State indicator
    const stateEl = document.getElementById('scheduleState');
    if (stateEl) {
        stateEl.className = 'schedule-state';
        if (scheduleBusy) stateEl.textContent = 'Saving schedule…';
        else if (scheduleLoadError) stateEl.textContent = scheduleLoadError;
        else if (!scheduleLoaded) stateEl.textContent = 'Loading schedule…';
        else if (!scheduleData.enabled) stateEl.textContent = scheduleData.paused ?
            'Schedule disabled — manual pause retained; enable then Resume' : 'Schedule disabled';
        else if (scheduleData.paused) {
            stateEl.textContent = 'Schedule paused — manual control active; press Resume to continue';
            stateEl.className = 'schedule-state paused';
        } else if (scheduleRuntime && scheduleRuntime.online === false)
            stateEl.textContent = 'Schedule enabled — waiting for device connection';
        else if (scheduleRuntime && scheduleRuntime.pending)
            stateEl.textContent = (scheduleRuntime.source === 'default' ? 'Schedule default' :
                scheduleRuntime.source === 'schedule' ? 'Scheduled period' : 'Schedule enabled') + ' — device update pending';
        else if (scheduleRuntime && scheduleRuntime.source === 'default')
            stateEl.textContent = scheduleData.ranges.length ? 'Schedule default active — outside configured periods' : 'Schedule default active — no periods configured';
        else if (scheduleRuntime && scheduleRuntime.active === true) {
            stateEl.textContent = 'Schedule active';
            stateEl.className = 'schedule-state active';
        } else stateEl.textContent = 'Schedule enabled — awaiting controller update';
        if (scheduleData.persistenceError || (scheduleRuntime && scheduleRuntime.persistenceError)) {
            stateEl.textContent += ' · Warning: pause could not be saved; restart may lose the override';
            stateEl.className = 'schedule-state paused';
        }
    }

    // Ranges
    const container = document.getElementById('scheduleRanges');
    if (!container) return;
    container.innerHTML = '';

    if (scheduleData.ranges.length === 0) {
        container.innerHTML = '<p class="placeholder-text">No periods configured. The saved default applies whenever scheduling is enabled and not paused.</p>';
        return;
    }

    scheduleData.ranges.forEach((r, i) => {
        const card = document.createElement('div');
        card.className = 'schedule-range-card';
        for (const [className, value] of [
            ['range-time', minutesToTime(r.start) + ' – ' + minutesToTime(r.end)],
            ['range-mode-badge ' + modeBadgeClass(r.mode), modeLabel(r.mode)],
            ['range-temps', 'H:' + r.heatingTemp + '° D:' + r.dhwTemp + '°'], ['range-edit-icon', '✎']]) {
            const span = document.createElement('span');
            span.className = className;
            span.textContent = value;
            card.appendChild(span);
        }
        card.addEventListener('click', () => openEditModal(i));
        container.appendChild(card);
    });
}

function openEditModal(index) {
    if (scheduleBusy || !scheduleLoaded || scheduleLoadError) return;
    editingRangeIndex = index;
    const modal = document.getElementById('scheduleEditModal');
    const title = document.getElementById('editModalTitle');
    const deleteBtn = document.getElementById('editDeleteBtn');

    if (index >= 0) {
        const r = scheduleData.ranges[index];
        title.textContent = 'Edit Range';
        deleteBtn.style.display = '';
        document.getElementById('editStart').value = minutesToTime(r.start);
        document.getElementById('editEnd').value = minutesToTime(r.end === 1440 ? 0 : r.end);
        document.getElementById('editMode').value = r.mode;
        document.getElementById('editHeatingTemp').value = r.heatingTemp;
        document.getElementById('editDhwTemp').value = r.dhwTemp;
    } else {
        title.textContent = 'Add Range';
        deleteBtn.style.display = 'none';
        document.getElementById('editStart').value = '08:00';
        document.getElementById('editEnd').value = '10:00';
        document.getElementById('editMode').value = '3';
        document.getElementById('editHeatingTemp').value = '40';
        document.getElementById('editDhwTemp').value = '45';
    }
    modal.classList.add('visible');
}

function closeEditModal() {
    document.getElementById('scheduleEditModal').classList.remove('visible');
    editingRangeIndex = -1;
}

async function performScheduleAction(url, payload, message, closeEditor = false) {
    if (scheduleBusy || !scheduleLoaded || scheduleLoadError) return false;
    scheduleBusy = true;
    renderSchedule();
    try {
        const response = await apiPost(url, payload);
        // Older backends return only {success:true}; reload rather than guessing.
        if (!applyScheduleResponse(Array.isArray(response.ranges) ? response : await apiGet(API_SCHEDULE_URL)))
            throw new Error('Newer schedule settings received; review and retry');
        if (closeEditor) closeEditModal();
        showNotification(message, 'success');
        return true;
    } catch (e) {
        showNotification('Schedule change failed: ' + e.message, 'error');
        return false;
    } finally {
        scheduleBusy = false;
        renderSchedule();
    }
}

async function saveEditModal() {
    if (scheduleBusy || !scheduleLoaded || scheduleLoadError) return;
    const end = timeToMinutes(document.getElementById('editEnd').value);
    const range = {
        start: timeToMinutes(document.getElementById('editStart').value),
        end: end === 0 ? 1440 : end,
        mode: Number(document.getElementById('editMode').value),
        heatingTemp: Number(document.getElementById('editHeatingTemp').value),
        dhwTemp: Number(document.getElementById('editDhwTemp').value)
    };
    const ranges = scheduleData.ranges.map(r => ({...r}));
    if (editingRangeIndex >= 0) ranges[editingRangeIndex] = range;
    else ranges.push(range);
    ranges.sort((a, b) => a.start - b.start);
    const error = validateScheduleRanges(ranges);
    if (error) { showNotification(error, 'error'); return; }
    await performScheduleAction(API_SCHEDULE_URL, {enabled: scheduleData.enabled, ranges}, 'Schedule range saved', true);
}

async function deleteEditModal() {
    if (editingRangeIndex < 0 || scheduleBusy) return;
    const ranges = scheduleData.ranges.filter((_, i) => i !== editingRangeIndex).map(r => ({...r}));
    await performScheduleAction(API_SCHEDULE_URL, {enabled: scheduleData.enabled, ranges},
        ranges.length ? 'Range deleted' : 'Last period deleted; default-only policy saved', true);
}

// Schedule event bindings
function bindScheduleEvents() {
    for (const id of ['scheduleDefaultMode','scheduleDefaultHeating','scheduleDefaultDhw']) {
        const input = document.getElementById(id);
        if (input) for (const event of ['input','change']) input.addEventListener(event,captureScheduleDefaultDraft);
    }
    const defaultSave = document.getElementById('saveScheduleDefaultBtn');
    if (defaultSave) defaultSave.addEventListener('click',saveScheduleDefault);
    // Toggle enable/disable
    const toggle = document.getElementById('scheduleEnabled');
    if (toggle) {
        toggle.addEventListener('change', async () => {
            const wanted = toggle.checked;
            await performScheduleAction(API_SCHEDULE_TOGGLE_URL, {enabled: wanted},
                'Schedule ' + (wanted ? 'enabled' : 'disabled'));
        });
    }

    const resume = document.getElementById('resumeScheduleBtn');
    if (resume) resume.addEventListener('click', async () => {
        if (!scheduleData.enabled) return;
        await performScheduleAction(API_SCHEDULE_RESUME_URL, {}, 'Schedule resumed');
    });

    // Add range button
    const addBtn = document.getElementById('addRangeBtn');
    if (addBtn) {
        addBtn.addEventListener('click', () => openEditModal(-1));
    }

    // Modal buttons
    const cancelBtn = document.getElementById('editCancelBtn');
    if (cancelBtn) cancelBtn.addEventListener('click', closeEditModal);

    const saveBtn = document.getElementById('editSaveBtn');
    if (saveBtn) saveBtn.addEventListener('click', saveEditModal);

    const deleteBtn = document.getElementById('editDeleteBtn');
    if (deleteBtn) deleteBtn.addEventListener('click', deleteEditModal);
}

// Initialize
function init() {
    fetchStatus();
    startCountdown();
    bindScheduleEvents();
    loadSchedule();
}

// Add CSS for notification animations
const style = document.createElement('style');
style.textContent = `
    @keyframes slideIn {
        from {
            transform: translateX(100%);
            opacity: 0;
        }
        to {
            transform: translateX(0);
            opacity: 1;
        }
    }
    @keyframes slideOut {
        from {
            transform: translateX(0);
            opacity: 1;
        }
        to {
            transform: translateX(100%);
            opacity: 0;
        }
    }
`;
document.head.appendChild(style);

// Start the app
init();
