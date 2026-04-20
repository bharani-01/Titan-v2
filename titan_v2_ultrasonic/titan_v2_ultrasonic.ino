/*
 * ╔══════════════════════════════════════════════════════════╗
 *  TITAN-V2 — Sensor Node (ESP8266 NodeMCU)
 *  4× HC-SR04 Ultrasonic  +  MPU-6050 IMU
 *  NO extra sensor libraries — uses Wire.h + pulseIn()
 *
 *  LIBRARY NEEDED (only 1):
 *    WebSockets by Markus Sattler  →  Library Manager
 *
 *  Static IP : 192.168.137.60
 *  WS Port   : 81
 *  JSON out  :
 *    {"F":45.2,"B":120.5,"L":30.1,"R":80.3,
 *     "pitch":3.2,"roll":1.8,"temp":25.5}
 *
 *  PIN WIRING (NodeMCU):
 *  ┌──────────┬────────────┬────────────┐
 *  │ Sensor   │ TRIG/SDA   │ ECHO/SCL   │
 *  ├──────────┼────────────┼────────────┤
 *  │ FRONT    │ D5=GPIO14  │ D6=GPIO12  │
 *  │ BACK     │ D7=GPIO13  │ D8=GPIO15  │
 *  │ LEFT     │ D3=GPIO0   │ D4=GPIO2   │ ← moved to free I2C pins
 *  │ RIGHT    │ D0=GPIO16  │ D9=GPIO3*  │ * RX pin used as fallback
 *  │ MPU-6050 │ D2=GPIO4   │ D1=GPIO5   │ SDA / SCL (hardware I2C)
 *  └──────────┴────────────┴────────────┘
 *
 *  MPU-6050 wiring:
 *    VCC → 3.3V     GND → GND
 *    SDA → D2       SCL → D1
 *    AD0 → GND  (I2C address 0x68)
 *    INT → not connected
 *
 *  ⚠ Unplug D3 and D4 sensors only during flashing (boot pins)
 * ╚══════════════════════════════════════════════════════════╝
 */

#include <ESP8266WiFi.h>
#include <WebSocketsServer.h>   // WebSockets by Markus Sattler
#include <Wire.h>               // Built-in — no install needed

// ===== WiFi =====
const char* ssid     = "Nothing_2a";
const char* password = "12345678";

// ===== Static IP =====
IPAddress local_IP(192, 168, 137, 60);
IPAddress gateway  (192, 168, 137,  1);
IPAddress subnet   (255, 255, 255,  0);

// ===== WebSocket =====
WebSocketsServer webSocket(81);
uint8_t connectedClients = 0;

// ===== Ultrasonic Pins =====
#define TRIG_F 14   // D5
#define ECHO_F 12   // D6
#define TRIG_B 13   // D7
#define ECHO_B 15   // D8
#define TRIG_L  0   // D3  ← moved here to free I2C pins D1/D2
#define ECHO_L  2   // D4
#define TRIG_R 16   // D0
#define ECHO_R  3   // RX  (connect a 1kΩ series resistor on ECHO)

#define MAX_DIST_CM  350
#define WARN_CM       60
#define CRIT_CM       25
#define TRIG_US       10

// ===== MPU-6050 (raw I2C — no library) =====
#define MPU_ADDR    0x68    // AD0 = GND
#define MPU_SDA      4      // D2 = GPIO4
#define MPU_SCL      5      // D1 = GPIO5

// Calibration offsets (run calibration once and paste values here)
float accelOffX = 0, accelOffY = 0, accelOffZ = 0;
float gyroOffX  = 0, gyroOffY  = 0, gyroOffZ  = 0;

bool mpuOk = false;

// IMU state
float pitch = 0, roll = 0, mpuTemp = 0;

// Low-pass filter coeff (0=no filter, 0.9=heavy smooth)
#define LPF_ALPHA  0.8f

// ===== Timers =====
unsigned long lastSend   = 0;
unsigned long lastStatus = 0;
unsigned long bootTime   = 0;
const long    sendEvery  = 100;
const long    statusEvery= 5000;

// ─── Utility ───────────────────────────────────────────────

void printDivider() {
  Serial.println(F("─────────────────────────────────────────"));
}

void printBar(float cm) {
  int filled = (int)map((long)cm, 0, MAX_DIST_CM, 20, 0);
  filled = constrain(filled, 0, 20);
  Serial.print(F(" ["));
  for (int i = 0; i < 20; i++) Serial.print(i < filled ? '#' : '-');
  Serial.print(F("]"));
}

const char* classify(float cm) {
  if (cm < CRIT_CM) return "!! CRITICAL";
  if (cm < WARN_CM) return "!  WARNING ";
  return "   OK      ";
}

// ─── Ultrasonic ────────────────────────────────────────────

float readCm(uint8_t trigPin, uint8_t echoPin) {
  digitalWrite(trigPin, LOW);  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH); delayMicroseconds(TRIG_US);
  digitalWrite(trigPin, LOW);
  unsigned long dur = pulseIn(echoPin, HIGH,
                       (unsigned long)(MAX_DIST_CM * 2 * 29.15));
  if (dur == 0) return (float)MAX_DIST_CM;
  return min((float)dur / 58.0f, (float)MAX_DIST_CM);
}

// ─── MPU-6050 raw I2C helpers ──────────────────────────────

void mpuWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg); Wire.write(val);
  Wire.endTransmission();
}

int16_t mpuRead16(uint8_t reg) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, 2);
  return (Wire.read() << 8) | Wire.read();
}

bool mpuInit() {
  Wire.begin(MPU_SDA, MPU_SCL);
  delay(100);

  // Check WHO_AM_I register (should return 0x68)
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x75);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, 1);
  uint8_t who = Wire.read();

  if (who != 0x68) {
    Serial.printf("[MPU] WHO_AM_I=0x%02X (expected 0x68) — not found!\n", who);
    return false;
  }

  mpuWrite(0x6B, 0x00);   // PWR_MGMT_1: wake up, use internal 8MHz osc
  delay(50);
  mpuWrite(0x1B, 0x08);   // GYRO_CONFIG:  ±500 °/s
  mpuWrite(0x1C, 0x00);   // ACCEL_CONFIG: ±2 g
  mpuWrite(0x1A, 0x03);   // CONFIG: DLPF ~44Hz

  Serial.println(F("[MPU] MPU-6050 initialised OK"));
  Serial.println(F("[MPU] Range: Accel ±2g | Gyro ±500°/s"));
  return true;
}

// Run a quick calibration (rover must be flat and still)
void mpuCalibrate(int samples = 200) {
  Serial.printf("[MPU] Calibrating (%d samples — keep rover flat & still)...\n", samples);
  double ax=0,ay=0,az=0,gx=0,gy=0,gz=0;
  for (int i = 0; i < samples; i++) {
    ax += mpuRead16(0x3B) / 16384.0;
    ay += mpuRead16(0x3D) / 16384.0;
    az += mpuRead16(0x3F) / 16384.0;
    gx += mpuRead16(0x43) / 65.5;
    gy += mpuRead16(0x45) / 65.5;
    gz += mpuRead16(0x47) / 65.5;
    delay(5);
  }
  accelOffX = ax/samples;  accelOffY = ay/samples;  accelOffZ = az/samples - 1.0f;
  gyroOffX  = gx/samples;  gyroOffY  = gy/samples;  gyroOffZ  = gz/samples;
  Serial.printf("[MPU] Offsets → AccX:%.3f AccY:%.3f AccZ:%.3f\n",
                accelOffX, accelOffY, accelOffZ);
  Serial.printf("[MPU]          → GyrX:%.3f GyrY:%.3f GyrZ:%.3f\n",
                gyroOffX,  gyroOffY,  gyroOffZ);
}

// Read pitch/roll from accelerometer + temperature
void mpuUpdate() {
  if (!mpuOk) return;

  float ax = mpuRead16(0x3B) / 16384.0f - accelOffX;
  float ay = mpuRead16(0x3D) / 16384.0f - accelOffY;
  float az = mpuRead16(0x3F) / 16384.0f - accelOffZ;
  int16_t rawT = mpuRead16(0x41);

  // Pitch & roll from accel (degrees)
  float rawPitch = atan2(ax, sqrt(ay*ay + az*az)) * 180.0f / PI;
  float rawRoll  = atan2(ay, sqrt(ax*ax + az*az)) * 180.0f / PI;

  // Low-pass filter to smooth jitter
  pitch = LPF_ALPHA * pitch + (1.0f - LPF_ALPHA) * rawPitch;
  roll  = LPF_ALPHA * roll  + (1.0f - LPF_ALPHA) * rawRoll;

  // Temperature (°C)
  mpuTemp = (rawT / 340.0f) + 36.53f;
}

// ─── WebSocket Events ──────────────────────────────────────

void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t len) {
  switch (type) {
    case WStype_CONNECTED: {
      IPAddress ip = webSocket.remoteIP(num);
      connectedClients++;
      printDivider();
      Serial.printf("[WS]  Client #%u CONNECTED  IP: %s\n", num, ip.toString().c_str());
      Serial.printf("[WS]  Clients active: %u\n", connectedClients);
      printDivider();
      break;
    }
    case WStype_DISCONNECTED:
      if (connectedClients > 0) connectedClients--;
      Serial.printf("[WS]  Client #%u disconnected — %u remaining\n",
                    num, connectedClients);
      break;
    default: break;
  }
}

// ─── Setup ─────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(200);

  printDivider();
  Serial.println(F("  TITAN-V2  SENSOR NODE  v2.0"));
  Serial.println(F("  ESP8266  |  4x HC-SR04  |  MPU-6050"));
  printDivider();

  // Ultrasonic pins
  uint8_t trigs[] = {TRIG_F, TRIG_B, TRIG_L, TRIG_R};
  uint8_t echos[] = {ECHO_F, ECHO_B, ECHO_L, ECHO_R};
  const char* names[] = {"FRONT", "BACK ", "LEFT ", "RIGHT"};
  Serial.println(F("[INIT] Ultrasonic pins:"));
  for (int i = 0; i < 4; i++) {
    pinMode(trigs[i], OUTPUT); digitalWrite(trigs[i], LOW);
    pinMode(echos[i], INPUT);
    Serial.printf("       %s  TRIG=GPIO%d  ECHO=GPIO%d\n",
                  names[i], trigs[i], echos[i]);
  }

  // MPU-6050 init
  Serial.println(F("[INIT] MPU-6050..."));
  mpuOk = mpuInit();
  if (mpuOk) mpuCalibrate(200);
  else        Serial.println(F("[MPU]  SKIPPED — check wiring & AD0=GND"));

  // WiFi
  Serial.printf("\n[WIFI] SSID: %s\n", ssid);
  Serial.print(F("[WIFI] Connecting "));
  WiFi.config(local_IP, gateway, subnet);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print('.'); }
  printDivider();
  Serial.printf("[WIFI] CONNECTED  IP: %s  RSSI: %d dBm\n",
                WiFi.localIP().toString().c_str(), WiFi.RSSI());
  printDivider();

  // WebSocket
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println(F("[WS]  ws://192.168.137.60:81  ready"));
  printDivider();

  bootTime = millis();
}

// ─── Loop ──────────────────────────────────────────────────

void loop() {
  webSocket.loop();
  unsigned long now = millis();

  // Sensor broadcast
  if (now - lastSend >= sendEvery) {
    lastSend = now;

    float f = readCm(TRIG_F, ECHO_F);
    float b = readCm(TRIG_B, ECHO_B);
    float l = readCm(TRIG_L, ECHO_L);
    float r = readCm(TRIG_R, ECHO_R);
    mpuUpdate();

    // JSON packet
    char buf[128];
    snprintf(buf, sizeof(buf),
      "{\"F\":%.1f,\"B\":%.1f,\"L\":%.1f,\"R\":%.1f"
      ",\"pitch\":%.2f,\"roll\":%.2f,\"temp\":%.1f}",
      f, b, l, r, pitch, roll, mpuTemp);
    webSocket.broadcastTXT(buf);

    // Serial log
    Serial.printf("F:%5.1f", f); printBar(f); Serial.printf(" %s\n", classify(f));
    Serial.printf("B:%5.1f", b); printBar(b); Serial.printf(" %s\n", classify(b));
    Serial.printf("L:%5.1f", l); printBar(l); Serial.printf(" %s\n", classify(l));
    Serial.printf("R:%5.1f", r); printBar(r); Serial.printf(" %s\n", classify(r));
    if (mpuOk) {
      Serial.printf("IMU  Pitch:%+6.1f°  Roll:%+6.1f°  Temp:%.1f°C\n",
                    pitch, roll, mpuTemp);
    }
    Serial.println();
  }

  // Status summary every 5s
  if (now - lastStatus >= statusEvery) {
    lastStatus = now;
    unsigned long up = (now - bootTime) / 1000;
    printDivider();
    Serial.printf("[STATUS] Uptime  : %02lu:%02lu:%02lu\n",
                  up/3600, (up%3600)/60, up%60);
    Serial.printf("[STATUS] WiFi    : %s  %d dBm\n",
                  WiFi.status()==WL_CONNECTED?"ONLINE":"LOST", WiFi.RSSI());
    Serial.printf("[STATUS] Clients : %u\n", connectedClients);
    Serial.printf("[STATUS] MPU-6050: %s\n", mpuOk ? "OK" : "NOT FOUND");
    if (mpuOk)
      Serial.printf("[STATUS] IMU     : Pitch=%+.1f° Roll=%+.1f° T=%.1f°C\n",
                    pitch, roll, mpuTemp);
    Serial.printf("[STATUS] Heap    : %u bytes free\n", ESP.getFreeHeap());
    printDivider();
  }
}
