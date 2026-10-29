/*
  4WD Robot Car — ESP32 + L298N + HC-SR04
  ----------------------------------------
  Pins:
    IN1 18, IN2 19   -> Left motor direction
    IN3 21, IN4 22   -> Right motor direction
    ENA_PIN 5        -> Left motor PWM (speed)
    ENB_PIN 23       -> Right motor PWM (speed)
    TRIG_PIN 25, ECHO_PIN 13 -> HC-SR04 ultrasonic sensor
    BUZZER_PIN 4
    MQ-2 34, MQ-4 35, MQ-7 32 (analog out), DHT11 26
    Sensor data is sent to ThingsBoard over MQTT.

  Behavior:
    - The car ONLY moves when you command it (forward/back/left/right)
      from the phone page or Serial Monitor. There is no automatic
      forward driving.
    - Safety feature (crash prevention): while driving FORWARD, if the
      ultrasonic sensor sees a wall closer than SAFE_DISTANCE_CM, the
      car automatically backs away a short distance and stops, taking
      over from whatever you were pressing. You get control back
      immediately after.
    - This safety feature has a software ON/OFF switch on the phone
      page. When OFF, nothing overrides your input — full manual,
      crash risk is on you.
    - Turning left/right and reversing are never blocked by safety,
      since those move the car away from an obstacle, not into it.

  Connect: the ESP32 joins your home WiFi network. Open Serial Monitor
  at boot to see the IP address it was assigned, then open that
  address in your phone's browser (phone must be on the same WiFi).
*/

#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>   // MQTT (for ThingsBoard)
#include <DHT.h>            // Adafruit "DHT sensor library"

// ---------- Home WiFi settings ----------
const char* WIFI_SSID     = "TS POLICE";
const char* WIFI_PASSWORD = "THIRUPATHI";

// ---------- ThingsBoard settings (FILL THESE IN) ----------
const char* TB_SERVER = "thingsboard.cloud";       // or your own server host/IP
const int   TB_PORT   = 1883;
const char* TB_TOKEN  = "ZBNtnsCDzkxZ1qou4sId"; // Device -> Copy access token

WebServer server(80);
WiFiClient espClient;
PubSubClient mqtt(espClient);

// ---------- Pin definitions ----------
#define IN1 18   // Left motor
#define IN2 19
#define IN3 21   // Right motor
#define IN4 22
#define ENA_PIN 5
#define ENB_PIN 23
#define TRIG_PIN 25
#define ECHO_PIN 13
#define BUZZER_PIN 4

// Gas / environment sensors
#define MQ2_PIN  34   // analog out
#define MQ4_PIN  35   // analog out
#define MQ7_PIN  32   // analog out
#define DHT_PIN  26
#define DHT_TYPE DHT11

DHT dht(DHT_PIN, DHT_TYPE);

// ---------- PWM (LEDC) config ----------
const int PWM_FREQ       = 5000;
const int PWM_RESOLUTION = 8; // 0-255

// ---------- Tunable parameters ----------
const int   SAFE_DISTANCE_CM   = 20;   // trigger distance for auto-back-off
const unsigned long ECHO_TIMEOUT_US    = 25000UL; // ~430 cm max range
const unsigned long SENSOR_INTERVAL_MS = 60;      // sensor poll rate
const unsigned long AUTO_BACK_MS       = 350;      // how long it backs away

// ---------- State ----------
int  currentSpeed  = 180;   // manual drive speed (0-255)
bool safetyEnabled = true;  // software switch, controlled from the web page

enum Direction { DIR_STOP, DIR_FORWARD, DIR_BACK, DIR_LEFT, DIR_RIGHT };
Direction currentDirection = DIR_STOP;

unsigned long lastSensorRead = 0;
long lastDistanceCm = -1;

// Non-blocking auto-back-off state (runs when safety triggers)
bool safetyActive = false;
unsigned long safetyStepStart = 0;

// ---------------- Motor primitives ----------------
void setSpeed(int leftPWM, int rightPWM) {
  leftPWM  = constrain(leftPWM, 0, 255);
  rightPWM = constrain(rightPWM, 0, 255);
  ledcWrite(ENA_PIN, leftPWM);
  ledcWrite(ENB_PIN, rightPWM);
}

void motorsForward() {
  digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
}
void motorsBackward() {
  digitalWrite(IN1, LOW);  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW);  digitalWrite(IN4, HIGH);
}
void motorsTurnLeft() {
  digitalWrite(IN1, LOW);  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
}
void motorsTurnRight() {
  digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);  digitalWrite(IN4, HIGH);
}
void motorsStop() {
  digitalWrite(IN1, LOW); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW); digitalWrite(IN4, LOW);
  setSpeed(0, 0);
}

// ---------------- Ultrasonic sensor ----------------
long readDistanceCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long duration = pulseIn(ECHO_PIN, HIGH, ECHO_TIMEOUT_US);
  if (duration == 0) return -1; // no echo in range
  return duration * 0.0343 / 2.0;
}

// ---------------- Manual drive commands ----------------
// These are the ONLY things that move the car — no autonomous driving.
void driveForward() {
  currentDirection = DIR_FORWARD;
  setSpeed(currentSpeed, currentSpeed);
  motorsForward();
}
void driveBackward() {
  currentDirection = DIR_BACK;
  setSpeed(currentSpeed, currentSpeed);
  motorsBackward();
}
void driveLeft() {
  currentDirection = DIR_LEFT;
  setSpeed(currentSpeed, currentSpeed);
  motorsTurnLeft();
}
void driveRight() {
  currentDirection = DIR_RIGHT;
  setSpeed(currentSpeed, currentSpeed);
  motorsTurnRight();
}
void driveStop() {
  currentDirection = DIR_STOP;
  motorsStop();
}

// ---------------- Safety: auto-back-off from a wall ----------------
// Runs every loop. Only steps in when: safety is ON, the car is
// currently being driven FORWARD, and the wall is within range.
// It takes a brief, non-blocking control of the motors, then hands
// control straight back to you.
void handleSafety() {
  unsigned long now = millis();

  if (safetyActive) {
    // Currently backing off automatically
    if (now - safetyStepStart >= AUTO_BACK_MS) {
      motorsStop();
      currentDirection = DIR_STOP;
      safetyActive = false;
    }
    return; // ignore new forward commands until the back-off finishes
  }

  if (safetyEnabled &&
      currentDirection == DIR_FORWARD &&
      lastDistanceCm > 0 &&
      lastDistanceCm < SAFE_DISTANCE_CM) {
    // Wall incoming — take over, back away
    safetyActive = true;
    safetyStepStart = now;
    setSpeed(currentSpeed, currentSpeed);
    motorsBackward();
  }
}

// ---------------- Buzzer: beep when inside safety distance ----------------
// Non-blocking. Beeps faster the closer the obstacle is.
// Works regardless of the safety ON/OFF switch (it's a warning only).
unsigned long lastBeepToggle = 0;
bool buzzerState = false;

void handleBuzzer() {
  unsigned long now = millis();

  if (lastDistanceCm > 0 && lastDistanceCm < SAFE_DISTANCE_CM) {
    // closer = faster beeping (60 ms to 300 ms per toggle)
    unsigned long interval = map(lastDistanceCm, 0, SAFE_DISTANCE_CM, 60, 300);
    if (now - lastBeepToggle >= interval) {
      lastBeepToggle = now;
      buzzerState = !buzzerState;
      digitalWrite(BUZZER_PIN, buzzerState);
    }
  } else {
    buzzerState = false;
    digitalWrite(BUZZER_PIN, LOW);
  }
}

// ---------------- ThingsBoard telemetry ----------------
// Non-blocking-ish: sensors are read every 5 s and published over MQTT.
// MQTT reconnect is only attempted every 10 s so driving is not affected.
const unsigned long TELEMETRY_INTERVAL_MS = 5000;
const unsigned long MQTT_RETRY_MS         = 10000;
unsigned long lastTelemetry = 0;
unsigned long lastMqttTry   = 0;

void handleThingsBoard() {
  unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED) return;

  if (!mqtt.connected()) {
    if (now - lastMqttTry >= MQTT_RETRY_MS) {
      lastMqttTry = now;
      // username = device access token, no password
      if (mqtt.connect("RobotCarESP32", TB_TOKEN, NULL)) {
        Serial.println("ThingsBoard connected");
      } else {
        Serial.print("ThingsBoard connect failed, rc=");
        Serial.println(mqtt.state());
      }
    }
    return;
  }

  mqtt.loop();

  if (now - lastTelemetry >= TELEMETRY_INTERVAL_MS) {
    lastTelemetry = now;

    int mq2 = analogRead(MQ2_PIN);
    int mq4 = analogRead(MQ4_PIN);
    int mq7 = analogRead(MQ7_PIN);
    float t = dht.readTemperature();
    float h = dht.readHumidity();

    String payload = "{\"mq2\":" + String(mq2) +
                     ",\"mq4\":" + String(mq4) +
                     ",\"mq7\":" + String(mq7);
    if (!isnan(t)) payload += ",\"temperature\":" + String(t, 1);
    if (!isnan(h)) payload += ",\"humidity\":" + String(h, 1);
    payload += "}";

    mqtt.publish("v1/devices/me/telemetry", payload.c_str());
    Serial.println("TB <- " + payload);
  }
}

// ---------------- Shared command handling ----------------
// One function used by both Serial and the web page so behavior is
// always identical between the two.
void printStatus() {
  Serial.print("Safety: ");
  Serial.print(safetyEnabled ? "ON" : "OFF");
  Serial.print(" | Speed: ");
  Serial.print(currentSpeed);
  Serial.print(" | Distance: ");
  Serial.print(lastDistanceCm);
  Serial.println(" cm");
}

void executeCommand(char c) {
  // While the safety back-off is actively running, ignore new forward
  // commands so it can finish pulling away from the wall.
  if (safetyActive && c == 'f') return;

  switch (c) {
    case 'f': driveForward();  break;
    case 'b': driveBackward(); break;
    case 'l': driveLeft();     break;
    case 'r': driveRight();    break;
    case 's': driveStop();     break;
    case '+': currentSpeed = constrain(currentSpeed + 25, 0, 255); break;
    case '-': currentSpeed = constrain(currentSpeed - 25, 0, 255); break;
    case 'y': safetyEnabled = true;  Serial.println(">> Safety ON");  break;
    case 'n': safetyEnabled = false; Serial.println(">> Safety OFF — manual only, crash prevention disabled"); break;
    case 'p': printStatus(); break;
    default: break;
  }
}

void handleSerialCommands() {
  if (!Serial.available()) return;
  executeCommand(Serial.read());
}

// ---------------- Web / phone control page ----------------
const char CONTROL_PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Robot Car</title>
<style>
  body{font-family:sans-serif;text-align:center;background:#111;color:#eee;margin:0;padding:20px}
  h2{margin-bottom:4px}
  #status{font-size:14px;color:#aaa;margin-bottom:16px}
  .row{display:flex;justify-content:center;gap:10px;margin:8px 0}
  button{width:80px;height:60px;font-size:18px;border:none;border-radius:10px;background:#333;color:#fff}
  button:active{background:#555}
  #safetyBtn{width:220px;background:#0a0}
  #safetyBtn.off{background:#a00}
  input[type=range]{width:220px}
</style></head><body>
<h2>Robot Car</h2>
<div id="status">Loading...</div>

<div class="row"><button ontouchstart="cmd('f')" onmousedown="cmd('f')" ontouchend="cmd('s')" onmouseup="cmd('s')">Forward</button></div>
<div class="row">
  <button ontouchstart="cmd('l')" onmousedown="cmd('l')" ontouchend="cmd('s')" onmouseup="cmd('s')">Left</button>
  <button onclick="cmd('s')">Stop</button>
  <button ontouchstart="cmd('r')" onmousedown="cmd('r')" ontouchend="cmd('s')" onmouseup="cmd('s')">Right</button>
</div>
<div class="row"><button ontouchstart="cmd('b')" onmousedown="cmd('b')" ontouchend="cmd('s')" onmouseup="cmd('s')">Back</button></div>

<p>Speed: <span id="spdVal">180</span></p>
<input type="range" min="0" max="255" value="180" id="spd" oninput="setSpeed(this.value)">

<div class="row"><button id="safetyBtn" onclick="toggleSafety()">Safety: ON</button></div>

<script>
var safetyOn = true;
function cmd(c){ fetch('/cmd?c=' + c).then(r=>r.json()).then(updateStatus); }
function setSpeed(v){ document.getElementById('spdVal').innerText=v; fetch('/speed?v=' + v).then(r=>r.json()).then(updateStatus); }
function toggleSafety(){ safetyOn = !safetyOn; cmd(safetyOn ? 'y' : 'n'); }
function updateStatus(s){
  document.getElementById('status').innerText =
    'Safety: ' + s.safety + ' | Distance: ' + s.dist + ' cm';
  var btn = document.getElementById('safetyBtn');
  safetyOn = (s.safety === 'ON');
  if (safetyOn) { btn.innerText = 'Safety: ON'; btn.classList.remove('off'); }
  else { btn.innerText = 'Safety: OFF'; btn.classList.add('off'); }
}
setInterval(()=>{ fetch('/status').then(r=>r.json()).then(updateStatus); }, 800);
</script>
</body></html>
)HTML";

String statusJson() {
  String j = "{\"safety\":\"";
  j += safetyEnabled ? "ON" : "OFF";
  j += "\",\"speed\":";
  j += String(currentSpeed);
  j += ",\"dist\":";
  j += String(lastDistanceCm);
  j += "}";
  return j;
}

void handleRoot()   { server.send_P(200, "text/html", CONTROL_PAGE); }
void handleStatus() { server.send(200, "application/json", statusJson()); }

void handleCmd() {
  if (server.hasArg("c")) {
    executeCommand(server.arg("c").charAt(0));
  }
  server.send(200, "application/json", statusJson());
}

void handleSpeed() {
  if (server.hasArg("v")) {
    currentSpeed = constrain(server.arg("v").toInt(), 0, 255);
  }
  server.send(200, "application/json", statusJson());
}

// ---------------- Setup / Loop ----------------
void setup() {
  Serial.begin(115200);

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(MQ2_PIN, INPUT);
  pinMode(MQ4_PIN, INPUT);
  pinMode(MQ7_PIN, INPUT);
  dht.begin();

  ledcAttach(ENA_PIN, PWM_FREQ, PWM_RESOLUTION);
  ledcAttach(ENB_PIN, PWM_FREQ, PWM_RESOLUTION);

  motorsStop();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("Connected!");
  Serial.print("Open this address in your phone's browser: ");
  Serial.println(WiFi.localIP());

  server.on("/",      handleRoot);
  server.on("/status", handleStatus);
  server.on("/cmd",    handleCmd);
  server.on("/speed",  handleSpeed);
  server.begin();

  mqtt.setServer(TB_SERVER, TB_PORT);
  mqtt.setSocketTimeout(2);   // keep a failed connect from stalling the car

  Serial.println("Robot ready. Manual drive only.");
  Serial.println("f/b/l/r/s = move, +/- = speed, y/n = safety on/off, p = status");
}

void loop() {
  // 1. Poll the ultrasonic sensor
  unsigned long now = millis();
  if (now - lastSensorRead >= SENSOR_INTERVAL_MS) {
    lastSensorRead = now;
    long d = readDistanceCm();
    if (d > 0) lastDistanceCm = d;
  }

  // 2. Read your commands (Serial Monitor + phone page)
  handleSerialCommands();
  server.handleClient();

  // 3. Safety check — only acts if you're driving forward into a wall
  handleSafety();

  // 4. Buzzer warning
  handleBuzzer();

  // 5. Send gas + DHT11 data to ThingsBoard
  handleThingsBoard();
}
