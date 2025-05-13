#define TINY_GSM_MODEM_SIM7600
#define TINY_GSM_RX_BUFFER 1024

#include <TinyGsmClient.h>
#include <ArduinoHttpClient.h>
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>

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

// Updated to use the 3000 port directly to avoid redirects
// Also, added support for custom paths when using tunneled services
TinyGsmClient gsmClient(modem);
HttpClient http(gsmClient, "kp36cdgx-3000.uks1.devtunnels.ms", 3000);  // Use port 3000 directly

// The full path prefix when using a tunneled service
const char* pathPrefix = "";  // Empty for direct connections, or add prefix like "/api/v1" if needed

// ==== State ====
float lastLat = 0.0;
float lastLng = 0.0;
unsigned long lastGPSTime = 0;
const unsigned long gpsInterval = 10000;
unsigned long lastConfigCheckTime = 0;
const unsigned long configCheckInterval = 60000; // Check config every minute
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

  // Enable GPS with more detailed setup
  Serial.println("Enabling GPS module...");
  
  // Specific GPS commands for SIM7600
  modem.sendAT("+CGPS=1,1");  // Turn on GPS with standalone mode
  if (modem.waitResponse(10000L) != 1) {
    Serial.println("Failed to enable GPS");
  } else {
    Serial.println("GPS enabled successfully");
  }
  
  // GPS power save mode off for faster fix
  modem.sendAT("+CGPSHOR=0");
  modem.waitResponse();
  
  // Wait a bit for GPS to initialize
  delay(2000);
}

// ========== MAIN LOOP ==========
void loop() {
  // Get and send GPS data
  getGPSData();
  
  // Check for config updates periodically
  checkConfig();

  // Check limit switch for theft detection
  bool currentLimit = digitalRead(LIMIT_SWITCH_PIN) == LOW;
  if (currentLimit && !limitTriggered) {
    limitTriggered = true;
    sendTheftAlert();
  } else if (!currentLimit) {
    limitTriggered = false;
  }

  // Check for remote theft trigger
  checkTheftTrigger();
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

// ========== GET GPS DATA ==========
void getGPSData() {
  if (millis() - lastGPSTime >= gpsInterval) {
    lastGPSTime = millis(); // Always update the timer first
    
    float lat, lng;
    bool hasGPS = false;
    
    // Try to get GPS data - attempt multiple times
    for (int i = 0; i < 3; i++) {
      if (modem.getGPS(&lat, &lng)) {
        hasGPS = true;
        break;
      }
      delay(1000);  // Wait a bit between attempts
    }
    
    // If we have GPS data or we're using mock data for testing
    if (hasGPS || (lastLat != 0.0 && lastLng != 0.0)) {
      // Use last known position if we couldn't get a new fix
      if (!hasGPS) {
        Serial.println("Using last known GPS position");
      } else {
        lastLat = lat;
        lastLng = lng;
      }
      
      // Simple JSON format for better compatibility
      String json = "{\"lat\":" + String(lastLat, 6) + ",\"lng\":" + String(lastLng, 6) + ",\"sim\":\"" + simNumber + "\"}";
      
      // Debug print full request details
      Serial.println("Sending GPS data to server...");
      Serial.print("URL: http://");
      Serial.print("kp36cdgx-3000.uks1.devtunnels.ms"); // Hardcoded instead of using getServerName()
      Serial.print(":");
      Serial.print("3000"); // Hardcoded instead of using getServerPort()
      Serial.println("/gps");
      Serial.println("Payload: " + json);
      
      // Try to send data with proper headers
      http.beginRequest();
      http.post(String(pathPrefix) + "/gps");
      http.sendHeader("Content-Type", "application/json");
      http.sendHeader("Content-Length", String(json.length()));
      http.beginBody();
      http.print(json);
      http.endRequest();
      
      // Get response
      int statusCode = http.responseStatusCode();
      String responseBody = http.responseBody();
      
      if (statusCode == 200) {
        Serial.println("GPS data sent successfully");
      } else {
        Serial.println("Failed to send GPS data: HTTP status " + String(statusCode));
        Serial.println("Response: " + responseBody);

        // Try parsing the response for redirect info
        if (statusCode == 301 || statusCode == 302 || statusCode == 307 || statusCode == 308) {
          Serial.println("Received a redirect. Check your server URL and port configuration.");
        }
      }
    } else {
      Serial.println("Failed to get GPS data from modem");
      
      // Let's try sending a mock location in test/development mode
      if (lastLat == 0.0 && lastLng == 0.0) {
        // Default to Moi University coordinates for testing
        lastLat = 0.2833;
        lastLng = 35.3167;
        Serial.println("Using mock location for testing");
      }
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

// ========== CHECK CONFIG ==========
void checkConfig() {
  if (millis() - lastConfigCheckTime >= configCheckInterval) {
    lastConfigCheckTime = millis();
    
    Serial.println("Checking for config updates...");
    
    http.beginRequest();
    http.get(String(pathPrefix) + "/api/config");
    http.endRequest();
    
    int statusCode = http.responseStatusCode();
    if (statusCode == 200) {
      String response = http.responseBody();
      Serial.println("Received config: " + response);
      
      // Parse JSON manually (simplified approach)
      if (response.indexOf("\"ownerNumber\"") != -1) {
        int idx = response.indexOf("\"ownerNumber\"");
        int valueStart = response.indexOf(":", idx) + 1;
        while (response.charAt(valueStart) == ' ' || response.charAt(valueStart) == '\"') valueStart++;
        int valueEnd = response.indexOf("\"", valueStart);
        if (valueEnd == -1) valueEnd = response.indexOf(",", valueStart);
        if (valueEnd == -1) valueEnd = response.indexOf("}", valueStart);
        
        String num = response.substring(valueStart, valueEnd);
        if (num.startsWith("+254")) {
          ownerNumber = num;
        } else if (num.startsWith("0")) {
          ownerNumber = "+254" + num.substring(1);
        }
        Serial.println("Updated owner number: " + ownerNumber);
      }
      
      if (response.indexOf("\"simNumber\"") != -1) {
        int idx = response.indexOf("\"simNumber\"");
        int valueStart = response.indexOf(":", idx) + 1;
        while (response.charAt(valueStart) == ' ' || response.charAt(valueStart) == '\"') valueStart++;
        int valueEnd = response.indexOf("\"", valueStart);
        if (valueEnd == -1) valueEnd = response.indexOf(",", valueStart);
        if (valueEnd == -1) valueEnd = response.indexOf("}", valueStart);
        
        String num = response.substring(valueStart, valueEnd);
        if (num.startsWith("+254")) {
          simNumber = num;
        } else if (num.startsWith("0")) {
          simNumber = "+254" + num.substring(1);
        }
        Serial.println("Updated SIM number: " + simNumber);
      }
      
      if (response.indexOf("\"simPin\"") != -1) {
        int idx = response.indexOf("\"simPin\"");
        int valueStart = response.indexOf(":", idx) + 1;
        while (response.charAt(valueStart) == ' ' || response.charAt(valueStart) == '\"') valueStart++;
        int valueEnd = response.indexOf("\"", valueStart);
        if (valueEnd == -1) valueEnd = response.indexOf(",", valueStart);
        if (valueEnd == -1) valueEnd = response.indexOf("}", valueStart);
        
        simPin = response.substring(valueStart, valueEnd);
        Serial.println("Updated SIM PIN: " + simPin);
      }
    } else {
      Serial.println("Failed to get config: HTTP status " + String(statusCode));
      if (statusCode == 301 || statusCode == 302 || statusCode == 307 || statusCode == 308) {
        Serial.println("Received a redirect. Check your server URL and port configuration.");
      }
    }
  }
}

// ========== CHECK FOR THEFT TRIGGER ==========
void checkTheftTrigger() {
  http.beginRequest();
  http.get(String(pathPrefix) + "/api/check-trigger");
  http.endRequest();
  
  int statusCode = http.responseStatusCode();
  if (statusCode == 200) {
    String response = http.responseBody();
    if (response.indexOf("\"triggered\":true") != -1) {
      Serial.println("Remote theft trigger detected!");
      limitTriggered = true;
      sendTheftAlert();
    }
  } else if (statusCode >= 300) {
    Serial.println("Failed to check theft trigger: HTTP status " + String(statusCode));
    if (statusCode == 301 || statusCode == 302 || statusCode == 307 || statusCode == 308) {
      Serial.println("Received a redirect. Check your server URL and port configuration.");
    }
  }
}
