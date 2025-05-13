import express from 'express';
import { WebSocketServer } from 'ws';
import path from 'path';
import { fileURLToPath } from 'url';
import fs from 'fs';
import http from 'http';

const app = express();
const __dirname = path.dirname(fileURLToPath(import.meta.url));
app.use(express.json());

// Store GPS history for tracking
const gpsHistory = [];
const MAX_HISTORY = 100;

// In-memory config
let config = {
  ownerNumber: '0714874451',
  simNumber: '0114931050',
  simPin: '6577'
};

// Express API for getting/updating config
app.get('/api/config', (req, res) => {
  res.json(config);
});

app.post('/api/config', (req, res) => {
  const { ownerNumber, simNumber, simPin } = req.body;
  if (ownerNumber) config.ownerNumber = ownerNumber;
  if (simNumber) config.simNumber = simNumber;
  if (simPin) config.simPin = simPin;
  
  // Push update to board via WS
  broadcast({ type: 'config', payload: config });
  res.json(config);
});

// Simulate limit switch trigger - update to set a flag for ESP32 to check
let remoteTheftTrigger = false;
app.post('/api/simulate-trigger', (req, res) => {
  // Set flag for ESP32 to retrieve
  remoteTheftTrigger = true;
  
  // Notify dashboard clients
  broadcast({ type: 'limitTrigger' });
  
  res.json({ triggered: true });
});

// API endpoint for ESP32 to check for theft trigger
app.get('/api/check-trigger', (req, res) => {
  // Respond with current trigger status - initially not triggered
  res.json({ triggered: remoteTheftTrigger });
  
  // Reset the trigger after it's been checked
  remoteTheftTrigger = false;
});

// Get GPS history
app.get('/api/gps-history', (req, res) => {
  res.json(gpsHistory);
});

// API to receive GPS data from ESP32 via HTTP POST
app.post('/gps', express.json({ strict: false }), (req, res) => {
  try {
    console.log('Received HTTP POST data:', JSON.stringify(req.body));
    
    // Log raw request for debugging
    console.log('Headers:', req.headers);
    
    let data = req.body;
    let validData = false;
    
    // Check for valid format with more flexibility
    if (typeof data === 'object') {
      if (data.type === 'gps' && data.payload) {
        // Standard format from our code
        validData = true;
        
        // Add timestamp to GPS data
        const gpsData = { 
          ...data.payload, 
          timestamp: new Date().toISOString() 
        };
        
        // Store in history
        gpsHistory.push(gpsData);
        if (gpsHistory.length > MAX_HISTORY) {
          gpsHistory.shift(); // Remove oldest item
        }
        
        // Broadcast GPS to dashboard clients via WebSocket
        broadcast({ type: 'gps', payload: gpsData });
        
        // Send success response
        res.status(200).json({ success: true });
      } else if (data.lat !== undefined && data.lng !== undefined) {
        // Direct lat/lng format (simpler format from ESP32)
        validData = true;
        
        // Convert to standard format
        const gpsData = { 
          sim: data.sim || simNumber,
          lat: data.lat,
          lng: data.lng,
          timestamp: new Date().toISOString() 
        };
        
        // Store in history
        gpsHistory.push(gpsData);
        if (gpsHistory.length > MAX_HISTORY) {
          gpsHistory.shift(); // Remove oldest item
        }
        
        // Broadcast GPS to dashboard clients via WebSocket
        broadcast({ type: 'gps', payload: gpsData });
        
        // Send success response
        res.status(200).json({ success: true });
      }
    }
    
    if (!validData) {
      console.warn('Invalid GPS data format:', data);
      res.status(400).json({ error: 'Invalid data format' });
    }
  } catch (err) {
    console.error('Error processing GPS data:', err);
    res.status(500).json({ error: 'Server error', details: err.message });
  }
});

// Serve dashboard
app.use(express.static(path.join(__dirname, 'public')));

// Serve GPS locator page
app.get('/gps-locator', (req, res) => {
  res.sendFile(path.join(__dirname, 'gps-locator', 'index.html'));
});

// Start HTTP server on port 3000 and 80 (for the ESP32)
const server = http.createServer(app);
server.listen(3000, () => {
  console.log('HTTP server listening on port 3000');
});

// Also listen on port 80 for the ESP32 which is using unsecure WebSockets
const server80 = http.createServer(app);
server80.listen(80, () => {
  console.log('HTTP server also listening on port 80 for ESP32');
});

// WebSocket servers on both HTTP servers
const wss = new WebSocketServer({ server, path: '/gps' });
const wss80 = new WebSocketServer({ server: server80, path: '/gps' });
const clients = new Set();

// Function to setup WebSocket server
function setupWebSocketServer(wss) {
  wss.on('connection', (ws, req) => {
    console.log('WS connection from', req.socket.remoteAddress, 'on port', req.socket.localPort);
    clients.add(ws);

    // Send current config on connect
    try {
      ws.send(JSON.stringify({ type: 'config', payload: config }));
      console.log('Sent initial config to new client');
      
      // Send latest GPS data if available
      if (gpsHistory.length > 0) {
        ws.send(JSON.stringify({ 
          type: 'gps', 
          payload: gpsHistory[gpsHistory.length - 1] 
        }));
        console.log('Sent last GPS position to new client');
      }
    } catch (err) {
      console.error('Error sending initial data to client:', err);
    }

    ws.on('message', data => {
      try {
        // Log the raw data for debugging
        console.log('Received message:', data.toString());
        
        // Try to parse JSON
        const msg = JSON.parse(data.toString());
        
        if (msg.type === 'gps' && msg.payload) {
          console.log('Received GPS data:', JSON.stringify(msg.payload));
          
          // Add timestamp to GPS data
          const gpsData = { 
            ...msg.payload, 
            timestamp: new Date().toISOString() 
          };
          
          // Store in history
          gpsHistory.push(gpsData);
          if (gpsHistory.length > MAX_HISTORY) {
            gpsHistory.shift(); // Remove oldest item
          }
          
          // Broadcast GPS to dashboard clients
          broadcast({ type: 'gps', payload: gpsData });
        }
      } catch (err) {
        console.warn('Invalid message format:', data.toString(), err);
      }
    });

    ws.on('error', (error) => {
      console.error('WebSocket error:', error);
    });

    ws.on('close', () => {
      console.log('Client disconnected');
      clients.delete(ws);
    });
  });
}

// Set up both WebSocket servers
setupWebSocketServer(wss);
setupWebSocketServer(wss80);

// Helper to broadcast to all WS clients
function broadcast(obj) {
  const str = JSON.stringify(obj);
  let sentCount = 0;
  
  for (const ws of clients) {
    try {
      if (ws.readyState === ws.OPEN) {
        ws.send(str);
        sentCount++;
      }
    } catch (err) {
      console.error('Error broadcasting to client:', err);
    }
  }
  
  console.log(`Broadcast message to ${sentCount} clients`);
}
