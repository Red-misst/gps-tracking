#define TINY_GSM_MODEM_SIM7600
#define TINY_GSM_RX_BUFFER 1024

#include <TinyGsmClient.h>
#include <ArduinoWebsockets.h>
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>

using namespace websockets;

// ==== SIM and Owner Info ====
String simPin = "6577";
String simNumber = "+254114931050";
String ownerNumber = "+254714874451";

// ==== Hardware Pins ====
#define MODEM_RX 26
#define MODEM_TX 27
#define MODEM_PWRKEY 4
#define MODEM_POWER_ON 23
#define MODEM_RST 5
#define LIMIT_SWITCH_PIN 34

// ==== Serial and Modules ====
HardwareSerial SerialAT(1);
TinyGsm modem(SerialAT);
TinyGPSPlus gps;
WebsocketsClient webSocket;
TinyGsmClient gsmClient(modem);  // GSM client for cellular connection

// ==== WebSocket Server Info ====
// For HTTPS server URL, use secure WebSocket with proper port
const char* ws_server = "kp36cdgx-3000.uks1.devtunnels.ms";  
const uint16_t ws_port = 443;  // Changed back to 443 for wss since we're using HTTPS
const char* ws_path = "/gps";
const bool useSSL = true;  // Enable SSL for secure WebSocket connection

// ==== State ====
float lastLat = 0.0;
float lastLng = 0.0;
unsigned long lastGPSTime = 0;
const unsigned long gpsInterval = 10000;
bool limitTriggered = false;
unsigned long lastReconnectAttempt = 0;
const unsigned long reconnectInterval = 30000;  // Try to reconnect every 30 seconds

// ========== SETUP ==========
void setup() {
  Serial.begin(115200);
  delay(10);

  pinMode(LIMIT_SWITCH_PIN, INPUT_PULLUP);
  pinMode(MODEM_PWRKEY, OUTPUT);
  pinMode(MODEM_POWER_ON, OUTPUT);
  pinMode(MODEM_RST, OUTPUT);

  powerOnModem();

  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(3000);

  Serial.println("Initializing modem...");
  modem.restart();
  modem.simUnlock(simPin.c_str());

  // Network connection
  Serial.println("Connecting to cellular network...");
  if (!modem.waitForNetwork(60000L)) {
    Serial.println("Network connection failed");
    while (true);
  }

  if (!modem.gprsConnect("safaricom", "", "")) {
    Serial.println("GPRS connection failed");
    while (true);
  }

  Serial.println("Network connected");

  // Enable GPS
  modem.enableGPS();

  // Setup WebSocket
  webSocket.onMessage(handleWebSocketMessage);
  webSocket.onEvent(handleWebSocketEvent);
  connectWebSocket();
}

// ========== MAIN LOOP ==========
void loop() {
  // Check if we need to reconnect the WebSocket
  if (!webSocket.available()) {
    // Try to reconnect if not connected
    connectWebSocket();
  } else {
    // Only poll if connected
    webSocket.poll();
  }
  
  // Get and send GPS data
  getGPSData();

  // Check limit switch for theft detection
  bool currentLimit = digitalRead(LIMIT_SWITCH_PIN) == LOW;
  if (currentLimit && !limitTriggered) {
    limitTriggered = true;
    sendTheftAlert();
  } else if (!currentLimit) {
    limitTriggered = false;
  }
}

// ========== POWER ON MODEM ==========
void powerOnModem() {
  digitalWrite(MODEM_POWER_ON, HIGH);
  digitalWrite(MODEM_RST, HIGH);
  digitalWrite(MODEM_PWRKEY, HIGH);
  delay(1000);
  digitalWrite(MODEM_PWRKEY, LOW);
  delay(1000);
}

// ========== CONNECT WEBSOCKET ==========
void connectWebSocket() {
  // Only attempt reconnection if enough time has passed since last attempt
  if (millis() - lastReconnectAttempt < reconnectInterval) {
    return;
  }
  
  lastReconnectAttempt = millis();
  
  // Use wss:// protocol for secure WebSocket connection
  String ws_url = "wss://" + String(ws_server) + ":" + String(ws_port) + String(ws_path);
  
  Serial.println("Connecting to WebSocket: " + ws_url);
  
  // Clear any existing connections
  webSocket.close();
  delay(1000);
  

  
  // Try to connect
  bool connected = webSocket.connect(ws_url);
  
  if (connected) {
    Serial.println("WebSocket connection established");
  } else {
    Serial.println("WebSocket connection failed");
  }
}

// ========== GET GPS DATA ==========
void getGPSData() {
  if (millis() - lastGPSTime >= gpsInterval) {
    lastGPSTime = millis(); // Always update the timer first
    
    float lat, lng;
    if (modem.getGPS(&lat, &lng)) {
      lastLat = lat;
      lastLng = lng;
      
      // Only attempt to send data if WebSocket is connected
      if (webSocket.available()) {
        // Format with type field for proper handling by server
        String json = "{\"type\":\"gps\",\"payload\":{\"sim\":\"" + simNumber + "\",\"lat\":" + String(lat, 6) + ",\"lng\":" + String(lng, 6) + "}}";
        
        // Try to send data
        bool sent = webSocket.send(json);
        if (sent) {
          Serial.println("GPS Sent: " + json);
        } else {
          Serial.println("Failed to send GPS data: WebSocket send failed");
        }
      } else {
        Serial.println("GPS data available but WebSocket not connected");
      }
    } else {
      Serial.println("Failed to get GPS data from modem");
    }
  }
}

// ========== THEFT ALERT ==========
void sendTheftAlert() {
  String msg = "Theft Alert!\nGPS: https://maps.google.com/?q=" + String(lastLat, 6) + "," + String(lastLng, 6);

  // Send SMS
  modem.sendSMS(ownerNumber.c_str(), msg.c_str());
  Serial.println("SMS sent to " + ownerNumber);

  // Make call
  modem.callNumber(ownerNumber.c_str());
  delay(15000);  // Wait for 15 seconds
  modem.sendAT("+CHUP");  // Hang up
  modem.waitResponse();
  Serial.println("Call ended");
}

// ========== HANDLE WEBSOCKET MESSAGE ==========
void handleWebSocketMessage(WebsocketsMessage message) {
  Serial.println("WebSocket Msg: " + message.data());

  String json = message.data();
  
  // Parse JSON message - look for type field
  if (json.indexOf("\"type\"") != -1) {
    if (json.indexOf("\"limitTrigger\"") != -1) {
      // Server is simulating a theft trigger
      limitTriggered = true;
      sendTheftAlert();
      return;
    }
    
    if (json.indexOf("\"config\"") != -1) {
      // Handle config update
      if (json.indexOf("\"ownerNumber\"") != -1) {
        int idx = json.indexOf("\"ownerNumber\"");
        int valueStart = json.indexOf(":", idx) + 1;
        while (json.charAt(valueStart) == ' ' || json.charAt(valueStart) == '\"') valueStart++;
        int valueEnd = json.indexOf("\"", valueStart);
        if (valueEnd == -1) valueEnd = json.indexOf(",", valueStart);
        if (valueEnd == -1) valueEnd = json.indexOf("}", valueStart);
        
        String num = json.substring(valueStart, valueEnd);
        if (num.startsWith("+254")) {
          ownerNumber = num;
        } else if (num.startsWith("0")) {
          ownerNumber = "+254" + num.substring(1);
        }
        Serial.println("Updated owner number: " + ownerNumber);
      }
      
      if (json.indexOf("\"simNumber\"") != -1) {
        int idx = json.indexOf("\"simNumber\"");
        int valueStart = json.indexOf(":", idx) + 1;
        while (json.charAt(valueStart) == ' ' || json.charAt(valueStart) == '\"') valueStart++;
        int valueEnd = json.indexOf("\"", valueStart);
        if (valueEnd == -1) valueEnd = json.indexOf(",", valueStart);
        if (valueEnd == -1) valueEnd = json.indexOf("}", valueStart);
        
        String num = json.substring(valueStart, valueEnd);
        if (num.startsWith("+254")) {
          simNumber = num;
        } else if (num.startsWith("0")) {
          simNumber = "+254" + num.substring(1);
        }
        Serial.println("Updated SIM number: " + simNumber);
      }
      
      if (json.indexOf("\"simPin\"") != -1) {
        int idx = json.indexOf("\"simPin\"");
        int valueStart = json.indexOf(":", idx) + 1;
        while (json.charAt(valueStart) == ' ' || json.charAt(valueStart) == '\"') valueStart++;
        int valueEnd = json.indexOf("\"", valueStart);
        if (valueEnd == -1) valueEnd = json.indexOf(",", valueStart);
        if (valueEnd == -1) valueEnd = json.indexOf("}", valueStart);
        
        simPin = json.substring(valueStart, valueEnd);
        Serial.println("Updated SIM PIN: " + simPin);
      }
    }
  } else {
    // Legacy format - direct JSON properties
    if (json.indexOf("ownerNumber") != -1) {
      int idx = json.indexOf("ownerNumber");
      String num = json.substring(idx + 13);
      num = num.substring(0, num.indexOf("\""));
      ownerNumber = num;
      Serial.println("Updated owner number: " + ownerNumber);
    }
    if (json.indexOf("simNumber") != -1) {
      int idx = json.indexOf("simNumber");
      String num = json.substring(idx + 11);
      num = num.substring(0, num.indexOf("\""));
      simNumber = num;
      Serial.println("Updated SIM number: " + simNumber);
    }
  }
}

// ========== HANDLE WEBSOCKET EVENTS ==========
void handleWebSocketEvent(WebsocketsEvent event, String data) {
  switch (event) {
    case WebsocketsEvent::ConnectionOpened:
      Serial.println("WebSocket connected");
      break;
    case WebsocketsEvent::ConnectionClosed:
      Serial.println("WebSocket disconnected. Reconnecting...");
      connectWebSocket();
      break;
    case WebsocketsEvent::GotPing:
      Serial.println("WebSocket ping");
      break;
    case WebsocketsEvent::GotPong:
      Serial.println("WebSocket pong");
      break;
  }
}
