// Configuration
const API_STATUS_URL = '/api/status';
const API_SET_DHW_URL = '/api/set-dhw';
const API_SET_HEATING_URL = '/api/set-heating';
const API_SET_PRIORITY_URL = '/api/set-priority';
const REFRESH_INTERVAL = 5000; // 5 seconds
const MAX_RETRIES = 3;
const RETRY_DELAY = 1000;

// DOM Elements
const elements = {
    deviceStatus: document.getElementById('deviceStatus'),
    runStatus: document.getElementById('runStatus'),
    modeValue: document.getElementById('modeValue'),
    priorityValue: document.getElementById('priorityValue'),
    outdoorTempValue: document.getElementById('outdoorTempValue'),
    lastUpdate: document.getElementById('lastUpdate'),
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
    refreshCountdown: document.getElementById('refreshCountdown')
};

// State
let currentPriority = 'dhw';
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

// Set DHW temperature
elements.setDhwBtn.addEventListener('click', async () => {
    const temperature = parseFloat(elements.dhwInput.value);
    if (isNaN(temperature) || temperature < 40 || temperature > 63) {
        alert('DHW temperature must be between 40 and 63°C');
        return;
    }
    
    elements.setDhwBtn.disabled = true;
    elements.setDhwBtn.textContent = 'Setting...';
    
    try {
        await apiPost(API_SET_DHW_URL, { temperature });
        showNotification('DHW temperature set successfully', 'success');
    } catch (error) {
        showNotification('Failed to set DHW temperature: ' + error.message, 'error');
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
    
    elements.setHeatingBtn.disabled = true;
    elements.setHeatingBtn.textContent = 'Setting...';
    
    try {
        await apiPost(API_SET_HEATING_URL, { temperature });
        showNotification('Heating temperature set successfully', 'success');
    } catch (error) {
        showNotification('Failed to set heating temperature: ' + error.message, 'error');
    } finally {
        elements.setHeatingBtn.disabled = false;
        elements.setHeatingBtn.textContent = 'Set Heating';
    }
});

// Priority toggle
elements.dhwPriorityBtn.addEventListener('click', () => {
    setPriority('dhw');
});

elements.heatingPriorityBtn.addEventListener('click', () => {
    setPriority('heating');
});

function setPriority(priority) {
    if (priority === currentPriority) return;
    
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
    
    // Run status
    if (status.status === 'running') {
        elements.runStatus.textContent = 'Running';
        elements.runStatus.className = 'status-indicator running';
    } else {
        elements.runStatus.textContent = 'Stopped';
        elements.runStatus.className = 'status-indicator stopped';
    }
    
    // Mode
    elements.modeValue.textContent = formatMode(status.mode);
    
    // Priority
    elements.priorityValue.textContent = status.priority.toUpperCase();
    
    // Temperatures
    elements.outdoorTempValue.textContent = status.outdoorTemperature + '°C';
    elements.dhwTempValue.textContent = status.dhwTemperature + '°C';
    elements.heatingTempValue.textContent = status.heatingTemperature + '°C';
    elements.dhwTargetValue.textContent = status.dhwTarget + '°C';
    elements.heatingTargetValue.textContent = status.heatingTarget + '°C';
    
    // Last update
    const now = new Date();
    elements.lastUpdate.textContent = now.toLocaleTimeString();
    
    // Update sliders if values are within range
    if (status.dhwTarget >= 40 && status.dhwTarget <= 63) {
        elements.dhwSlider.value = status.dhwTarget;
        elements.dhwInput.value = status.dhwTarget;
    }
    if (status.heatingTarget >= 25 && status.heatingTarget <= 63) {
        elements.heatingSlider.value = status.heatingTarget;
        elements.heatingInput.value = status.heatingTarget;
    }
    
    // Update priority buttons
    currentPriority = status.priority;
    elements.dhwPriorityBtn.classList.toggle('active', status.priority === 'dhw');
    elements.heatingPriorityBtn.classList.toggle('active', status.priority === 'heating');
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
