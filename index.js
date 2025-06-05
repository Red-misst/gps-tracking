import express from 'express';
import { createServer } from 'http';
import { WebSocketServer } from 'ws';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';

// Get the directory name from the file URL
const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

// Initialize express app
const app = express();
const port = process.env.PORT || 3000;

// Create HTTP server
const server = createServer(app);

// Create WebSocket server
const wss = new WebSocketServer({ server });

// Store connected clients
const clients = new Set();
// Store device connections
const deviceConnections = new Map();
// Store GPS history (most recent entries)
const gpsHistory = [];
const MAX_HISTORY_LENGTH = 100;

// System configuration
const systemConfig = {
  ownerNumber: "+254714874451",
  simNumber: "+254743600744",
  simPin: "2588",
  isArmed: false,
  isTheftDetected: false
};

// Serve static files from the 'public' directory
app.use(express.static(join(__dirname, 'public')));
app.use(express.json());

// Routes
app.get('/api/status', (req, res) => {
  res.json({
    status: 'online',
    connections: {
      total: clients.size,
      esp32: deviceConnections.has('esp32')
    },
    system: {
      armed: systemConfig.isArmed,
      theftDetected: systemConfig.isTheftDetected
    },
    config: {
      ownerNumber: systemConfig.ownerNumber,
      simNumber: systemConfig.simNumber
    }
  });
});

// Handle WebSocket connections
wss.on('connection', (ws) => {
  console.log('Client connected');
  clients.add(ws);
  
  // Send initial data to the new client
  ws.send(JSON.stringify({
    type: 'init',
    payload: {
      systemStatus: systemConfig.isArmed ? 'ARMED' : 'DISARMED',
      gpsHistory: gpsHistory,
      config: {
        ownerNumber: systemConfig.ownerNumber,
        simNumber: systemConfig.simNumber
      }
    }
  }));

  // Handle incoming messages
  ws.on('message', (message) => {
    try {
      const data = JSON.parse(message);
      
      // Log the message type
      console.log(`Received message type: ${data.type}`);

      // Handle different message types
      switch (data.type) {
        case 'identify':
          handleIdentification(ws, data);
          break;
          
        case 'gps_data':
          handleGpsData(data);
          break;
          
        case 'arm_system':
          handleArmSystem();
          break;
          
        case 'disarm_system':
          handleDisarmSystem();
          break;
          
        case 'simulate_theft':
          handleSimulateTheft();
          break;
          
        case 'update_phone':
          handleUpdatePhone(data);
          break;
          
        case 'get_status':
          handleGetStatus();
          break;
          
        case 'heartbeat':
          // Just log heartbeats
          console.log('Heartbeat received from device');
          break;
          
        case 'theft_alert':
          handleTheftAlert(data);
          break;
          
        case 'system_status':
          handleSystemStatus(data);
          break;
          
        default:
          console.log(`Unknown message type: ${data.type}`);
      }
    } catch (error) {
      console.error('Error processing message:', error);
    }
  });

  // Handle disconnections
  ws.on('close', () => {
    console.log('Client disconnected');
    clients.delete(ws);
    
    // Remove from device connections if present
    for (const [deviceId, connection] of deviceConnections.entries()) {
      if (connection === ws) {
        console.log(`Device ${deviceId} disconnected`);
        deviceConnections.delete(deviceId);
        
        // Notify all clients about the device disconnection
        broadcastMessage({
          type: 'device_status',
          payload: {
            deviceId,
            status: 'disconnected'
          }
        });
        
        break;
      }
    }
  });
});

// Message handlers
function handleIdentification(ws, data) {
  const { clientType, sim } = data;
  
  if (clientType === 'esp32') {
    console.log('ESP32 device identified with SIM:', sim);
    deviceConnections.set('esp32', ws);
    systemConfig.simNumber = sim;
    
    // Notify all clients about the device connection
    broadcastMessage({
      type: 'device_status',
      payload: {
        deviceId: 'esp32',
        status: 'connected',
        sim
      }
    });
  } else {
    console.log(`Client identified as: ${clientType}`);
  }
}

function handleGpsData(data) {
  const payload = data.payload || data;
  
  // Add timestamp if not present
  if (!payload.timestamp) {
    payload.timestamp = new Date().toISOString();
  }
  
  console.log(`GPS data received: ${payload.lat}, ${payload.lng}`);
  
  // Add to history
  gpsHistory.push(payload);
  
  // Trim history if needed
  if (gpsHistory.length > MAX_HISTORY_LENGTH) {
    gpsHistory.shift();
  }
  
  // Broadcast to all clients
  broadcastMessage({
    type: 'gps',
    payload
  });
}

function handleArmSystem() {
  console.log('Arming system');
  systemConfig.isArmed = true;
  systemConfig.isTheftDetected = false;
  
  // Broadcast to all clients
  broadcastMessage({
    type: 'system_status',
    payload: 'ARMED'
  });
  
  // Send command to ESP32 if connected
  const esp32Client = deviceConnections.get('esp32');
  if (esp32Client) {
    esp32Client.send(JSON.stringify({
      type: 'arm_system'
    }));
  }
}

function handleDisarmSystem() {
  console.log('Disarming system');
  systemConfig.isArmed = false;
  systemConfig.isTheftDetected = false;
  
  // Broadcast to all clients
  broadcastMessage({
    type: 'system_status',
    payload: 'DISARMED'
  });
  
  // Send command to ESP32 if connected
  const esp32Client = deviceConnections.get('esp32');
  if (esp32Client) {
    esp32Client.send(JSON.stringify({
      type: 'disarm_system'
    }));
  }
}

function handleSimulateTheft() {
  console.log('Simulating theft');
  systemConfig.isTheftDetected = true;
  
  // Broadcast to all clients
  broadcastMessage({
    type: 'theft_alert',
    payload: {
      message: 'Theft simulation triggered'
    }
  });
  
  // Send command to ESP32 if connected
  const esp32Client = deviceConnections.get('esp32');
  if (esp32Client) {
    esp32Client.send(JSON.stringify({
      type: 'simulate_theft'
    }));
  }
}

function handleUpdatePhone(data) {
  const { ownerNumber } = data;
  
  if (ownerNumber) {
    console.log(`Updating owner number to: ${ownerNumber}`);
    systemConfig.ownerNumber = ownerNumber;
    
    // Broadcast to all clients
    broadcastMessage({
      type: 'config_update',
      payload: {
        ownerNumber
      }
    });
    
    // Send command to ESP32 if connected
    const esp32Client = deviceConnections.get('esp32');
    if (esp32Client) {
      esp32Client.send(JSON.stringify({
        type: 'update_phone',
        ownerNumber
      }));
    }
  }
}

function handleGetStatus() {
  console.log('Status requested');
  
  // Broadcast current status
  broadcastMessage({
    type: 'system_status',
    payload: systemConfig.isArmed ? 'ARMED' : 'DISARMED'
  });
  
  // Send command to ESP32 if connected
  const esp32Client = deviceConnections.get('esp32');
  if (esp32Client) {
    esp32Client.send(JSON.stringify({
      type: 'get_status'
    }));
  }
}

function handleTheftAlert(data) {
  console.log('Theft alert received:', data.payload);
  systemConfig.isTheftDetected = true;
  
  // Broadcast to all clients
  broadcastMessage({
    type: 'theft_alert',
    payload: data.payload
  });
}

function handleSystemStatus(data) {
  console.log('System status update:', data.payload);
  
  // Update system status
  if (data.payload === 'ARMED') {
    systemConfig.isArmed = true;
  } else if (data.payload === 'DISARMED') {
    systemConfig.isArmed = false;
    systemConfig.isTheftDetected = false;
  }
  
  // Broadcast to all clients
  broadcastMessage({
    type: 'system_status',
    payload: data.payload
  });
}

// Helper to broadcast message to all connected clients
function broadcastMessage(message) {
  const messageStr = JSON.stringify(message);
  
  for (const client of clients) {
    if (client.readyState === client.OPEN) {
      client.send(messageStr);
    }
  }
}

// Start server
server.listen(port, () => {
  console.log(`Server running on port ${port}`);
  console.log(`Access the dashboard at http://localhost:${port}`);
});
