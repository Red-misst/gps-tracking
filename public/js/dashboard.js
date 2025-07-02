// WebSocket connection and dashboard functionality
let socket;
let map;
let markers = [];
let path;
let isConnected = false;
let systemStatus = 'DISARMED';
let tripStartTime;
let totalDistance = 0;
let lastPosition = null;
let deviceConnected = false;

// Initialize the map
function initMap() {
    // Default location (Nairobi, Kenya)
    const defaultLocation = [-1.2921, 36.8219];
    
    map = L.map('map').setView(defaultLocation, 15);
    
    L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
        attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors'
    }).addTo(map);
    
    // Initialize path
    path = L.polyline([], { color: 'red', weight: 4 }).addTo(map);
}

// Connect to WebSocket server
function connectWebSocket() {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${window.location.host}`;
    
    console.log(`Connecting to WebSocket server at ${wsUrl}`);
    
    socket = new WebSocket(wsUrl);
    
    socket.onopen = () => {
        console.log('WebSocket connected');
        isConnected = true;
        updateConnectionStatus('Connected');
        
        // Identify as web client
        socket.send(JSON.stringify({
            type: 'identify',
            clientType: 'web'
        }));
        
        // Request initial status
        getStatus();
    };
    
    socket.onmessage = (event) => {
        console.log('Message received:', event.data);
        handleMessage(JSON.parse(event.data));
    };
    
    socket.onclose = () => {
        console.log('WebSocket disconnected');
        isConnected = false;
        updateConnectionStatus('Disconnected');
        
        // Try to reconnect after 5 seconds
        setTimeout(connectWebSocket, 5000);
    };
    
    socket.onerror = (error) => {
        console.error('WebSocket error:', error);
        isConnected = false;
        updateConnectionStatus('Connection Error');
    };
}

// Handle incoming WebSocket messages
function handleMessage(message) {
    switch (message.type) {
        case 'init':
            handleInitData(message.payload);
            break;
            
        case 'gps':
            handleGpsData(message.payload);
            break;
            
        case 'system_status':
            handleSystemStatus(message.payload);
            break;
            
        case 'device_status':
            handleDeviceStatus(message.payload);
            break;
            
        case 'theft_alert':
            handleTheftAlert(message.payload);
            break;
            
        case 'config_update':
            handleConfigUpdate(message.payload);
            break;
            
        default:
            console.log('Unknown message type:', message.type);
    }
}

// Initialize with data from server
function handleInitData(data) {
    console.log('Initializing with data:', data);
    
    // Set system status
    handleSystemStatus(data.systemStatus);
    
    
    
    // Set SIM number
    if (data.config && data.config.simNumber) {
        document.getElementById('simNumberDisplay').textContent = data.config.simNumber;
    }
    
    // Initialize GPS history
    if (data.gpsHistory && data.gpsHistory.length > 0) {
        // Sort by timestamp
        data.gpsHistory.sort((a, b) => new Date(a.timestamp) - new Date(b.timestamp));
        
        // Initialize the map path
        const points = data.gpsHistory.map(point => [point.lat, point.lng]);
        path.setLatLngs(points);
        
        // Add markers
        data.gpsHistory.forEach(point => {
            addMarker([point.lat, point.lng]);
        });
        
        // Center map on the most recent point
        const lastPoint = data.gpsHistory[data.gpsHistory.length - 1];
        map.setView([lastPoint.lat, lastPoint.lng], 15);
        
        // Calculate distance
        calculateTripDistance(data.gpsHistory);
        
        // Update timestamp
        document.getElementById('lastGpsUpdate').textContent = 
            new Date(lastPoint.timestamp).toLocaleTimeString();
    }
}

// Handle GPS data update
function handleGpsData(data) {
    console.log('GPS data received:', data);
    
    // Update map
    const position = [data.lat, data.lng];
    
    // Update markers
    if (markers.length > 0) {
        // Move the last marker
        const lastMarker = markers[markers.length - 1];
        lastMarker.setLatLng(position);
    } else {
        // Add a new marker
        addMarker(position);
    }
    
    // Update path
    const currentPath = path.getLatLngs();
    currentPath.push(position);
    path.setLatLngs(currentPath);
    
    // Center map on the most recent position
    map.setView(position, map.getZoom());
    
    // Update coordinates display
    document.getElementById('latValue').textContent = data.lat.toFixed(6);
    document.getElementById('lngValue').textContent = data.lng.toFixed(6);
    document.getElementById('lastGpsUpdate').textContent = 
        new Date(data.timestamp).toLocaleTimeString();
    
    // Calculate distance traveled if we have a previous position
    if (lastPosition) {
        const distance = calculateDistance(
            lastPosition[0], lastPosition[1],
            data.lat, data.lng
        );
        totalDistance += distance;
        document.getElementById('total-distance').textContent = Math.round(totalDistance);
        
        // Calculate speed (if timestamp is available)
        if (data.timestamp && lastPosition[2]) {
            const timeDiff = new Date(data.timestamp) - new Date(lastPosition[2]);
            if (timeDiff > 0) {
                // Convert to seconds and then to hours
                const timeInHours = timeDiff / 1000 / 3600;
                // Distance in kilometers
                const distanceInKm = distance / 1000;
                // Speed in km/h
                const speed = distanceInKm / timeInHours;
                document.getElementById('current-speed').textContent = Math.round(speed);
            }
        }
    }
    
    // Update last position
    lastPosition = [data.lat, data.lng, data.timestamp];
    
    // Start trip timer if not already started
    if (!tripStartTime) {
        tripStartTime = new Date();
        updateTripDuration();
    }
}

// Add a marker to the map
function addMarker(position) {
    const marker = L.marker(position).addTo(map);
    markers.push(marker);
    
    // Limit the number of markers to prevent memory issues
    if (markers.length > 10) {
        map.removeLayer(markers.shift());
    }
}

// Handle system status update
function handleSystemStatus(status) {
    systemStatus = status;
    
    document.getElementById('systemText').textContent = status;
    document.getElementById('securityStatus').textContent = status;
    document.getElementById('lastCommand').textContent = `System ${status}`;
    
    // Update status indicator
    const systemIndicator = document.getElementById('systemIndicator');
    if (status === 'ARMED') {
        systemIndicator.classList.remove('status-disarmed');
        systemIndicator.classList.add('status-armed');
        
        // Add alert to the list
        addAlert('System Armed', 'Security system has been armed', 'warning');
    } else if (status === 'DISARMED') {
        systemIndicator.classList.remove('status-armed');
        systemIndicator.classList.add('status-disarmed');
        
        // Add alert to the list
        addAlert('System Disarmed', 'Security system has been disarmed', 'success');
    }
}

// Handle device status update
function handleDeviceStatus(data) {
    console.log('Device status:', data);
    
    if (data.deviceId === 'esp32') {
        deviceConnected = data.status === 'connected';
        
        // Update status display
        document.getElementById('esp32-status').textContent = deviceConnected ? 'Online' : 'Offline';
        document.getElementById('esp32ConnectionStatus').textContent = deviceConnected ? 'Connected' : 'Disconnected';
        document.getElementById('esp32StatusBadge').textContent = deviceConnected ? 'Online' : 'Offline';
        
        const statusBadge = document.getElementById('esp32StatusBadge');
        statusBadge.className = deviceConnected ? 
            'px-2 py-1 bg-green-600 text-white rounded text-xs' : 
            'px-2 py-1 bg-red-600 text-white rounded text-xs';
        
        // Update SIM number if available
        if (data.sim) {
            document.getElementById('simNumberDisplay').textContent = data.sim;
        }
        
        // Add alert to the list
        if (deviceConnected) {
            addAlert('ESP32 Connected', 'Device is now online and transmitting data', 'success');
        } else {
            addAlert('ESP32 Disconnected', 'Lost connection to the device', 'error');
        }
    }
}

// Handle theft alert
function handleTheftAlert(data) {
    console.log('Theft alert:', data);
    
    // Add alert to the list
    addAlert('THEFT DETECTED', data.message || 'Vehicle security breach detected!', 'critical');
    
    // Visual and audio alerts can be added here
    document.body.classList.add('theft-alert');
    setTimeout(() => {
        document.body.classList.remove('theft-alert');
    }, 5000);
}

// Handle config update
function handleConfigUpdate(data) {
    console.log('Config updated:', data);
    
    if (data.ownerNumber) {
        document.getElementById('ownerPhone').value = data.ownerNumber;
        addAlert('Settings Updated', 'Phone number has been updated', 'info');
    }
}

// Update the connection status display
function updateConnectionStatus(status) {
    document.getElementById('connectionText').textContent = status;
    
    const indicator = document.getElementById('connectionIndicator');
    if (status === 'Connected') {
        indicator.classList.remove('status-disconnected');
        indicator.classList.add('status-connected');
    } else {
        indicator.classList.remove('status-connected');
        indicator.classList.add('status-disconnected');
    }
}

// Add an alert to the alerts list
function addAlert(title, message, type) {
    const alertsList = document.getElementById('alertsList');
    
    // Create the alert element
    const alertDiv = document.createElement('div');
    alertDiv.className = 'flex items-center p-2 bg-dark-gray rounded-lg';
    
    // Determine color based on alert type
    let dotColor;
    switch (type) {
        case 'critical':
            dotColor = 'bg-red-500';
            break;
        case 'warning':
            dotColor = 'bg-yellow-500';
            break;
        case 'success':
            dotColor = 'bg-green-500';
            break;
        case 'info':
            dotColor = 'bg-blue-500';
            break;
        case 'error':
            dotColor = 'bg-red-500';
            break;
        default:
            dotColor = 'bg-gray-500';
    }
    
    // Create alert content
    alertDiv.innerHTML = `
        <div class="w-2 h-2 ${dotColor} rounded-full mr-3"></div>
        <div>
            <p class="text-sm font-medium">${title}</p>
            <p class="text-xs text-gray-400">${message}</p>
            <p class="text-xs text-gray-500 mt-1">${new Date().toLocaleTimeString()}</p>
        </div>
    `;
    
    // Add to the top of the list
    alertsList.insertBefore(alertDiv, alertsList.firstChild);
    
    // Limit number of alerts
    if (alertsList.children.length > 5) {
        alertsList.removeChild(alertsList.lastChild);
    }
}

// Calculate distance between two points using Haversine formula
function calculateDistance(lat1, lon1, lat2, lon2) {
    const R = 6371000; // Earth radius in meters
    const dLat = (lat2 - lat1) * Math.PI / 180;
    const dLon = (lon2 - lon1) * Math.PI / 180;
    const a = 
        Math.sin(dLat / 2) * Math.sin(dLat / 2) +
        Math.cos(lat1 * Math.PI / 180) * Math.cos(lat2 * Math.PI / 180) * 
        Math.sin(dLon / 2) * Math.sin(dLon / 2);
    const c = 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));
    return R * c; // Distance in meters
}

// Calculate total trip distance from historical points
function calculateTripDistance(points) {
    totalDistance = 0;
    
    for (let i = 1; i < points.length; i++) {
        totalDistance += calculateDistance(
            points[i-1].lat, points[i-1].lng,
            points[i].lat, points[i].lng
        );
    }
    
    document.getElementById('total-distance').textContent = Math.round(totalDistance);
}

// Update trip duration
function updateTripDuration() {
    if (!tripStartTime) return;
    
    const now = new Date();
    const diff = now - tripStartTime;
    
    // Convert to minutes and seconds
    const minutes = Math.floor(diff / 60000);
    const seconds = Math.floor((diff % 60000) / 1000);
    
    document.getElementById('trip-duration').textContent = 
        `${minutes.toString().padStart(2, '0')}:${seconds.toString().padStart(2, '0')}`;
    
    // Update every second
    setTimeout(updateTripDuration, 1000);
}

// Update current time
function updateCurrentTime() {
    document.getElementById('current-time').textContent = new Date().toLocaleTimeString();
    setTimeout(updateCurrentTime, 1000);
}

// Set up all event listeners
function setupEventListeners() {
    // Arm system button
    document.getElementById('armBtn').addEventListener('click', armSystem);
    document.getElementById('quickArmBtn').addEventListener('click', armSystem);
    
    // Disarm system button
    document.getElementById('disarmBtn').addEventListener('click', disarmSystem);
    document.getElementById('quickDisarmBtn').addEventListener('click', disarmSystem);
    
    // Get status button
    document.getElementById('statusBtn').addEventListener('click', getStatus);
    document.getElementById('getStatusBtn').addEventListener('click', getStatus);
    
    // Simulate theft button
    document.getElementById('simulateTheftBtn').addEventListener('click', simulateTheft);
    
    
}

// WebSocket command functions
function armSystem() {
    if (!isConnected) {
        addAlert('Connection Error', 'Cannot arm system while disconnected', 'error');
        return;
    }
    
    console.log('Arming system');
    socket.send(JSON.stringify({
        type: 'arm_system'
    }));
    
    document.getElementById('lastCommand').textContent = 'Arm System';
}

function disarmSystem() {
    if (!isConnected) {
        addAlert('Connection Error', 'Cannot disarm system while disconnected', 'error');
        return;
    }
    
    console.log('Disarming system');
    socket.send(JSON.stringify({
        type: 'disarm_system'
    }));
    
    document.getElementById('lastCommand').textContent = 'Disarm System';
}

function getStatus() {
    if (!isConnected) {
        addAlert('Connection Error', 'Cannot get status while disconnected', 'error');
        return;
    }
    
    console.log('Getting status');
    socket.send(JSON.stringify({
        type: 'get_status'
    }));
    
    document.getElementById('lastCommand').textContent = 'Get Status';
}

function simulateTheft() {
    if (!isConnected) {
        addAlert('Connection Error', 'Cannot simulate theft while disconnected', 'error');
        return;
    }
    
    if (systemStatus !== 'ARMED') {
        addAlert('System Disarmed', 'Cannot simulate theft while system is disarmed', 'warning');
        return;
    }
    
    console.log('Simulating theft');
    socket.send(JSON.stringify({
        type: 'simulate_theft'
    }));
    
    document.getElementById('lastCommand').textContent = 'Simulate Theft';
}

function updatePhone() {
    if (!isConnected) {
        addAlert('Connection Error', 'Cannot update phone while disconnected', 'error');
        return;
    }
    
    const phoneInput = document.getElementById('ownerPhone');
    const newPhone = phoneInput.value.trim();
    
    // Basic validation
    if (!newPhone || !newPhone.startsWith('+')) {
        addAlert('Invalid Format', 'Phone number must start with + and country code', 'error');
        return;
    }
    
    console.log('Updating phone number:', newPhone);
    socket.send(JSON.stringify({
        type: 'update_phone',
        ownerNumber: newPhone
    }));
    
    document.getElementById('lastCommand').textContent = 'Update Phone';
}

// Initialize everything when the page loads
document.addEventListener('DOMContentLoaded', function() {
    // Initialize the map
    initMap();
    
    // Connect to WebSocket
    connectWebSocket();
    
    // Set up event listeners
    setupEventListeners();
    
    // Update current time
    updateCurrentTime();
    
    console.log('Dashboard initialized');
});