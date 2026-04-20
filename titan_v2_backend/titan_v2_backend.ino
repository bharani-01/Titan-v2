#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ESP32Servo.h>   // Install via Arduino Library Manager: "ESP32Servo"

// ===== WIFI =====
const char* ssid     = "Nothing_2a";
const char* password = "12345678";

// ===== SERVERS =====
WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// ══════════════════════════════════════════════
//  MOTOR PINS  (L298N)
// ══════════════════════════════════════════════
#define IN1 27
#define IN2 26
#define IN3 25
#define IN4 33
#define ENA 14
#define ENB 12

#define PWM_FREQ 1000
#define PWM_RES  8

// ══════════════════════════════════════════════
//  SERVO PINS  (6-DOF Robotic Arm)
//  Use pins that are NOT input-only on ESP32
// ══════════════════════════════════════════════
#define SERVO_BASE      2    // Base rotation  (0° – 180°)
#define SERVO_SHOULDER  4    // Shoulder joint  (0° – 180°)
#define SERVO_ARM      15    // Upper arm       (0° – 180°)
#define SERVO_JOINT1   13    // Wrist joint 1   (0° – 180°)
#define SERVO_JOINT2   16    // Wrist joint 2   (0° – 180°)
#define SERVO_GRIPPER  17    // Gripper         (0°=open, 90°=closed)

// Home / safe position angles
#define HOME_BASE     90
#define HOME_SHOULDER 90
#define HOME_ARM      90
#define HOME_J1       90
#define HOME_J2       90
#define HOME_GRIP      0    // 0 = fully open

Servo servoBase, servoShoulder, servoArm, servoJ1, servoJ2, servoGripper;

// Current angles (for smooth motion & feedback)
int angleBase     = HOME_BASE;
int angleShoulder = HOME_SHOULDER;
int angleArm      = HOME_ARM;
int angleJ1       = HOME_J1;
int angleJ2       = HOME_J2;
int angleGrip     = HOME_GRIP;

// ══════════════════════════════════════════════
//  SERVO SETUP
// ══════════════════════════════════════════════
void setupServos() {
  ESP32PWM::allocateTimer(2);   // Timers 0 & 1 used by motors; 2 & 3 free
  ESP32PWM::allocateTimer(3);

  servoBase.setPeriodHertz(50);
  servoShoulder.setPeriodHertz(50);
  servoArm.setPeriodHertz(50);
  servoJ1.setPeriodHertz(50);
  servoJ2.setPeriodHertz(50);
  servoGripper.setPeriodHertz(50);

  servoBase.attach(SERVO_BASE,       500, 2400);
  servoShoulder.attach(SERVO_SHOULDER, 500, 2400);
  servoArm.attach(SERVO_ARM,         500, 2400);
  servoJ1.attach(SERVO_JOINT1,       500, 2400);
  servoJ2.attach(SERVO_JOINT2,       500, 2400);
  servoGripper.attach(SERVO_GRIPPER, 500, 2400);

  // Move all to home position on boot
  armHome();
  Serial.println("Servos initialised — ARM at HOME");
}

// ══════════════════════════════════════════════
//  ARM COMMANDS
// ══════════════════════════════════════════════
void armHome() {
  servoBase.write(HOME_BASE);
  servoShoulder.write(HOME_SHOULDER);
  servoArm.write(HOME_ARM);
  servoJ1.write(HOME_J1);
  servoJ2.write(HOME_J2);
  servoGripper.write(HOME_GRIP);
  angleBase = HOME_BASE;  angleShoulder = HOME_SHOULDER;
  angleArm  = HOME_ARM;   angleJ1 = HOME_J1;
  angleJ2   = HOME_J2;    angleGrip = HOME_GRIP;
}

void setServoAngle(Servo &sv, int &current, int angle) {
  angle = constrain(angle, 0, 180);
  sv.write(angle);
  current = angle;
}

// Parse: ARM:BASE:90  /  ARM:SHOULDER:45  /  ARM:GRIP:OPEN  etc.
void handleArmCommand(String cmd) {
  // cmd comes in as "ARM:JOINT:VALUE"
  int first  = cmd.indexOf(':');
  int second = cmd.lastIndexOf(':');
  if (first < 0 || second <= first) return;

  String joint = cmd.substring(first + 1, second);
  String val   = cmd.substring(second + 1);
  joint.toUpperCase();

  // --- Special presets ---
  if (joint == "HOME")  { armHome(); return; }
  if (joint == "GRAB")  {
    servoGripper.write(90);  angleGrip = 90; return;
  }
  if (joint == "OPEN") {
    servoGripper.write(0);   angleGrip = 0;  return;
  }

  // --- Gripper by name ---
  if (joint == "GRIP" || joint == "GRIPPER") {
    if (val == "OPEN")  { setServoAngle(servoGripper, angleGrip, 0);  return; }
    if (val == "CLOSE") { setServoAngle(servoGripper, angleGrip, 90); return; }
    setServoAngle(servoGripper, angleGrip, val.toInt()); return;
  }

  int angle = val.toInt();

  if      (joint == "BASE")     setServoAngle(servoBase,     angleBase,     angle);
  else if (joint == "SHOULDER") setServoAngle(servoShoulder, angleShoulder, angle);
  else if (joint == "ARM")      setServoAngle(servoArm,      angleArm,      angle);
  else if (joint == "J1" || joint == "JOINT1") setServoAngle(servoJ1, angleJ1, angle);
  else if (joint == "J2" || joint == "JOINT2") setServoAngle(servoJ2, angleJ2, angle);

  Serial.printf("ARM %s → %d°\n", joint.c_str(), angle);
}

// ══════════════════════════════════════════════
//  MOTOR SETUP
// ══════════════════════════════════════════════
void setupMotors() {
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  ledcAttach(ENA, PWM_FREQ, PWM_RES);
  ledcAttach(ENB, PWM_FREQ, PWM_RES);

  ledcWrite(ENA, 200);
  ledcWrite(ENB, 200);
}

// ══════════════════════════════════════════════
//  MOTOR FUNCTIONS
// ══════════════════════════════════════════════
void stopMotors() {
  digitalWrite(IN1, LOW); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW); digitalWrite(IN4, LOW);
}
void forward()  { digitalWrite(IN1,HIGH); digitalWrite(IN2,LOW);  digitalWrite(IN3,HIGH); digitalWrite(IN4,LOW);  }
void backward() { digitalWrite(IN1,LOW);  digitalWrite(IN2,HIGH); digitalWrite(IN3,LOW);  digitalWrite(IN4,HIGH); }
void left()     { digitalWrite(IN1,LOW);  digitalWrite(IN2,HIGH); digitalWrite(IN3,HIGH); digitalWrite(IN4,LOW);  }
void right()    { digitalWrite(IN1,HIGH); digitalWrite(IN2,LOW);  digitalWrite(IN3,LOW);  digitalWrite(IN4,HIGH); }

// ══════════════════════════════════════════════
//  MAIN COMMAND DISPATCHER
// ══════════════════════════════════════════════
void handleCommand(String cmd) {
  cmd.trim();

  // Motor drive commands (single char)
  if (cmd == "F") { forward();    return; }
  if (cmd == "B") { backward();   return; }
  if (cmd == "L") { left();       return; }
  if (cmd == "R") { right();      return; }
  if (cmd == "S") { stopMotors(); return; }

  // Arm commands (ARM:JOINT:ANGLE)
  if (cmd.startsWith("ARM:")) { handleArmCommand(cmd); return; }

  Serial.println("Unknown command: " + cmd);
}

// ══════════════════════════════════════════════
//  WEBSOCKET HANDLER
// ══════════════════════════════════════════════
void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      Serial.printf("[WS] Client #%u connected\n", num);
      break;
    case WStype_DISCONNECTED:
      Serial.printf("[WS] Client #%u disconnected\n", num);
      stopMotors();   // Safety: stop if dashboard disconnects
      break;
    case WStype_TEXT: {
      String cmd = String((char*)payload);
      Serial.println("[CMD] " + cmd);
      handleCommand(cmd);
      break;
    }
    default: break;
  }
}

// ══════════════════════════════════════════════
//  SIMPLE WEB PAGE (fallback)
// ══════════════════════════════════════════════
const char* html = R"rawliteral(
<!DOCTYPE html><html><head><title>TITAN-V2 Rover</title></head><body>
<h2>TITAN-V2 Drive</h2>
<button onclick="ws.send('F')">Forward</button><br><br>
<button onclick="ws.send('L')">Left</button>
<button onclick="ws.send('S')">Stop</button>
<button onclick="ws.send('R')">Right</button><br><br>
<button onclick="ws.send('B')">Backward</button>
<hr>
<h3>ARM</h3>
<button onclick="ws.send('ARM:HOME')">HOME</button>
<button onclick="ws.send('ARM:GRIP:OPEN')">OPEN</button>
<button onclick="ws.send('ARM:GRIP:CLOSE')">CLOSE</button>
<script>
var ws = new WebSocket("ws://" + location.hostname + ":81/");
</script></body></html>
)rawliteral";

void handleRoot() { server.send(200, "text/html", html); }

// ══════════════════════════════════════════════
//  SETUP
// ══════════════════════════════════════════════
void setup() {
  Serial.begin(115200);

  setupMotors();
  stopMotors();   // Safety

  setupServos();  // Init arm at home position

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print('.'); }
  Serial.println("\nConnected!");
  Serial.print("IP: "); Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.begin();

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  Serial.println("TITAN-V2 Backend Ready — Motors + 6-DOF Arm");
}

// ══════════════════════════════════════════════
//  LOOP
// ══════════════════════════════════════════════
void loop() {
  server.handleClient();
  webSocket.loop();
}
