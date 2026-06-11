// Configuration
const API_STATUS_URL = '/api/status';
const API_SET_DHW_URL = '/api/set-dhw';
const API_SET_HEATING_URL = '/api/set-heating';
const API_SET_PRIORITY_URL = '/api/set-priority';
const REFRESH_INTERVAL = 5000; // 5 seconds
const MAX_RETRIES = 3;
const RETRY_DELAY = 1000;

// Track pending user settings to prevent UI from reverting before server updates
let pendingDhwTarget = null;
let pendingHeatingTarget = null;
let pendingMode = null;  // Prevents status poll from reverting mode before command is processed

// Working mode: 'off', 'dhw', 'heat', 'both'
let currentMode = 'both';
let currentPriority = 'dhw'; // 'dhw' or 'heating'

// DOM Elements
const elements = {
    deviceStatus: document.getElementById('deviceStatus'),
    runStatus: document.getElementById('runStatus'),
    powerBtn: document.getElementById('powerBtn'),
    modeValue: document.getElementById('modeValue'),
    priorityValue: document.getElementById('priorityValue'),
    prioritySection: document.getElementById('prioritySection'),
    modeOffBtn: document.getElementById('modeOffBtn'),
    modeDhwBtn: document.getElementById('modeDhwBtn'),
    modeHeatBtn: document.getElementById('modeHeatBtn'),
    modeBothBtn: document.getElementById('modeBothBtn'),
    outdoorTempValue: document.getElementById('outdoorTempValue'),
    dhwTempValue: document.getElementById('dhwTempValue'),
    heatingTempValue: document.getElementById('heatingTempValue'),
    dhwTargetValue: document.getElementById('dhwTargetValue'),
    heatingTargetValue: document.getElementById('heatingTargetValue'),
    dhwSlider: document.getElementById('dhwSlider'),
    dhwInput: document.getElementById('dhwInput'),
    heatingSlider: document.getElementById('heatingSlider'),
    heatingInput: document.getElementById('heatingInput'),
    setDhwBtn: document.getElementById('setDhwBtn'),
    setHeatingBtn: document.getElementById('setHeatingBtn'),
    dhwPriorityBtn: document.getElementById('dhwPriorityBtn'),
    heatingPriorityBtn: document.getElementById('heatingPriorityBtn'),
    refreshCountdown: document.getElementById('refreshCountdown'),
    acPowerValue: document.getElementById('acPowerValue')
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

// Working mode buttons
function setWorkingMode(mode) {
    if (mode === currentMode) return;

    currentMode = mode;
    pendingMode = mode;  // Block status poll from reverting until server confirms
    setTimeout(() => { pendingMode = null; }, 10000);  // Safety: clear after 10s if server never confirms

    // Update button states
    elements.modeOffBtn.classList.toggle('active', mode === 'off');
    elements.modeDhwBtn.classList.toggle('active', mode === 'dhw');
    elements.modeHeatBtn.classList.toggle('active', mode === 'heat');
    elements.modeBothBtn.classList.toggle('active', mode === 'both');

    // Show/hide priority section based on mode
    elements.prioritySection.style.display = (mode === 'both') ? 'block' : 'none';

    // Send mode command to server
    const modeMap = {
        'off': 0,
        'dhw': 1,  // Will be mapped to DHW-only on backend
        'heat': 2, // Will be mapped to Heating-only on backend
        'both': 2  // DHW + Heating
    };

    apiPost('/api/set-mode', { mode: modeMap[mode], priority: currentPriority })
        .then(() => showNotification('Mode set to ' + mode, 'success'))
        .catch(error => {
            showNotification('Failed to set mode: ' + error.message, 'error');
            pendingMode = null;  // Clear pending on failure so status poll can correct
        });
}

elements.modeOffBtn.addEventListener('click', () => setWorkingMode('off'));
elements.modeDhwBtn.addEventListener('click', () => setWorkingMode('dhw'));
elements.modeHeatBtn.addEventListener('click', () => setWorkingMode('heat'));
elements.modeBothBtn.addEventListener('click', () => setWorkingMode('both'));

// Power button (ON/OFF toggle)
elements.powerBtn.addEventListener('click', () => {
    if (currentMode === 'off') {
        // Turn on: restore to DHW+Heating (default on mode)
        setWorkingMode('both');
    } else {
        // Turn off
        setWorkingMode('off');
    }
});

// Priority toggle (only active when mode is 'both')
elements.dhwPriorityBtn.addEventListener('click', () => {
    setPriority('dhw');
});

elements.heatingPriorityBtn.addEventListener('click', () => {
    setPriority('heating');
});

function setPriority(priority) {
    if (priority === currentPriority || currentMode !== 'both') return;

    currentPriority = priority;
    elements.dhwPriorityBtn.classList.toggle('active', priority === 'dhw');
    elements.heatingPriorityBtn.classList.toggle('active', priority === 'heating');

    apiPost(API_SET_PRIORITY_URL, { priority })
        .then(() => showNotification('Priority set to ' + priority, 'success'))
        .catch(error => showNotification('Failed to set priority: ' + error.message, 'error'));
}

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
function updateUI(status) {
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

    // Priority display
    elements.priorityValue.textContent = status.priority.toUpperCase();

    // Map working mode from server (0=off, 1=dhw, 2=heat, 3=both)
    const modeMap = ['off', 'dhw', 'heat', 'both'];
    const displayMode = modeMap[status.workingMode] || 'both';

    // If we have a pending mode change, only accept server state once it matches
    if (pendingMode !== null) {
        if (displayMode === pendingMode) {
            pendingMode = null;  // Server confirmed the mode - resume normal updates
        } else {
            // Server hasn't caught up yet - keep the user's choice, don't revert
        }
        // Fall through: still update currentPriority below
    } else {
        currentMode = displayMode;
    }

    currentPriority = status.priority.toLowerCase();

    // Update mode button active states (always reflect currentMode)
    elements.modeOffBtn.classList.toggle('active', currentMode === 'off');
    elements.modeDhwBtn.classList.toggle('active', currentMode === 'dhw');
    elements.modeHeatBtn.classList.toggle('active', currentMode === 'heat');
    elements.modeBothBtn.classList.toggle('active', currentMode === 'both');

    // Update power button
    if (currentMode === 'off') {
        elements.powerBtn.textContent = 'ON';
        elements.powerBtn.className = 'power-btn off';
    } else {
        elements.powerBtn.textContent = 'OFF';
        elements.powerBtn.className = 'power-btn on';
    }

    // Show/hide priority section based on mode
    elements.prioritySection.style.display = (currentMode === 'both') ? 'block' : 'none';

    // Update priority buttons
    elements.dhwPriorityBtn.classList.toggle('active', currentPriority === 'dhw');
    elements.heatingPriorityBtn.classList.toggle('active', currentPriority === 'heating');

    // Temperatures
    elements.outdoorTempValue.textContent = status.outdoorTemperature + '°C';
    elements.dhwTempValue.textContent = status.dhwTemperature + '°C';
    elements.heatingTempValue.textContent = status.heatingTemperature + '°C';

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
    const heatingTargetDisplay = pendingHeatingTarget !== null ? pendingHeatingTarget : status.heatingTarget;

    elements.dhwTargetValue.textContent = dhwTargetDisplay + '°C';
    elements.heatingTargetValue.textContent = heatingTargetDisplay + '°C';

    // Diagnostic values
    if (status.dhwValveStatus !== undefined) {
        const valveEl = document.getElementById('dhwValveValue');
        if (valveEl) valveEl.textContent = status.dhwValveStatus === 0 ? 'Opened' : (status.dhwValveStatus === 1 ? 'Closed' : 'Unknown');
    }
    if (status.actualCapacityOutput !== undefined) {
        const capEl = document.getElementById('actualCapacityValue');
        if (capEl) capEl.textContent = status.actualCapacityOutput;
    }

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

// Initialize
function init() {
    fetchStatus();
    startCountdown();
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
