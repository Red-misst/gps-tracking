#define TINY_GSM_MODEM_SIM7600
#define TINY_GSM_RX_BUFFER 1024

#include <TinyGsmClient.h>
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>

HardwareSerial SerialAT(1);
TinyGsm modem(SerialAT);
TinyGsmClient client(modem);
TinyGPSPlus gps;
WebSocketsClient webSocket;

// ==== Server Configuration ====
const char server[] = "gps-tracking-1rnf.onrender.com"; // Live server domain
const int wsPort = 443; // HTTPS port for secure WebSocket (wss://)
const char wsPath[] = "/"; // WebSocket path

// Alternative HTTP endpoint for fallback
const char httpServer[] = "gps-tracking-1rnf.onrender.com";
const int httpPort = 443; // HTTPS port

// ==== SIM and Owner Info ====
String simPin      = "2588";
String simNumber   = "+254743600744";
String ownerNumber = "+254714874451";

// ==== Pins ====
#define MODEM_RX         26
#define MODEM_TX         27
#define MODEM_PWRKEY     4
#define MODEM_POWER_ON   23
#define MODEM_RST        5
#define LIMIT_SWITCH_PIN 34
#define LED_PIN          12
#define BUZZER_PIN       13

// ==== State Variables ====
bool limitTriggered       = false;
bool systemEnabled        = false;
bool theftAlertSent       = false;
bool wsConnected          = false;
unsigned long lastGPSTime = 0;
unsigned long lastSMSTime = 0;
unsigned long lastHeartbeat = 0;
const unsigned long gpsInterval = 5000; // Send GPS every 5 seconds
const unsigned long smsRepeatInterval = 60000;
const unsigned long heartbeatInterval = 30000; // Heartbeat every 30 seconds

void setup() {
  Serial.begin(115200);
  delay(10);
  
  Serial.println("ESP32 Security System starting...");
  Serial.println("Target server: https://gps-tracking-1rnf.onrender.com");

  pinMode(LIMIT_SWITCH_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(MODEM_PWRKEY, OUTPUT);
  pinMode(MODEM_POWER_ON, OUTPUT);
  pinMode(MODEM_RST, OUTPUT);

  // Initialize modem and network
  initializeModem();
  
  // Initialize GPS
  initializeGPS();
  
  // Initialize WebSocket
  initializeWebSocket();

  // Startup notification
  Serial.println("System initialized. Sending startup notification...");
  sendStartupNotification();
}

void loop() {
  // Handle WebSocket events
  webSocket.loop();
  
  // Check limit switch for theft detection
  checkTheftSensor();
  
  // Send periodic GPS data
  sendPeriodicGPS();
  
  // Send heartbeat to server
  sendHeartbeat();
  
  // Update connection status LED
  updateStatusLED();
  
  delay(100); // Small delay to prevent overwhelming the loop
}

void initializeModem() {
  Serial.println("Initializing modem...");
  
  powerOnModem();
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(3000);

  modem.restart();
  modem.simUnlock(simPin.c_str());

  Serial.println("Connecting to network...");
  if (!modem.waitForNetwork(60000L)) {
    Serial.println("Network connection failed");
    while (true) {
      digitalWrite(LED_PIN, HIGH);
      delay(100);
      digitalWrite(LED_PIN, LOW);
      delay(100);
    }
  }
  
  if (!modem.gprsConnect("safaricom", "", "")) {
    Serial.println("GPRS connection failed");
    while (true) {
      digitalWrite(LED_PIN, HIGH);
      delay(200);
      digitalWrite(LED_PIN, LOW);
      delay(200);
    }
  }

  Serial.println("GSM/GPRS connected successfully");
}

void initializeGPS() {
  Serial.println("Initializing GPS...");
  
  // Enable GPS
  modem.sendAT("+CGPS=1,1");
  if (modem.waitResponse(10000L) != 1) {
    Serial.println("GPS initialization failed");
  } else {
    Serial.println("GPS initialized successfully");
  }
}

void initializeWebSocket() {
  Serial.println("Initializing WebSocket connection to live server...");
  
  // Set WebSocket event handler
  webSocket.onEvent(webSocketEvent);
  
  // Connect to WebSocket server with SSL (wss://)
  webSocket.beginSSL(server, wsPort, wsPath);
  
  // Set reconnection interval
  webSocket.setReconnectInterval(5000);
  
  // Enable heartbeat
  webSocket.enableHeartbeat(15000, 3000, 2);
  
  Serial.println("WebSocket client started for live server");
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("WebSocket Disconnected from live server");
      wsConnected = false;
      break;
      
    case WStype_CONNECTED:
      Serial.printf("WebSocket Connected to live server: %s\n", payload);
      wsConnected = true;
      
      // Identify ourselves as ESP32 device
      DynamicJsonDocument doc(1024);
      doc["type"] = "identify";
      doc["clientType"] = "esp32";
      doc["deviceInfo"]["simNumber"] = simNumber;
      doc["deviceInfo"]["deviceId"] = "ESP32-SECURE";
      doc["deviceInfo"]["server"] = "live";
      doc["timestamp"] = getCurrentTimestamp();
      
      String message;
      serializeJson(doc, message);
      webSocket.sendTXT(message);
      
      Serial.println("Identification sent to live server");
      break;
      
    case WStype_TEXT:
      Serial.printf("WebSocket message received from live server: %s\n", payload);
      handleWebSocketMessage((char*)payload);
      break;
      
    case WStype_BIN:
      Serial.printf("WebSocket binary data received, length: %u\n", length);
      break;
      
    case WStype_ERROR:
      Serial.printf("WebSocket Error: %s\n", payload);
      wsConnected = false;
      break;
      
    case WStype_PONG:
      Serial.println("WebSocket PONG received - connection alive");
      break;
      
    default:
      break;
  }
}

void handleWebSocketMessage(const char* message) {
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, message);
  
  if (error) {
    Serial.print("Failed to parse WebSocket message: ");
    Serial.println(error.c_str());
    return;
  }
  
  String type = doc["type"];
  
  if (type == "arm_system") {
    systemEnabled = true;
    limitTriggered = false;
    theftAlertSent = false;
    Serial.println("System ARMED via WebSocket");
    
    // Send confirmation
    sendWebSocketMessage("system_status", "ARMED");
    
    // Flash LED to confirm
    flashLED(3, 200);
    
  } else if (type == "disarm_system") {
    systemEnabled = false;
    limitTriggered = false;
    theftAlertSent = false;
    Serial.println("System DISARMED via WebSocket");
    
    // Send confirmation
    sendWebSocketMessage("system_status", "DISARMED");
    
    // Turn off alarms
    noTone(BUZZER_PIN);
    
  } else if (type == "get_status") {
    String status = systemEnabled ? "ARMED" : "DISARMED";
    sendWebSocketMessage("system_status", status);
    
  } else if (type == "welcome") {
    Serial.println("Received welcome message from server");
    
  } else {
    Serial.println("Unknown WebSocket command: " + type);
  }
}

void sendWebSocketMessage(String type, String payload) {
  if (!wsConnected) return;
  
  DynamicJsonDocument doc(1024);
  doc["type"] = type;
  doc["payload"] = payload;
  doc["timestamp"] = getCurrentTimestamp();
  doc["deviceId"] = "ESP32-SECURE";
  
  String message;
  serializeJson(doc, message);
  webSocket.sendTXT(message);
}

void sendWebSocketMessage(String type, JsonObject payload) {
  if (!wsConnected) return;
  
  DynamicJsonDocument doc(1024);
  doc["type"] = type;
  doc["payload"] = payload;
  doc["timestamp"] = getCurrentTimestamp();
  doc["deviceId"] = "ESP32-SECURE";
  
  String message;
  serializeJson(doc, message);
  webSocket.sendTXT(message);
}

void checkTheftSensor() {
  if (!systemEnabled) return;
  
  bool current = digitalRead(LIMIT_SWITCH_PIN) == LOW;
  unsigned long now = millis();

  if (current && !limitTriggered) {
    limitTriggered = true;
    theftAlertSent = false;
    Serial.println("THEFT DETECTED! Limit switch triggered!");
    
    // Immediate theft response
    activateAlarms();
    sendTheftAlert();
    lastSMSTime = now;
    theftAlertSent = true;
  }

  // Continue sending alerts if theft is ongoing
  if (limitTriggered && (now - lastSMSTime >= smsRepeatInterval)) {
    sendTheftAlert();
    lastSMSTime = now;
  }
}

void sendPeriodicGPS() {
  unsigned long now = millis();
  
  if (wsConnected && (now - lastGPSTime >= gpsInterval)) {
    sendCurrentGPSData();
    lastGPSTime = now;
  }
}

void sendCurrentGPSData() {
  // Try to get current GPS data
  unsigned long startTime = millis();
  bool gpsDataSent = false;
  
  while (millis() - startTime < 5000 && !gpsDataSent) { // 5 second timeout
    while (SerialAT.available()) {
      char c = SerialAT.read();
      if (gps.encode(c) && gps.location.isValid()) {
        float lat = gps.location.lat();
        float lng = gps.location.lng();
        
        // Create GPS data payload
        DynamicJsonDocument payload(512);
        payload["lat"] = lat;
        payload["lng"] = lng;
        payload["sim"] = simNumber;
        payload["speed"] = gps.speed.isValid() ? gps.speed.kmph() : 0;
        payload["altitude"] = gps.altitude.isValid() ? gps.altitude.meters() : 0;
        payload["satellites"] = gps.satellites.isValid() ? gps.satellites.value() : 0;
        payload["hdop"] = gps.hdop.isValid() ? gps.hdop.hdop() : 0;
        
        // Send via WebSocket
        sendWebSocketMessage("gps_data", payload.as<JsonObject>());
        
        Serial.printf("GPS data sent: %.6f, %.6f\n", lat, lng);
        gpsDataSent = true;
        break;
      }
    }
    delay(100);
  }
  
  if (!gpsDataSent) {
    Serial.println("No valid GPS data available");
  }
}

void sendTheftAlert() {
  Serial.println("Sending theft alert to live server...");
  
  // Get current GPS coordinates
  float lat = 0, lng = 0;
  bool hasGPS = false;
  
  // Try to get fresh GPS data for theft alert
  unsigned long startTime = millis();
  while (millis() - startTime < 10000) { // 10 second timeout for theft alert
    while (SerialAT.available()) {
      char c = SerialAT.read();
      if (gps.encode(c) && gps.location.isValid()) {
        lat = gps.location.lat();
        lng = gps.location.lng();
        hasGPS = true;
        break;
      }
    }
    if (hasGPS) break;
    delay(100);
  }
  
  // Send theft alert via WebSocket
  if (wsConnected) {
    DynamicJsonDocument payload(512);
    payload["message"] = "THEFT ALERT DETECTED!";
    payload["hasGPS"] = hasGPS;
    if (hasGPS) {
      payload["lat"] = lat;
      payload["lng"] = lng;
      payload["mapUrl"] = "https://gps-tracking-1rnf.onrender.com/gps-locator?lat=" + String(lat, 6) + "&lng=" + String(lng, 6);
    }
    payload["simNumber"] = simNumber;
    payload["deviceId"] = "ESP32-SECURE";
    payload["alertTime"] = getCurrentTimestamp();
    
    sendWebSocketMessage("theft_alert", payload.as<JsonObject>());
    Serial.println("Theft alert sent via WebSocket to live server");
  }
  
  // Send SMS alert with live server link
  String smsMessage;
  if (hasGPS) {
    String mapUrl = "https://gps-tracking-1rnf.onrender.com/gps-locator?lat=" + String(lat, 6) + "&lng=" + String(lng, 6);
    smsMessage = "THEFT ALERT! Vehicle location: " + mapUrl;
  } else {
    smsMessage = "THEFT ALERT! GPS unavailable. Check live dashboard: https://gps-tracking-1rnf.onrender.com";
  }
  
  modem.sendSMS(ownerNumber.c_str(), smsMessage.c_str());
  Serial.println("SMS sent: " + smsMessage);

  // Make call to owner
  modem.callNumber(ownerNumber.c_str());
  delay(15000); // Ring for 15 seconds
  modem.sendAT("+CHUP");
  modem.waitResponse();
  Serial.println("Alert call completed");
}

void sendHeartbeat() {
  unsigned long now = millis();
  
  if (wsConnected && (now - lastHeartbeat >= heartbeatInterval)) {
    DynamicJsonDocument payload(256);
    payload["status"] = systemEnabled ? "ARMED" : "DISARMED";
    payload["gpsActive"] = gps.location.isValid();
    payload["uptime"] = now;
    payload["limitSwitchState"] = digitalRead(LIMIT_SWITCH_PIN) == LOW ? "TRIGGERED" : "NORMAL";
    
    sendWebSocketMessage("heartbeat", payload.as<JsonObject>());
    lastHeartbeat = now;
  }
}

void sendStartupNotification() {
  // Send startup notification via SMS with live server link
  String startupMsg = "ESP32 Security System Online. ";
  startupMsg += "WebSocket: " + String(wsConnected ? "Connected" : "Connecting...");
  startupMsg += " Dashboard: https://gps-tracking-1rnf.onrender.com";
  
  modem.sendSMS(ownerNumber.c_str(), startupMsg.c_str());
  Serial.println("Startup SMS sent: " + startupMsg);
  
  // Test call
  modem.callNumber(ownerNumber.c_str());
  delay(10000); // Ring for 10 seconds
  modem.sendAT("+CHUP");
  modem.waitResponse();
  Serial.println("Startup notification completed");
}

// Add HTTP fallback for critical GPS data
void sendGPSViaHTTP(float lat, float lng) {
  if (!wsConnected) {
    Serial.println("WebSocket down, sending GPS via HTTP fallback...");
    
    // Create JSON payload
    String jsonData = "{\"lat\":" + String(lat, 6) + ",\"lng\":" + String(lng, 6) + ",\"sim\":\"" + simNumber + "\"}";
    
    // Send HTTP POST to live server
    if (client.connect(httpServer, httpPort)) {
      client.println("POST /gps HTTP/1.1");
      client.println("Host: gps-tracking-1rnf.onrender.com");
      client.println("Content-Type: application/json");
      client.println("Content-Length: " + String(jsonData.length()));
      client.println("Connection: close");
      client.println();
      client.println(jsonData);
      
      Serial.println("GPS data sent via HTTP fallback");
      client.stop();
    } else {
      Serial.println("HTTP fallback connection failed");
    }
  }
}

void powerOnModem() {
  digitalWrite(MODEM_POWER_ON, HIGH);
  digitalWrite(MODEM_RST, HIGH);
  digitalWrite(MODEM_PWRKEY, HIGH);
  delay(1000);
  digitalWrite(MODEM_PWRKEY, LOW);
  delay(1000);
}

void activateAlarms() {
  Serial.println("Activating theft alarms!");
  
  // Flash LED rapidly
  for (int i = 0; i < 20; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(50);
    digitalWrite(LED_PIN, LOW);
    delay(50);
  }
  
  // Sound buzzer alarm pattern
  for (int i = 0; i < 10; i++) {
    tone(BUZZER_PIN, 1000, 200);
    delay(300);
    tone(BUZZER_PIN, 1500, 200);
    delay(300);
  }
}

void flashLED(int times, int delayMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(delayMs);
    digitalWrite(LED_PIN, LOW);
    delay(delayMs);
  }
}

void updateStatusLED() {
  // LED patterns: 
  // Solid ON = WebSocket connected and system armed
  // Slow blink = WebSocket connected, system disarmed
  // Fast blink = No WebSocket connection
  
  static unsigned long lastLEDUpdate = 0;
  static bool ledState = false;
  unsigned long now = millis();
  
  if (wsConnected && systemEnabled) {
    // Solid ON when armed
    digitalWrite(LED_PIN, HIGH);
  } else if (wsConnected) {
    // Slow blink when connected but disarmed
    if (now - lastLEDUpdate >= 1000) {
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState);
      lastLEDUpdate = now;
    }
  } else {
    // Fast blink when disconnected
    if (now - lastLEDUpdate >= 200) {
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState);
      lastLEDUpdate = now;
    }
  }
}

String getCurrentTimestamp() {
  // Simple timestamp - you might want to implement proper NTP time sync
  return String(millis());
}
