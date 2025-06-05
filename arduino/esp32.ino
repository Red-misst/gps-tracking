#define TINY_GSM_MODEM_SIM7600
#define TINY_GSM_RX_BUFFER 1024

#include <TinyGsmClient.h>
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WebSocketsClient.h>

HardwareSerial SerialAT(1);
TinyGsm modem(SerialAT);
TinyGPSPlus gps; // Add GPS parser instance

// WiFi client for WebSocket connections
WiFiClient wifiClient;
WebSocketsClient webSocket;

// ==== Server Configuration ====
// Use local development server (the IP address should be your computer's local IP address)
const char server[] = "192.168.0.105";  // Replace with your actual computer's IP address
const int wsPort = 3000;                // Local development port
const char wsPath[] = "/";

// For production use:
// const char server[] = "gps-tracking-1rnf.onrender.com";
// const int wsPort = 80;

// ==== WiFi Configuration ====
const char* ssid = "Tenda_5C30C8";
const char* password = "op898989..";

bool wifiConnected = false;

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
bool gpsReading           = false;
bool wsInitialized        = false;
bool systemReady          = false;
bool wsConnectionAttempted = false;
unsigned long lastGPSTime = 0;
unsigned long lastSMSTime = 0;
unsigned long lastHeartbeat = 0;
unsigned long wsRetryTime = 0;
unsigned long systemStartTime = 0;
unsigned long lastPingTime = 0;
const unsigned long gpsInterval = 20000;    // 20 seconds
const unsigned long smsRepeatInterval = 60000;
const unsigned long heartbeatInterval = 180000;  // 3 minutes
const unsigned long wsRetryInterval = 60000;     // 60 seconds
const unsigned long systemStartupDelay = 20000;  // 20 seconds
const unsigned long pingInterval = 30000;        // 30 seconds

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("ESP32 Security System starting...");
  Serial.println("Target server: https://gps-tracking-1rnf.onrender.com");
  Serial.printf("Free heap at start: %d bytes\n", ESP.getFreeHeap());

  pinMode(LIMIT_SWITCH_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(MODEM_PWRKEY, OUTPUT);
  pinMode(MODEM_POWER_ON, OUTPUT);
  pinMode(MODEM_RST, OUTPUT);

  // Connect to WiFi first
  setupWiFi();
  
  // Initialize basic components
  initializeModem();
  initializeGPS();
  
  // Mark system start time for delayed WebSocket initialization
  systemStartTime = millis();
  systemReady = true;
  
  Serial.println("Basic system initialized. WebSocket will initialize in 20 seconds...");
  Serial.printf("Free heap after basic init: %d bytes\n", ESP.getFreeHeap());
}

void loop() {
  if (!systemReady) return;
  
  // Check WiFi connection and reconnect if necessary
  if (!wifiConnected) {
    setupWiFi();
  }
  
  // Initialize WebSocket after system has been running for a while
  if (!wsInitialized && !wsConnectionAttempted && (millis() - systemStartTime >= systemStartupDelay)) {
    setupWebSocket();
  }
  
  // Handle WebSocket events only if initialized
  if (wsInitialized) {
    webSocket.loop(); // Process WebSocket events
  }
  
  // Core system functions
  checkTheftSensor();
  sendPeriodicGPS();
  sendHeartbeat();
  retryWebSocketConnection();
  updateStatusLED();
  
  delay(100);  // Short delay to reduce CPU usage
}

void setupWiFi() {
  Serial.println("Connecting to WiFi...");
  
  WiFi.begin(ssid, password);
  
  int attemptCount = 0;
  while (WiFi.status() != WL_CONNECTED && attemptCount < 20) {
    delay(500);
    Serial.print(".");
    attemptCount++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to WiFi!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.printf("Connecting to WebSocket server at: %s:%d\n", server, wsPort);
    wifiConnected = true;
  } else {
    Serial.println("\nFailed to connect to WiFi. Will retry later.");
    wifiConnected = false;
  }
}

void setupWebSocket() {
  Serial.println("Setting up WebSocket connection...");
  
  if (!wifiConnected) {
    Serial.println("WiFi not connected. Cannot setup WebSocket.");
    wsConnectionAttempted = false;
    wsRetryTime = millis();
    return;
  }
  
  wsConnectionAttempted = true;
  
  Serial.printf("Connecting to server: %s:%d%s\n", server, wsPort, wsPath);
  
  // Configure WebSocket client
  webSocket.begin(server, wsPort, wsPath);
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);
  
  Serial.println("WebSocket initialized");
  
  wsInitialized = true;
  lastPingTime = millis();
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("WebSocket disconnected");
      wsConnected = false;
      break;
      
    case WStype_CONNECTED:
      Serial.println("WebSocket connected");
      wsConnected = true;
      
      // Send identification when connected
      sendIdentification();
      lastHeartbeat = millis();
      break;
      
    case WStype_TEXT:
      // Handle received message
      handleWebSocketMessage(payload, length);
      break;
      
    case WStype_PING:
      Serial.println("Received ping");
      break;
      
    case WStype_PONG:
      Serial.println("Received pong");
      break;
      
    case WStype_ERROR:
      Serial.println("WebSocket error occurred");
      break;
  }
}

void handleWebSocketMessage(uint8_t* payload, size_t length) {
  StaticJsonDocument<256> doc; 
  
  DeserializationError error = deserializeJson(doc, payload, length);
  if (error) {
    Serial.print("JSON parse error: ");
    Serial.println(error.c_str());
    return;
  }
  
  Serial.print("Received: ");
  Serial.println((char*)payload);
  
  const char* type = doc["type"];
  if (type == nullptr) return;
  
  if (strcmp(type, "arm_system") == 0) {
    systemEnabled = true;
    limitTriggered = false;
    theftAlertSent = false;
    Serial.println("System ARMED via WebSocket");
    sendSystemStatus("ARMED");
    flashLED(3, 200);
    
  } else if (strcmp(type, "disarm_system") == 0) {
    systemEnabled = false;
    limitTriggered = false;
    theftAlertSent = false;
    Serial.println("System DISARMED via WebSocket");
    sendSystemStatus("DISARMED");
    noTone(BUZZER_PIN);
    
  } else if (strcmp(type, "get_status") == 0) {
    String status = systemEnabled ? "ARMED" : "DISARMED";
    sendSystemStatus(status);
  
  } else if (strcmp(type, "update_phone") == 0) {
    const char* newOwnerNumber = doc["ownerNumber"];
    if (newOwnerNumber != nullptr) {
      ownerNumber = String(newOwnerNumber);
      Serial.println("Owner number updated to: " + ownerNumber);
      sendConfigUpdate();
    }
  
  } else if (strcmp(type, "simulate_theft") == 0) {
    Serial.println("Simulating theft via WebSocket");
    if (systemEnabled) {
      limitTriggered = true;
      theftAlertSent = false;
      activateSimpleAlarm();
      sendTheftSMS();
      lastSMSTime = millis();
      theftAlertSent = true;
      sendTheftAlert();
    } else {
      sendErrorMessage("System is not armed. Cannot simulate theft.");
    }
  }
}

void sendIdentification() {
  StaticJsonDocument<200> doc;
  doc["type"] = "identify";
  doc["clientType"] = "esp32";
  doc["sim"] = simNumber;
  
  String output;
  serializeJson(doc, output);
  
  sendWebSocketMessage(output);
  Serial.println("[WS] Sent identification");
}

void sendSystemStatus(const String& status) {
  StaticJsonDocument<200> doc;
  doc["type"] = "system_status";
  doc["payload"] = status;
  doc["device"] = "ESP32";
  
  String output;
  serializeJson(doc, output);
  
  sendWebSocketMessage(output);
  Serial.println("[WS] Sent system status: " + status);
}

void sendConfigUpdate() {
  StaticJsonDocument<200> doc;
  doc["type"] = "config_update";
  doc["payload"]["ownerNumber"] = ownerNumber;
  doc["payload"]["success"] = true;
  
  String output;
  serializeJson(doc, output);
  
  sendWebSocketMessage(output);
  Serial.println("[WS] Sent config update");
}

void sendTheftAlert() {
  StaticJsonDocument<200> doc;
  doc["type"] = "theft_alert";
  doc["payload"]["message"] = "THEFT_DETECTED";
  doc["payload"]["device"] = "ESP32";
  
  String output;
  serializeJson(doc, output);
  
  sendWebSocketMessage(output);
  Serial.println("[WS] Sent theft alert");
}

void sendErrorMessage(const String& errorMsg) {
  StaticJsonDocument<200> doc;
  doc["type"] = "error";
  doc["payload"] = errorMsg;
  
  String output;
  serializeJson(doc, output);
  
  sendWebSocketMessage(output);
  Serial.println("[WS] Sent error: " + errorMsg);
}

void sendGpsData(float lat, float lng) {
  StaticJsonDocument<200> doc;
  doc["type"] = "gps_data";
  doc["payload"]["lat"] = lat;
  doc["payload"]["lng"] = lng;
  doc["payload"]["sim"] = simNumber;
  
  String output;
  serializeJson(doc, output);
  
  sendWebSocketMessage(output);
  Serial.printf("[WS] Sent GPS: %.6f, %.6f\n", lat, lng);
}

void sendWebSocketMessage(const String& message) {
  if (!wsConnected || !wsInitialized) return;
  String messageCopy = message;  // Create a non-const copy of the string
  webSocket.sendTXT(messageCopy);  // Pass the non-const copy
}

void sendHeartbeat() {
  unsigned long now = millis();
  
  if (wsConnected && (now - lastHeartbeat >= heartbeatInterval)) {
    StaticJsonDocument<200> doc;
    doc["type"] = "heartbeat";
    doc["payload"]["status"] = systemEnabled ? "ARMED" : "DISARMED";
    doc["payload"]["device"] = "ESP32";
    
    String output;
    serializeJson(doc, output);
    
    sendWebSocketMessage(output);
    lastHeartbeat = now;
    Serial.println("Heartbeat sent");
  }
}

void retryWebSocketConnection() {
  // Don't retry if already connected or recently attempted
  if (wsConnected) return;
  
  unsigned long now = millis();
  if (wsConnectionAttempted && (now - wsRetryTime < wsRetryInterval)) return;
  
  Serial.println("Retrying WebSocket connection...");
  
  // Reset connection state
  wsConnectionAttempted = false;
  wsInitialized = false;
  
  // Small delay to let system stabilize
  delay(1000);
  
  setupWebSocket();
  wsRetryTime = now;
}

void initializeModem() {
  Serial.println("Initializing modem...");
  
  powerOnModem();
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(3000);

  int retryCount = 0;
  while (retryCount < 3) {
    Serial.println("Attempting modem restart...");
    if (modem.restart()) {
      Serial.println("Modem restarted successfully");
      break;
    }
    retryCount++;
    Serial.println("Modem restart failed, retrying...");
    delay(3000);
  }
  
  // Unlock SIM
  if (!modem.simUnlock(simPin.c_str())) {
    Serial.println("SIM unlock failed");
  }

  Serial.println("Modem initialization completed");
}

void initializeGPS() {
  Serial.println("Initializing GPS...");
  
  modem.sendAT("+CGPS=1,1");
  if (modem.waitResponse(10000L) != 1) {
    Serial.println("GPS initialization failed");
  } else {
    Serial.println("GPS initialized successfully");
  }
}

void checkTheftSensor() {
  if (!systemEnabled) return;
  
  bool current = digitalRead(LIMIT_SWITCH_PIN) == LOW;
  unsigned long now = millis();

  if (current && !limitTriggered) {
    limitTriggered = true;
    theftAlertSent = false;
    Serial.println("THEFT DETECTED!");
    
    activateSimpleAlarm();
    sendTheftSMS();
    lastSMSTime = now;
    theftAlertSent = true;
    
    // Send WebSocket alert if available
    if (wsConnected) {
      sendTheftAlert();
    }
  }

  if (limitTriggered && (now - lastSMSTime >= smsRepeatInterval)) {
    sendTheftSMS();
    lastSMSTime = now;
  }
}

void sendPeriodicGPS() {
  unsigned long now = millis();
  
  if (wsConnected && (now - lastGPSTime >= gpsInterval)) {
    sendSimpleGPSData();
    lastGPSTime = now;
  }
}

void sendSimpleGPSData() {
  if (gpsReading) return;
  
  gpsReading = true;
  
  unsigned long startTime = millis();
  
  while (millis() - startTime < 1500) {  // 1.5 second timeout
    yield();
    
    if (SerialAT.available()) {
      char c = SerialAT.read();
      if (gps.encode(c) && gps.location.isValid()) {
        float lat = gps.location.lat();
        float lng = gps.location.lng();
        
        if (wsConnected && wsInitialized) {
          sendGpsData(lat, lng);
        }
        break;
      }
    }
    yield();
    delay(50);
  }
  
  gpsReading = false;
}

void sendTheftSMS() {
  Serial.println("Sending theft SMS...");
  
  String smsMessage = "THEFT ALERT! Check dashboard: http://localhost:3000";
  
  if (modem.sendSMS(ownerNumber.c_str(), smsMessage.c_str())) {
    Serial.println("Theft SMS sent successfully");
  } else {
    Serial.println("Theft SMS failed");
  }
}

void powerOnModem() {
  digitalWrite(MODEM_POWER_ON, HIGH);
  digitalWrite(MODEM_RST, HIGH);
  digitalWrite(MODEM_PWRKEY, HIGH);
  delay(1000);
  digitalWrite(MODEM_PWRKEY, LOW);
  delay(2000);  // Longer delay for stability
}

void activateSimpleAlarm() {
  Serial.println("Activating simple alarm!");
  
  // Very simple alarm pattern
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, HIGH);
    tone(BUZZER_PIN, 1000, 200);
    delay(300);
    digitalWrite(LED_PIN, LOW);
    delay(300);
    yield();
  }
}

void flashLED(int times, int delayMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(delayMs);
    digitalWrite(LED_PIN, LOW);
    delay(delayMs);
    yield();
  }
}

void updateStatusLED() {
  static unsigned long lastLEDUpdate = 0;
  static bool ledState = false;
  unsigned long now = millis();
  
  if (wsConnected && systemEnabled) {
    digitalWrite(LED_PIN, HIGH);
  } else if (wsConnected) {
    if (now - lastLEDUpdate >= 1000) {
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState);
      lastLEDUpdate = now;
    }
  } else {
    if (now - lastLEDUpdate >= 500) {
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState);
      lastLEDUpdate = now;
    }
  }
}