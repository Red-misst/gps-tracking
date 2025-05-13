# Real-time GPS Vehicle Tracking System with Theft Detection

## Project Overview

This repository contains a comprehensive IoT solution for real-time vehicle tracking and theft detection. The system utilizes an ESP32 microcontroller with a SIM7600 cellular modem and GPS module to provide continuous location monitoring, while also implementing theft detection through a physical limit switch sensor and remote trigger capabilities.

The solution consists of three main components:
1. An ESP32-based hardware device for GPS tracking and cellular communication
2. A Node.js web server for data processing and storage
3. A web-based dashboard for real-time monitoring and control

## Table of Contents

- [System Architecture](#system-architecture)
- [Hardware Components](#hardware-components)
- [Software Components](#software-components)
- [Data Flow](#data-flow)
- [Communication Protocols](#communication-protocols)
- [Security Considerations](#security-considerations)
- [Installation and Setup](#installation-and-setup)
- [Configuration](#configuration)
- [Theft Detection Mechanism](#theft-detection-mechanism)
- [Future Improvements](#future-improvements)
- [Technical Specifications](#technical-specifications)

## System Architecture

The GPS tracking system follows a client-server architecture with real-time communication capabilities:

```mermaid
flowchart TD
    %% Main components
    ESP32[ESP32 Device<br>llytgo.ino] <--> Server[Node.js Server<br>index.js]
    Server <--> Dashboard[Web Dashboard<br>Client]
    
    %% Hardware components attached to ESP32
    GPS[GPS Module] --> ESP32
    Modem[Cellular Modem] --> ESP32
    LimitSwitch[Limit Switch<br>Sensor] --> ESP32
    LED[Status LED] --> ESP32
    Buzzer[Alarm Buzzer] --> ESP32
    
    %% Mobile alerts
    ESP32 -- SMS/Call --> OwnerPhone[Vehicle Owner<br>Mobile Phone]
    Dashboard -- View --> Browser[Web Browser]
    Browser -- Access --> OwnerPhone
    
    %% Communication protocols
    ESP32 -. HTTP POST .-> Server
    Server -. WebSocket .-> Dashboard
    ESP32 -- Cellular Network --> Safaricom[Cellular Provider]
    
    %% Styling
    classDef hardware fill:#e6f7ff,stroke:#0099cc,stroke-width:2px;
    classDef software fill:#f9f0ff,stroke:#9933cc,stroke-width:2px;
    classDef communication fill:#fff0e6,stroke:#ff6600,stroke-width:2px;
    classDef user fill:#e6ffe6,stroke:#009900,stroke-width:2px;
    
    class ESP32,GPS,Modem,LimitSwitch,LED,Buzzer hardware;
    class Server,Dashboard software;
    class Safaricom communication;
    class OwnerPhone,Browser user;
```

### Flowchart: System Operation Logic

```mermaid
flowchart TD
    A[System Start] --> B[Initialize Hardware]
    B --> B1[Initialize LED & Buzzer]
    B1 --> B2[Setup GPIO Pins]
    B2 --> C[Connect to Cellular Network]
    C --> C1{Network Connected?}
    C1 -->|No| C2[Retry with Timeout]
    C2 --> C
    C1 -->|Yes| D[Enable GPS Module]
    D --> D1[Configure GPS Parameters]
    D1 --> D2[Set Acquisition Mode]
    D2 --> E[Enter Main Loop]
    
    %% Main parallel processes
    E --> F[Periodic GPS Data Collection]
    E --> G[Check Limit Switch]
    E --> H[Check Remote Trigger]
    E --> J[Check Configuration Updates]
    
    %% GPS Data Collection and Transmission Flow
    F --> F1[Obtain GPS Coordinates]
    F1 --> F2{Valid GPS Data?}
    F2 -->|No| F3[Use Last Known Position]
    F2 -->|Yes| F4[Update Current Position]
    F3 --> F5[Format JSON Data]
    F4 --> F5
    F5 --> F6[Send HTTP POST to Server]
    F6 --> F7{Transmission Successful?}
    F7 -->|No| F8[Log Error & Retry Later]
    F7 -->|Yes| F9[Log Success]
    F8 --> E
    F9 --> E
    
    %% Theft Detection Flow
    G --> G1{Limit Switch Triggered?}
    G1 -->|No| G2[Reset Alert Flag if Active]
    G1 -->|Yes| G3[Set Alert Flag]
    G3 --> K[Theft Alert Sequence]
    G2 --> E
    
    H --> H1[Query Server for Remote Trigger]
    H1 --> H2{Trigger Active?}
    H2 -->|Yes| K
    H2 -->|No| E
    
    %% Theft Alert Sequence
    K --> K1[Activate Buzzer with Alarm Pattern]
    K1 --> K2[Flash Warning LED]
    K2 --> K3[Format SMS with GPS Coordinates]
    K3 --> K4[Send SMS to Owner]
    K4 --> K5[Initiate Call to Owner]
    K5 --> K6[Wait for Timeout]
    K6 --> K7[End Call]
    K7 --> E
    
    %% Config Update Flow
    J --> J1[Request Config from Server]
    J1 --> J2{New Config Available?}
    J2 -->|Yes| J3[Update System Parameters]
    J2 -->|No| E
    J3 --> E
    
    %% Styling
    classDef process fill:#f9f,stroke:#333,stroke-width:2px;
    classDef decision fill:#bbf,stroke:#333,stroke-width:2px;
    classDef alert fill:#f66,stroke:#333,stroke-width:2px;
    
    class F,G,H,J process;
    class C1,F2,G1,H2,J2 decision;
    class K,K1,K2,K3,K4,K5 alert;
```

## Hardware Components

| Component | Model | Description |
|-----------|-------|-------------|
| Microcontroller | ESP32 | Dual-core processor with built-in Wi-Fi and Bluetooth |
| Cellular/GPS Module | SIM7600 | 4G LTE module with integrated GPS receiver |
| SIM Card | Safaricom | Cellular data connectivity |
| Limit Switch | - | Physical sensor for theft detection |
| Buzzer | Piezoelectric | Audible alert during theft detection |
| LED | High-brightness | Visual indicator for system status and alerts |
| Power Supply | - | Battery or vehicle power connection |

### Pin Configuration

| ESP32 Pin | Component | Function |
|-----------|-----------|----------|
| GPIO 26 | SIM7600 | Rx (MODEM_RX) |
| GPIO 27 | SIM7600 | Tx (MODEM_TX) |
| GPIO 4 | SIM7600 | Power Key (MODEM_PWRKEY) |
| GPIO 23 | SIM7600 | Power On (MODEM_POWER_ON) |
| GPIO 5 | SIM7600 | Reset (MODEM_RST) |
| GPIO 34 | Limit Switch | Theft detection sensor (LIMIT_SWITCH_PIN) |
| GPIO 13 | Buzzer | Audible alarm output (BUZZER_PIN) |
| GPIO 12 | LED | Visual alert indicator (LED_PIN) |

## Software Components

### 1. ESP32 Firmware (`arduino/llytgo.ino`)

The firmware for the ESP32 microcontroller handles:
- GPS data acquisition
- Network connectivity
- HTTP communication with the server
- Theft detection via limit switch
- SMS/call alerts to the vehicle owner
- Audible alarm (buzzer) activation
- Visual alerts through LED indicators

**Key Dependencies:**
- TinyGSM Library
- ArduinoHttpClient Library
- TinyGPSPlus Library
- HardwareSerial Library

### 2. Server Application (`index.js`)

The Node.js server component:
- Processes incoming GPS data
- Maintains location history
- Serves the web dashboard
- Manages device configuration
- Facilitates bi-directional communication via WebSockets
- Handles remote theft triggers

**Key Dependencies:**
- Express.js
- ws (WebSocket)
- HTTP

### 3. Web Dashboard (`public/index.html` & `gps-locator/index.html`)

The web-based user interface:
- Displays real-time vehicle location on a map
- Shows movement history
- Provides theft simulation controls
- Allows configuration of phone numbers and SIM PIN
- Displays system status and alerts

**Key Dependencies:**
- Leaflet.js (Mapping)
- Tailwind CSS (Styling)

## Data Flow

### Outbound Data Flow (ESP32 to Server)

The ESP32 device collects GPS coordinates and sends them to the server:

```mermaid
flowchart LR
    A[GPS Acquisition] --> B[Process Coordinates]
    B --> C[Format JSON Data]
    C --> D[Add Device Metadata]
    D --> E[Send HTTP POST Request]
    E --> F{Server Response}
    F -->|Success| G[Log Success]
    F -->|Failure| H[Retry Logic]
    H --> E
    
    classDef process fill:#d1f0ff,stroke:#0078d7,stroke-width:1px;
    classDef data fill:#d8f3d8,stroke:#107c10,stroke-width:1px;
    classDef network fill:#ffefe3,stroke:#d16b00,stroke-width:1px;
    
    class A,B process;
    class C,D data;
    class E,F,G,H network;
```

**GPS Data Format:**
```json
{
  "lat": 0.2833,
  "lng": 35.3167,
  "sim": "+254114931050"
}
```

### Inbound Data Flow (Server to Dashboard)

The server processes GPS data and forwards it to all connected dashboard clients:

```mermaid
flowchart LR
    A[Data Reception] --> B[Timestamp Addition]
    B --> C[Format Validation]
    C --> D[GPS History Storage]
    D --> E[Add to Memory Cache]
    
    %% Handle different client types
    D --> F[WebSocket Broadcast]
    F --> G[Dashboard Clients]
    F --> H[Mobile Clients]
    
    classDef server fill:#e1d1ff,stroke:#5c2d91,stroke-width:1px;
    classDef storage fill:#d8f3d8,stroke:#107c10,stroke-width:1px;
    classDef client fill:#ffefe3,stroke:#d16b00,stroke-width:1px;
    
    class A,B,C server;
    class D,E storage;
    class F,G,H client;
```

**WebSocket Message Format:**
```json
{
  "type": "gps",
  "payload": {
    "lat": 0.2833,
    "lng": 35.3167,
    "sim": "+254114931050",
    "timestamp": "2025-05-13T12:34:56.789Z"
  }
}
```

## Communication Protocols

| Protocol | Usage | Port |
|----------|-------|------|
| HTTP | GPS data transmission (ESP32 to Server) | 3000 |
| WebSocket | Real-time updates (Server to Dashboard) | 3000/80 |
| SMS | Theft alerts (ESP32 to Owner) | - |
| Cellular Call | Theft alerts (ESP32 to Owner) | - |

## Theft Detection Mechanism

The system implements two methods of theft detection:

1. **Physical Limit Switch**
   - Connected to GPIO 34 on the ESP32
   - Triggered when tampered with or removed
   - Immediately sends SMS and initiates a call to the owner

2. **Remote Trigger**
   - Dashboard interface allows manual triggering
   - Server sets a flag that the ESP32 periodically checks
   - Simulates a theft scenario for testing

**Theft Response Sequence:**

```mermaid
flowchart TD
    A[Theft Detected] --> B{Trigger Source}
    B -->|Limit Switch| C[Physical Sensor Triggered]
    B -->|Remote API| D[Remote Trigger Activated]
    
    C --> E[Set Theft Flag]
    D --> E
    
    E --> F1[Activate Buzzer]
    E --> F2[Flash LED Warning]
    E --> F3[Prepare SMS Alert]
    
    F1 --> G1[Generate Alarm Pattern<br>High-Low-High Tone]
    F2 --> G2[Rapid Flashing<br>Red Warning]
    F3 --> G3[Format SMS with<br>GPS Coordinates]
    
    G3 --> H[Send SMS to Owner]
    H --> I[Initiate Phone Call]
    I --> J[Wait for Answer<br>15 seconds]
    J --> K[End Call]
    
    L[Continue Alarm<br>Until Reset] --> L
    G1 --> L
    G2 --> L
    
    classDef trigger fill:#ffcccc,stroke:#cc0000,stroke-width:2px;
    classDef action fill:#ccffcc,stroke:#00cc00,stroke-width:2px;
    classDef alert fill:#ffebcc,stroke:#ff9900,stroke-width:2px;
    classDef communication fill:#cce5ff,stroke:#0066cc,stroke-width:2px;
    
    class A,B,C,D trigger;
    class E,F1,F2,F3 action;
    class G1,G2,G3,L alert;
    class H,I,J,K communication;
```

## Configuration

The system supports remote configuration via the web dashboard:

| Parameter | Default | Description |
|-----------|---------|-------------|
| Owner Number | +254714874451 | Phone number to receive theft alerts |
| SIM Number | +254114931050 | SIM card number in the device |
| SIM PIN | 6577 | PIN code for the SIM card |

Configuration changes are:
1. Saved on the server
2. Transmitted to the ESP32 during periodic checks
3. Used for subsequent operations

## Technical Specifications

### ESP32 Performance Metrics

| Metric | Value |
|--------|-------|
| GPS Polling Interval | 10 seconds |
| Config Check Interval | 60 seconds |
| Theft Check Interval | Continuous |
| GPS Retry Attempts | 3 |

### Server Capacity

| Metric | Value |
|--------|-------|
| Max GPS History | 100 entries |
| Supported Protocols | HTTP, WebSocket |
| Ports | 3000 (main), 80 (alternative) |

### Dashboard Features

The dashboard provides:
- Real-time location tracking
- Historical path visualization
- Theft simulation
- Remote configuration
- Alert notifications
- Battery and sensor status

## Installation and Setup

### Hardware Setup

1. Connect the SIM7600 module to the ESP32 according to the pin configuration
2. Attach the limit switch to GPIO 34
3. Insert a SIM card with data capabilities
4. Power the device using a suitable power source

### Server Setup

```bash
# Clone the repository
git clone https://github.com/username/gps-front-end.git

# Navigate to the project directory
cd gps-front-end

# Install dependencies
npm install

# Start the server
npm start
```

### ESP32 Firmware Installation

1. Install Arduino IDE or PlatformIO
2. Install required libraries:
   - TinyGSM
   - ArduinoHttpClient
   - TinyGPSPlus
3. Open the `arduino/llytgo.ino` file
4. Configure the server address and credentials
5. Upload the firmware to your ESP32

## Future Improvements

1. **Enhanced Security**
   - Implement HTTPS communication
   - Add authentication for dashboard access
   - Encrypt sensitive configuration data

2. **Extended Features**
   - Geofencing capabilities
   - Battery level monitoring
   - Motion detection alerts
   - Historical route analysis

3. **Optimizations**
   - Reduce power consumption for longer battery life
   - Improve GPS acquisition time
   - Implement more robust error handling and recovery

---

This project was developed as an academic demonstration of IoT applications in vehicle security and tracking. The implementation showcases integration of hardware sensors, cellular communication, web technologies, and real-time data processing in a practical application.
