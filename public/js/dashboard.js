// ============ SYSTEM STATE VARIABLES ============
let systemArmed = false;
let esp32Connected = false;
let connectionStatus = false;
let ws = null;
let reconnectInterval = null;
let routePoints = [];
let map = null;
let carMarker = null;
let routeLine = null;

// ============ MAP INITIALIZATION ============
function initializeMap() {
    try {
        // Initialize Leaflet map with Moi University, Eldoret coordinates
        map = L.map('map').setView([0.2833, 35.3167], 14);
        
        // Apply dark theme map tiles
        L.tileLayer('https://{s}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}{r}.png', {
            attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors &copy; <a href="https://carto.com/attributions">CARTO</a>',
            subdomains: 'abcd',
            maxZoom: 19
        }).addTo(map);

        // Custom marker icon with Tesla red color
        const carIcon = L.divIcon({
            html: `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="#e82127" width="24" height="24">
                    <path d="M12 2C7.58 2 4 5.58 4 10c0 4.42 3.58 8 8 8s8-3.58 8-8c0-4.42-3.58-8-8-8zm0 14c-3.31 0-6-2.69-6-6s2.69-6 6-6 6 2.69 6 6-2.69 6-6 6z"/>
                    <circle cx="12" cy="10" r="3" fill="#e82127"/>
                  </svg>`,
            className: 'car-marker',
            iconSize: [24, 24],
            iconAnchor: [12, 12]
        });

        // Add car marker at default location
        carMarker = L.marker([0.2833, 35.3167], {icon: carIcon}).addTo(map);

        // Create empty path line for tracking
        routeLine = L.polyline(routePoints, {
            color: '#e82127',
            weight: 4,
            opacity: 0.7,
            lineJoin: 'round'
        }).addTo(map);

        // Add glowing effect to the route
        if (routeLine._path) {
            routeLine._path.classList.add('tesla-glow');
        }

        console.log('Map initialized successfully');
    } catch (error) {
        console.error('Error initializing map:', error);
        showError('Failed to initialize map. Please refresh the page.');
    }
}

// ============ WEBSOCKET CONNECTION ============
function connectWebSocket() {
    try {
        // Determine WebSocket URL based on current location
        let wsUrl;
        if (window.location.hostname === 'localhost' || window.location.hostname === '127.0.0.1') {
            // Local development
            const protocol = window.location.protocol === 'https:' ? 'wss' : 'ws';
            const port = window.location.port || '3000';
            wsUrl = `${protocol}://${window.location.hostname}:${port}`;
        } else {
            // Production server
            const protocol = window.location.protocol === 'https:' ? 'wss' : 'ws';
            wsUrl = `${protocol}://${window.location.host}`;
        }
        
        console.log('Connecting to WebSocket:', wsUrl);
        ws = new WebSocket(wsUrl);
        
        ws.onopen = () => {
            console.log('WebSocket connected');
            connectionStatus = true;
            updateConnectionUI(true);
            clearInterval(reconnectInterval);
            
            // Identify as web client
            ws.send(JSON.stringify({
                type: 'identify',
                clientType: 'web',
                timestamp: new Date().toISOString()
            }));
            
            fetchGPSHistory();
        };
        
        ws.onclose = () => {
            console.log('WebSocket disconnected');
            connectionStatus = false;
            esp32Connected = false;
            updateConnectionUI(false);
            updateESP32StatusUI();
            
            clearInterval(reconnectInterval);
            reconnectInterval = setInterval(connectWebSocket, 5000);
        };
        
        ws.onmessage = (event) => {
            try {
                const data = JSON.parse(event.data);
                handleWebSocketMessage(data);
            } catch (err) {
                console.error('Error parsing WebSocket message:', err);
            }
        };
        
        ws.onerror = (error) => {
            console.error('WebSocket error:', error);
            showError('WebSocket connection error. Attempting to reconnect...');
        };
    } catch (error) {
        console.error('Error setting up WebSocket:', error);
        showError('Failed to establish WebSocket connection.');
    }
}

// ============ WEBSOCKET MESSAGE HANDLING ============
function handleWebSocketMessage(data) {
    try {
        console.log('WebSocket message received:', data);
        
        switch (data.type) {
            case 'welcome':
                console.log('Connected to server, client ID:', data.clientId);
                break;
                
            case 'gps':
            case 'gps_data':
                if (data.payload) {
                    esp32Connected = true;
                    updateESP32StatusUI();
                    updateGPSData(data.payload);
                }
                break;
                
            case 'gps_history':
                if (data.payload && Array.isArray(data.payload)) {
                    loadGPSHistory(data.payload);
                }
                break;
                
            case 'theft_alert':
                handleTheftAlert(data.payload);
                break;
                
            case 'system_status':
                handleSystemStatusUpdate(data.payload);
                break;
                
            case 'heartbeat':
                esp32Connected = true;
                updateESP32StatusUI();
                handleHeartbeat(data.payload);
                break;
                
            default:
                console.log('Unknown message type:', data.type);
        }
    } catch (error) {
        console.error('Error handling WebSocket message:', error);
    }
}

// ============ GPS DATA HANDLING ============
function updateGPSData(data) {
    try {
        if (data.lat && data.lng && !isNaN(data.lat) && !isNaN(data.lng)) {
            // Update marker position
            carMarker.setLatLng([data.lat, data.lng]);
            
            // Add to route if it's a new point (avoid duplicates)
            const lastPoint = routePoints.length > 0 ? routePoints[routePoints.length - 1] : null;
            if (!lastPoint || Math.abs(lastPoint[0] - data.lat) > 0.0001 || Math.abs(lastPoint[1] - data.lng) > 0.0001) {
                routePoints.push([data.lat, data.lng]);
                routeLine.setLatLngs(routePoints);
            }
            
            // Update dashboard with enhanced data
            const speed = data.speed || calculateSpeedFromHistory();
            updateElement('current-speed', speed.toFixed(1));
            
            // Calculate and update total distance
            updateTotalDistance();
            
            // Update trip duration
            updateTripDuration(data.timestamp);
            
            // Center map on new location (only if not an alert)
            if (!data.isAlert) {
                map.panTo([data.lat, data.lng]);
            }
        }
    } catch (error) {
        console.error('Error updating GPS data:', error);
    }
}

function calculateSpeedFromHistory() {
    if (routePoints.length < 2) return 0;
    
    const lastPoint = routePoints[routePoints.length - 1];
    const prevPoint = routePoints[routePoints.length - 2];
    
    const distance = calculateDistance(prevPoint[0], prevPoint[1], lastPoint[0], lastPoint[1]);
    const timeInterval = 5; // 5 seconds between GPS points
    const speed = (distance / timeInterval) * 3600; // km/h
    
    return speed;
}

function updateTotalDistance() {
    if (routePoints.length > 1) {
        let totalDistance = 0;
        for (let i = 1; i < routePoints.length; i++) {
            totalDistance += calculateDistance(
                routePoints[i-1][0], routePoints[i-1][1], 
                routePoints[i][0], routePoints[i][1]
            );
        }
        updateElement('total-distance', (totalDistance * 1000).toFixed(1) + 'm');
    }
}

function updateTripDuration(timestamp) {
    if (timestamp) {
        const startTime = routePoints.length > 1 ? 
            new Date(Date.now() - (routePoints.length * 5000)) : new Date(Date.now() - 60000);
        const currentTime = new Date(timestamp);
        const diffMs = currentTime - startTime;
        const minutes = Math.floor(diffMs / 60000);
        const seconds = Math.floor((diffMs % 60000) / 1000);
        updateElement('trip-duration', 
            `${minutes.toString().padStart(2, '0')}:${seconds.toString().padStart(2, '0')}`);
    }
}

// ============ UI UPDATE FUNCTIONS ============
function updateConnectionUI(connected) {
    const indicator = document.getElementById('connectionIndicator');
    const text = document.getElementById('connectionText');
    
    if (indicator && text) {
        if (connected) {
            indicator.className = 'inline-block w-2 h-2 rounded-full bg-green-500 mr-2';
            text.textContent = 'Connected';
        } else {
            indicator.className = 'inline-block w-2 h-2 rounded-full bg-red-500 mr-2';
            text.textContent = 'Disconnected';
        }
    }
}

function updateESP32StatusUI() {
    const statusElement = document.getElementById('esp32ConnectionStatus');
    const badgeElement = document.getElementById('esp32StatusBadge');
    const dashboardStatus = document.getElementById('esp32-status');
    
    if (esp32Connected) {
        updateElement('esp32ConnectionStatus', 'Connected', 'text-lg font-semibold text-green-500');
        updateElement('esp32StatusBadge', 'Online', 'px-2 py-1 rounded text-xs bg-green-600 text-white');
        updateElement('esp32-status', 'Online', 'text-2xl font-bold text-green-500');
    } else {
        updateElement('esp32ConnectionStatus', 'Disconnected', 'text-lg font-semibold text-tesla-red');
        updateElement('esp32StatusBadge', 'Offline', 'px-2 py-1 rounded text-xs bg-red-600 text-white');
        updateElement('esp32-status', 'Offline', 'text-2xl font-bold text-red-500');
    }
}

function updateSystemUI() {
    const indicator = document.getElementById('systemIndicator');
    const text = document.getElementById('systemText');
    const securityStatus = document.getElementById('securityStatus');
    
    if (systemArmed) {
        if (indicator) indicator.className = 'inline-block w-2 h-2 rounded-full bg-tesla-red mr-2';
        updateElement('systemText', 'Armed');
        updateElement('securityStatus', 'Armed', 'text-lg font-semibold text-tesla-red');
    } else {
        if (indicator) indicator.className = 'inline-block w-2 h-2 rounded-full bg-green-500 mr-2';
        updateElement('systemText', 'Disarmed');
        updateElement('securityStatus', 'Disarmed', 'text-lg font-semibold text-green-500');
    }
}

// ============ UTILITY FUNCTIONS ============
function updateElement(elementId, text, className = null) {
    const element = document.getElementById(elementId);
    if (element) {
        element.textContent = text;
        if (className) {
            element.className = className;
        }
    }
}

function updateTime() {
    const now = new Date();
    updateElement('current-time', now.toLocaleTimeString());
}

function calculateDistance(lat1, lon1, lat2, lon2) {
    const R = 6371; // Radius of the Earth in kilometers
    const dLat = (lat2 - lat1) * Math.PI / 180;
    const dLon = (lon2 - lon1) * Math.PI / 180;
    const a = Math.sin(dLat/2) * Math.sin(dLat/2) +
            Math.cos(lat1 * Math.PI / 180) * Math.cos(lat2 * Math.PI / 180) *
            Math.sin(dLon/2) * Math.sin(dLon/2);
    const c = 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1-a));
    return R * c; // Distance in kilometers
}

function showError(message) {
    console.error(message);
    // You could also show a toast notification or modal here
    addAlert(message, 'just now', 'bg-red-500');
}

// ============ ALERT FUNCTIONS ============
function addAlert(message, time, colorClass = 'bg-tesla-red') {
    const alertsList = document.getElementById('alertsList');
    if (!alertsList) return;
    
    const alertDiv = document.createElement('div');
    alertDiv.className = 'flex items-center p-2 bg-dark-gray rounded-lg';
    alertDiv.innerHTML = `
        <div class="w-2 h-2 ${colorClass} rounded-full mr-3"></div>
        <div>
            <p class="text-sm font-medium">${message}</p>
            <p class="text-xs text-gray-400">${time}</p>
        </div>
    `;
    
    alertsList.insertBefore(alertDiv, alertsList.firstChild);
    
    // Keep only last 5 alerts
    while (alertsList.children.length > 5) {
        alertsList.removeChild(alertsList.lastChild);
    }
}

function handleTheftAlert(alertData) {
    console.log('Theft alert received:', alertData);
    
    // Show prominent alert
    const alertMessage = alertData.message || 'THEFT DETECTED!';
    alert(alertMessage);
    
    // Add to alerts list
    const timeString = new Date().toLocaleTimeString();
    addAlert(alertMessage, timeString, 'bg-tesla-red');
    
    // If GPS coordinates are available, center map on location
    if (alertData.hasGPS && alertData.lat && alertData.lng) {
        const alertLocation = {
            lat: alertData.lat,
            lng: alertData.lng,
            timestamp: new Date().toISOString(),
            isAlert: true
        };
        updateGPSData(alertLocation);
        
        // Zoom in on alert location
        map.setView([alertData.lat, alertData.lng], 18);
        
        // Add special alert marker
        addAlertMarker(alertData.lat, alertData.lng, timeString);
    }
}

function addAlertMarker(lat, lng, timeString) {
    const alertIcon = L.divIcon({
        html: `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="#ff0000" width="32" height="32">
                <path d="M12 2C7.58 2 4 5.58 4 10c0 4.42 3.58 8 8 8s8-3.58 8-8c0-4.42-3.58-8-8-8zm0 14c-3.31 0-6-2.69-6-6s2.69-6 6-6 6 2.69 6 6-2.69 6-6 6z"/>
                <circle cx="12" cy="10" r="3" fill="#ff0000"/>
                <text x="12" y="10" text-anchor="middle" fill="white" font-size="8">!</text>
              </svg>`,
        className: 'alert-marker',
        iconSize: [32, 32],
        iconAnchor: [16, 16]
    });
    
    L.marker([lat, lng], {icon: alertIcon})
     .addTo(map)
     .bindPopup(`<strong>THEFT ALERT!</strong><br>Time: ${timeString}`)
     .openPopup();
}

// ============ COMMAND FUNCTIONS ============
function sendESP32Command(command, payload = {}) {
    if (ws && ws.readyState === WebSocket.OPEN) {
        const message = {
            type: command,
            payload: payload,
            timestamp: new Date().toISOString()
        };
        ws.send(JSON.stringify(message));
        console.log('Sent command to ESP32:', message);
        return true;
    } else {
        console.error('WebSocket not connected');
        alert('Not connected to server. Please check your connection.');
        return false;
    }
}

function armSystem() {
    if (sendESP32Command('arm_system')) {
        updateLastCommand('ARM Command Sent');
        addAlert('ARM command sent to ESP32', 'just now', 'bg-tesla-red');
    }
}

function disarmSystem() {
    if (sendESP32Command('disarm_system')) {
        updateLastCommand('DISARM Command Sent');
        addAlert('DISARM command sent to ESP32', 'just now', 'bg-green-500');
    }
}

function requestStatus() {
    if (sendESP32Command('get_status')) {
        updateLastCommand('Status Requested');
    }
}

async function simulateTheft() {
    try {
        // Send command via WebSocket if connected
        if (connectionStatus) {
            if (sendESP32Command('simulate_theft')) {
                addAlert('Theft simulation sent to ESP32', 'just now', 'bg-tesla-red');
                updateLastCommand('Theft Simulation via WebSocket');
            }
        } else {
            // Fallback to HTTP API
            const response = await fetch('/api/simulate-trigger', { method: 'POST' });
            if (response.ok) {
                addAlert('Theft simulation triggered (HTTP)', 'just now', 'bg-tesla-red');
            }
        }
    } catch (err) {
        console.error('Error triggering theft simulation:', err);
        showError('Failed to trigger theft simulation');
    }
}

function updateLastCommand(command) {
    updateElement('lastCommand', command);
}

// ============ DATA FETCHING FUNCTIONS ============
async function fetchGPSHistory() {
    try {
        const response = await fetch('/api/gps-history');
        if (response.ok) {
            const history = await response.json();
            if (history.length > 0) {
                loadGPSHistory(history);
            }
        }
    } catch (err) {
        console.error('Error fetching GPS history:', err);
        showError('Failed to fetch GPS history');
    }
}

function loadGPSHistory(history) {
    console.log('Loading GPS history:', history.length, 'points');
    
    if (history.length > 0) {
        // Clear existing route
        routePoints = [];
        
        // Add all points to route
        history.forEach(point => {
            if (point.lat && point.lng && !isNaN(point.lat) && !isNaN(point.lng)) {
                routePoints.push([point.lat, point.lng]);
            }
        });
        
        // Update route line
        routeLine.setLatLngs(routePoints);
        
        // Update marker to latest position
        const latest = history[history.length - 1];
        updateGPSData(latest);
        
        // Fit map to show all points if we have multiple
        if (routePoints.length > 1) {
            map.fitBounds(routeLine.getBounds());
        } else if (routePoints.length === 1) {
            map.setView(routePoints[0], 16);
        }
    }
}

// ============ STATUS UPDATE HANDLERS ============
function handleSystemStatusUpdate(status) {
    console.log('System status update:', status);
    
    if (status === 'ARMED') {
        systemArmed = true;
        addAlert('System Armed by ESP32', 'just now', 'bg-tesla-red');
    } else if (status === 'DISARMED') {
        systemArmed = false;
        addAlert('System Disarmed by ESP32', 'just now', 'bg-green-500');
    }
    
    updateSystemUI();
    updateLastCommand(`ESP32 Status: ${status}`);
}

function handleHeartbeat(heartbeatData) {
    console.log('Heartbeat from ESP32:', heartbeatData);
    
    // Update system state based on heartbeat
    if (heartbeatData.status) {
        systemArmed = (heartbeatData.status === 'ARMED');
        updateSystemUI();
    }
    
    // Show device is alive with brief color change
    const indicator = document.getElementById('connectionIndicator');
    if (indicator) {
        const originalColor = indicator.style.backgroundColor;
        indicator.style.backgroundColor = '#00ff00';
        setTimeout(() => {
            indicator.style.backgroundColor = originalColor;
        }, 200);
    }
}

// ============ EVENT LISTENERS ============
function setupEventListeners() {
    // System control buttons
    const armBtn = document.getElementById('armBtn');
    const disarmBtn = document.getElementById('disarmBtn');
    const statusBtn = document.getElementById('statusBtn');
    const quickArmBtn = document.getElementById('quickArmBtn');
    const quickDisarmBtn = document.getElementById('quickDisarmBtn');
    const simulateTheftBtn = document.getElementById('simulateTheftBtn');

    if (armBtn) armBtn.addEventListener('click', armSystem);
    if (disarmBtn) disarmBtn.addEventListener('click', disarmSystem);
    if (statusBtn) statusBtn.addEventListener('click', requestStatus);
    if (quickArmBtn) quickArmBtn.addEventListener('click', armSystem);
    if (quickDisarmBtn) quickDisarmBtn.addEventListener('click', disarmSystem);
    if (simulateTheftBtn) simulateTheftBtn.addEventListener('click', simulateTheft);
}

// ============ INITIALIZATION ============
function initializeDashboard() {
    console.log('Initializing dashboard...');
    
    try {
        // Initialize map
        initializeMap();
        
        // Setup event listeners
        setupEventListeners();
        
        // Start time updates
        setInterval(updateTime, 1000);
        updateTime();
        
        // Initialize WebSocket connection
        connectWebSocket();
        
        // Initialize UI state
        updateESP32StatusUI();
        updateSystemUI();
        updateConnectionUI(false);
        
        // Add initial alert
        addAlert('System Ready', 'Waiting for ESP32 connection', 'bg-green-500');
        
        console.log('Dashboard initialized successfully');
    } catch (error) {
        console.error('Error initializing dashboard:', error);
        showError('Failed to initialize dashboard. Please refresh the page.');
    }
}

// ============ WINDOW LOAD EVENT ============
window.addEventListener('load', initializeDashboard);

// ============ ERROR HANDLING ============
window.addEventListener('error', (event) => {
    console.error('Global error:', event.error);
    showError('An unexpected error occurred. Please refresh the page.');
});

// ============ EXPORTS (if using modules) ============
// Uncomment if you want to use this as a module
// export { initializeDashboard, armSystem, disarmSystem, simulateTheft };
