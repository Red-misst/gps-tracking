import express from 'express';
import { WebSocketServer } from 'ws';
import path from 'path';
import { fileURLToPath } from 'url';
import fs from 'fs';
import http from 'http';

const app = express();
const __dirname = path.dirname(fileURLToPath(import.meta.url));
app.use(express.json());

// Serve static files
app.use(express.static(path.join(__dirname, 'public')));
app.use('/gps-locator', express.static(path.join(__dirname, 'gps-locator')));

// Add middleware to log all requests for debugging
app.use((req, res, next) => {
  console.log(`${new Date().toISOString()} - ${req.method} ${req.url}`);
  if (req.method === 'POST' && req.body && Object.keys(req.body).length > 0) {
    console.log('Body:', JSON.stringify(req.body, null, 2));
  }
  next();
});

// Store GPS history for tracking
const gpsHistory = [];
const MAX_HISTORY = 100;

// In-memory config
let config = {
  ownerNumber: '0714874451',
  simNumber: '0114931050',
  simPin: '6577'
};

// Store connected clients
const dashboardClients = new Set();
let arduinoClient = null;

// Express API for getting/updating config
app.get('/api/config', (req, res) => {
  const formattedConfig = {
    ownerNumber: config.ownerNumber,
    simNumber: config.simNumber.startsWith('+') ? config.simNumber : `+254${config.simNumber.startsWith('0') ? config.simNumber.substring(1) : config.simNumber}`,
    simPin: config.simPin
  };
  res.json(formattedConfig);
});

app.post('/api/config', (req, res) => {
  const { ownerNumber, simNumber, simPin } = req.body;
  if (ownerNumber) config.ownerNumber = ownerNumber;
  if (simNumber) config.simNumber = simNumber;
  if (simPin) config.simPin = simPin;
  
  broadcast({ type: 'config', payload: config });
  res.json(config);
});

// Simulate limit switch trigger
let remoteTheftTrigger = false;
app.post('/api/simulate-trigger', (req, res) => {
  remoteTheftTrigger = true;
  broadcast({ type: 'limitTrigger' });
  res.json({ triggered: true });
});

app.get('/api/check-trigger', (req, res) => {
  res.json({ triggered: remoteTheftTrigger });
  remoteTheftTrigger = false;
});

// Get GPS history
app.get('/api/gps-history', (req, res) => {
  res.json(gpsHistory);
});

// API to receive GPS data from Arduino via HTTP POST
app.post('/gps', express.json({ strict: false }), (req, res) => {
  try {
    const { lat, lng, sim } = req.body;
    
    const validData = lat && lng && !isNaN(lat) && !isNaN(lng);
    
    if (!validData) {
      return res.status(400).json({ error: 'Invalid GPS data' });
    }
    
    const gpsData = {
      lat: parseFloat(lat),
      lng: parseFloat(lng),
      sim: sim || 'unknown',
      timestamp: new Date().toISOString()
    };
    
    // Store in memory (limit to last 100 entries)
    gpsHistory.push(gpsData);
    if (gpsHistory.length > 100) {
      gpsHistory.shift();
    }
    
    // Broadcast to all WebSocket clients
    broadcast({
      type: 'gps',
      payload: gpsData
    });
    
    res.status(200).json({ status: 'success', received: gpsData });
  } catch (err) {
    console.error('Error processing GPS data:', err);
    res.status(500).json({ error: 'Server error', details: err.message });
  }
});

// Create HTTP server
const server = http.createServer(app);

// Create WebSocket server on the same port as HTTP
const wss = new WebSocketServer({ server });

// Store connected clients (both web dashboards and ESP32 devices)
const clients = new Map();

wss.on('connection', (ws, req) => {
  const clientId = Date.now() + Math.random();
  const clientInfo = {
    id: clientId,
    ws: ws,
    type: 'unknown', // Will be set when client identifies itself
    ip: req.socket.remoteAddress,
    connected: new Date()
  };
  
  clients.set(clientId, clientInfo);
  console.log(`WebSocket client connected: ${clientId} from ${clientInfo.ip}`);
  
  // Handle incoming messages
  ws.on('message', (data) => {
    try {
      const message = JSON.parse(data.toString());
      handleWebSocketMessage(clientId, message);
    } catch (err) {
      console.error('Invalid WebSocket message:', err);
      // Try to handle as raw ESP32 message
      handleESP32RawMessage(clientId, data.toString());
    }
  });
  
  // Handle client disconnection
  ws.on('close', () => {
    const client = clients.get(clientId);
    if (client) {
      console.log(`WebSocket client disconnected: ${clientId} (${client.type})`);
      clients.delete(clientId);
    }
  });
  
  // Send welcome message
  ws.send(JSON.stringify({
    type: 'welcome',
    clientId: clientId,
    timestamp: new Date().toISOString()
  }));
});

// Handle WebSocket messages from clients
function handleWebSocketMessage(clientId, message) {
  const client = clients.get(clientId);
  if (!client) return;
  
  console.log(`Message from ${clientId}:`, message);
  
  switch (message.type) {
    case 'identify':
      // Client identifies itself as web dashboard or ESP32
      client.type = message.clientType || 'web';
      client.deviceInfo = message.deviceInfo || {};
      console.log(`Client ${clientId} identified as: ${client.type}`);
      
      // If it's a web client, send current GPS history
      if (client.type === 'web') {
        client.ws.send(JSON.stringify({
          type: 'gps_history',
          payload: gpsHistory
        }));
      }
      break;
      
    case 'gps_data':
      // GPS data from ESP32
      if (client.type === 'esp32' || client.type === 'unknown') {
        client.type = 'esp32'; // Auto-identify ESP32 devices
        handleGPSFromESP32(message.payload);
      }
      break;
      
    case 'theft_alert':
      // Theft alert from ESP32
      if (client.type === 'esp32' || client.type === 'unknown') {
        client.type = 'esp32';
        handleTheftAlert(message.payload);
      }
      break;
      
    case 'arm_system':
      // Command from web dashboard to arm system
      if (client.type === 'web') {
        sendCommandToESP32({ type: 'arm_system' });
      }
      break;
      
    case 'disarm_system':
      // Command from web dashboard to disarm system
      if (client.type === 'web') {
        sendCommandToESP32({ type: 'disarm_system' });
      }
      break;
      
    case 'get_status':
      // Request status from ESP32
      if (client.type === 'web') {
        sendCommandToESP32({ type: 'get_status' });
      }
      break;
  }
}

// Handle raw messages from ESP32 (for backward compatibility)
function handleESP32RawMessage(clientId, data) {
  const client = clients.get(clientId);
  if (!client) return;
  
  // Auto-identify as ESP32 if receiving raw data
  if (client.type === 'unknown') {
    client.type = 'esp32';
    console.log(`Client ${clientId} auto-identified as ESP32 (raw data)`);
  }
  
  // Try to parse GPS coordinates from raw data
  const gpsMatch = data.match(/GPS:(-?\d+\.\d+),(-?\d+\.\d+)/);
  if (gpsMatch) {
    const gpsData = {
      lat: parseFloat(gpsMatch[1]),
      lng: parseFloat(gpsMatch[2]),
      sim: 'websocket',
      timestamp: new Date().toISOString()
    };
    handleGPSFromESP32(gpsData);
  }
  
  // Handle theft alerts
  if (data.includes('THEFT_ALERT')) {
    handleTheftAlert({ message: data, timestamp: new Date().toISOString() });
  }
}

// Handle GPS data from ESP32
function handleGPSFromESP32(gpsData) {
  // Store in history
  gpsHistory.push(gpsData);
  if (gpsHistory.length > 100) {
    gpsHistory.shift();
  }
  
  // Broadcast to web clients
  broadcastToWebClients({
    type: 'gps',
    payload: gpsData
  });
  
  console.log('GPS data received from ESP32:', gpsData);
}

// Handle theft alerts from ESP32
function handleTheftAlert(alertData) {
  // Broadcast theft alert to all web clients
  broadcastToWebClients({
    type: 'theft_alert',
    payload: alertData
  });
  
  console.log('Theft alert received from ESP32:', alertData);
}

// Send command to all connected ESP32 devices
function sendCommandToESP32(command) {
  clients.forEach((client) => {
    if (client.type === 'esp32' && client.ws.readyState === WebSocket.OPEN) {
      client.ws.send(JSON.stringify(command));
    }
  });
}

// Broadcast message to all web dashboard clients
function broadcastToWebClients(message) {
  clients.forEach((client) => {
    if (client.type === 'web' && client.ws.readyState === WebSocket.OPEN) {
      client.ws.send(JSON.stringify(message));
    }
  });
}

// Broadcast to all connected clients
function broadcast(message) {
  clients.forEach((client) => {
    if (client.ws.readyState === WebSocket.OPEN) {
      client.ws.send(JSON.stringify(message));
    }
  });
}

// API endpoint to get connected devices status
app.get('/api/devices/status', (req, res) => {
  const deviceStatus = {
    total: clients.size,
    esp32_devices: Array.from(clients.values()).filter(c => c.type === 'esp32').length,
    web_clients: Array.from(clients.values()).filter(c => c.type === 'web').length,
    devices: Array.from(clients.values()).map(c => ({
      id: c.id,
      type: c.type,
      connected: c.connected,
      ip: c.ip
    }))
  };
  
  res.json(deviceStatus);
});

// API endpoint to send commands to ESP32
app.post('/api/esp32/command', express.json(), (req, res) => {
  const { command, payload } = req.body;
  
  if (!command) {
    return res.status(400).json({ error: 'Command is required' });
  }
  
  const message = {
    type: command,
    payload: payload || {},
    timestamp: new Date().toISOString()
  };
  
  sendCommandToESP32(message);
  
  res.json({ 
    status: 'success', 
    message: 'Command sent to ESP32 devices',
    command: message
  });
});

// Start server on port 3000
const PORT = process.env.PORT || 3000;
server.listen(PORT, '0.0.0.0', () => {
  console.log(`Server running on port ${PORT}`);
  console.log(`HTTP server: http://localhost:${PORT}`);
  console.log(`WebSocket server: ws://localhost:${PORT}`);
  console.log(`Live server: https://gps-tracking-1rnf.onrender.com`);
});
