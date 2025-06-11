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
// Use production server with secure WebSocket
const char server[] = "gps-tracking-1rnf.onrender.com";  // Production server
const int wsPort = 443;                                  // Standard HTTPS/WSS port
const char wsPath[] = "/";

// For local development:
// const char server[] = "192.168.0.106";  // Local IP address
// const int wsPort = 3000;                // Local development port

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
#define LED_PIN          12           // External LED
#define BUILTIN_LED      2            // ESP32 built-in LED
#define BUZZER_PIN       13

// ==== Default GPS Location (Moi University, Kesses, Eldoret) ====
const float DEFAULT_LAT = 0.2833;
const float DEFAULT_LNG = 35.3167;

// ==== State Variables ====
bool limitTriggered       = false;
bool systemEnabled        = true;     // System is armed by default
bool theftAlertSent       = false;
bool wsConnected          = false;
bool gpsReading           = false;
bool wsInitialized        = false;
bool systemReady          = false;
bool wsConnectionAttempted = false;
bool callInProgress       = false;
volatile bool interruptTriggered = false;  // Volatile flag for interrupt detection
unsigned long lastGPSTime = 0;
unsigned long lastSMSTime = 0;
unsigned long lastHeartbeat = 0;
unsigned long wsRetryTime = 0;
unsigned long systemStartTime = 0;
unsigned long lastPingTime = 0;
unsigned long callStartTime = 0;
const unsigned long gpsInterval = 5000;     // 5 seconds for more frequent updates
const unsigned long smsRepeatInterval = 60000;
const unsigned long heartbeatInterval = 180000;  // 3 minutes
const unsigned long wsRetryInterval = 60000;     // 60 seconds
const unsigned long systemStartupDelay = 20000;  // 20 seconds
const unsigned long pingInterval = 30000;        // 30 seconds
const unsigned long callDuration = 15000;        // Changed back to 15 seconds call duration

// Interrupt Service Routine for limit switch
void IRAM_ATTR limitSwitchISR() {
  if (systemEnabled) {
    interruptTriggered = true;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("ESP32 Security System starting...");
  Serial.println("Target server: https://gps-tracking-1rnf.onrender.com");

  pinMode(LIMIT_SWITCH_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUILTIN_LED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(MODEM_PWRKEY, OUTPUT);
  pinMode(MODEM_POWER_ON, OUTPUT);
  pinMode(MODEM_RST, OUTPUT);
  
  // Attach interrupt to limit switch pin
  attachInterrupt(digitalPinToInterrupt(LIMIT_SWITCH_PIN), limitSwitchISR, CHANGE);
  
  // Connect to WiFi first
  setupWiFi();
  
  // Initialize basic components
  initializeModem();
  initializeGPS();
  
  // Mark system start time for delayed WebSocket initialization
  systemStartTime = millis();
  systemReady = true;
  
  // Signal that system is starting in armed mode
  flashLED(3, 200);
  
  Serial.println("Basic system initialized. WebSocket will initialize in 20 seconds...");
}

void loop() {
  if (!systemReady) return;
  
  // Process limit switch trigger with highest priority
  if (interruptTriggered) {
    handleTheftTrigger();
    interruptTriggered = false;
  }
  
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
  handlePhoneCall();
  
  delay(100);  // Short delay to reduce CPU usage
}

// Handle ongoing phone call
void handlePhoneCall() {
  if (!callInProgress) return;
  
  unsigned long now = millis();
  
  // End the call after the specified duration
  if (now - callStartTime >= callDuration) {
    Serial.println("Ending theft alert call");
    modem.callHangup();
    callInProgress = false;
    
    // Add a short beep to indicate end of call
    tone(BUZZER_PIN, 2000, 200);
  }
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
  
  Serial.printf("Connecting to secure server: %s:%d%s\n", server, wsPort, wsPath);
  
  // Configure WebSocket client for SSL connection
  webSocket.beginSSL(server, wsPort, wsPath);
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);
  
  Serial.println("Secure WebSocket initialized");
  
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
      
      // Also send initial system status (since we're in armed mode by default)
      sendSystemStatus(systemEnabled ? "ARMED" : "DISARMED");
      
      // Immediately send GPS position
      sendRealTimeGPSData();
      
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

  // Backup polling detection method
  if (current && !limitTriggered) {
    handleTheftTrigger();
  }

  // Periodic SMS resending logic remains unchanged
  if (limitTriggered && (now - lastSMSTime >= smsRepeatInterval)) {
    sendTheftSMS();
    lastSMSTime = now;
  }
}

// New function to handle theft trigger events
void handleTheftTrigger() {
  if (!systemEnabled) return;
  
  limitTriggered = true;
  theftAlertSent = false;
  Serial.println("THEFT DETECTED via interrupt!");
  
  activateBuzzerAlarm();
  activateLEDAlarm();
  sendTheftSMS();
  initiateTheftCall();
  lastSMSTime = millis();
  theftAlertSent = true;
  
  // Send WebSocket alert if available
  if (wsConnected) {
    sendTheftAlert();
  }
}

void initiateTheftCall() {
  Serial.println("Initiating theft alert call...");
  
  // Start the call
  if (modem.callNumber(ownerNumber.c_str())) {
    Serial.println("Call initiated successfully");
    callInProgress = true;
    callStartTime = millis();
  } else {
    Serial.println("Call failed");
  }
}

void sendPeriodicGPS() {
  unsigned long now = millis();
  
  if (wsConnected && (now - lastGPSTime >= gpsInterval)) {
    sendRealTimeGPSData();
    lastGPSTime = now;
  }
}

void sendRealTimeGPSData() {
  if (gpsReading) return;
  
  gpsReading = true;
  bool gpsValid = false;
  float lat = DEFAULT_LAT;
  float lng = DEFAULT_LNG;
  
  unsigned long startTime = millis();
  
  // Try to get real GPS data with timeout
  while (millis() - startTime < 1500) {  // 1.5 second timeout
    yield();
    
    if (SerialAT.available()) {
      char c = SerialAT.read();
      if (gps.encode(c) && gps.location.isValid()) {
        lat = gps.location.lat();
        lng = gps.location.lng();
        gpsValid = true;
        break;
      }
    }
    yield();
    delay(10);
  }
  
  // Send data regardless of GPS validity (will use default if not valid)
  if (wsConnected && wsInitialized) {
    sendGpsData(lat, lng);
    Serial.printf("Sent %s GPS data: %f, %f\n", 
                  gpsValid ? "valid" : "default", lat, lng);
  }
  
  gpsReading = false;
}

void activateBuzzerAlarm() {
  Serial.println("Activating theft alarm with buzzer!");
  
  // More attention-grabbing alarm pattern
  for (int i = 0; i < 5; i++) {
    // High pitched emergency tone
    tone(BUZZER_PIN, 2000, 200);
    delay(300);
    
    // Low pitched emergency tone
    tone(BUZZER_PIN, 1000, 300);
    delay(400);
    
    yield();
  }
  
  // Continuous alarm tone
  tone(BUZZER_PIN, 1500, 2000); // 2 second alarm
}

void activateLEDAlarm() {
  Serial.println("Activating LED alarm indicators!");
  
  // Use both the built-in and external LEDs in alternating pattern
  for (int i = 0; i < 10; i++) {
    digitalWrite(LED_PIN, HIGH);
    digitalWrite(BUILTIN_LED, LOW);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    digitalWrite(BUILTIN_LED, HIGH);
    delay(100);
    yield();
  }
  
  // Leave external LED on as a continuous indicator
  digitalWrite(LED_PIN, HIGH);
  digitalWrite(BUILTIN_LED, LOW);
}

void sendTheftSMS() {
  Serial.println("Sending theft SMS with location...");
  
  float lat = DEFAULT_LAT;
  float lng = DEFAULT_LNG;
  
  // Try to get current location for the SMS
  if (gps.location.isValid()) {
    lat = gps.location.lat();
    lng = gps.location.lng();
  }
  
  // Create a message with coordinates and map link
  String smsMessage = "THEFT ALERT! Vehicle location: " + 
                     String(lat, 6) + "," + String(lng, 6) + 
                     " View map: https://gps-tracking-1rnf.onrender.com/?lat=" + 
                     String(lat, 6) + "&lng=" + String(lng, 6) + "&alert=theft";
  
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
  
  if (limitTriggered) {
    // Rapid flashing during theft alert (using built-in LED, external LED handled by alarm)
    if (now - lastLEDUpdate >= 100) {
      ledState = !ledState;
      digitalWrite(BUILTIN_LED, ledState);
      lastLEDUpdate = now;
    }
  } else if (wsConnected && systemEnabled) {
    // Armed mode - steady on
    digitalWrite(BUILTIN_LED, HIGH);
    digitalWrite(LED_PIN, HIGH);
  } else if (wsConnected) {
    // Connected but disarmed - slow blink
    if (now - lastLEDUpdate >= 1000) {
      ledState = !ledState;
      digitalWrite(BUILTIN_LED, ledState);
      digitalWrite(LED_PIN, ledState);
      lastLEDUpdate = now;
    }
  } else {
    // Not connected - fast blink
    if (now - lastLEDUpdate >= 500) {
      ledState = !ledState;
      digitalWrite(BUILTIN_LED, ledState);
      digitalWrite(LED_PIN, ledState);
      lastLEDUpdate = now;
    }
  }
}