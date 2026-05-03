
#include <WiFi.h>
#include <WebServer.h>
#include <time.h>
#include <MD_Parola.h>
#include <MD_MAX72XX.h>
#include <SPI.h>
#include <arduinoFFT.h>

const char* ssid = "...";
const char* password = "...";

WebServer server(80);

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4
#define DATA_PIN 6
#define CLK_PIN  4
#define CS_PIN   7
#define BUZZER_PIN 1
#define BUTTON_PIN 3

MD_Parola display = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

#define BUTTON_PIN 3
int mode = 0;
bool lastState = HIGH;

String message = "";

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7 * 3600;

#define SAMPLES 64
#define SAMPLING_FREQUENCY 4000
ArduinoFFT<double> FFT = ArduinoFFT<double>();

double realComponent[SAMPLES];
double imagComponent[SAMPLES];

#define MIC_PIN 0
int scrollSpeed = 50; 

byte spectralHeight[] = {0,128,192,224,240,248,252,254,255};

// ================= HTML =================
String webpage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta charset="UTF-8">
<style>
body { 
  font-family: Arial; 
  text-align: center; 
  margin-top: 30px;
}
button { 
  padding: 15px; 
  margin: 5px; 
  font-size: 18px; 
}
input { 
  padding: 10px; 
  font-size: 16px; 
}
.slider {
  width: 80%;
}
</style>
</head>
<body>
<h1>Nhóm 2: Lê Quốc Toàn và Trần Tuấn<h1>
<h2>Điều khiển LED Ma trận</h2>

<!-- MODE -->
<button onclick="setMode(0)">Đồng hồ</button>
<button onclick="setMode(1)">Chạy chữ</button>
<button onclick="setMode(2)">Nhạc</button>

<br><br>

<!-- TEXT -->
<input id="msg" placeholder="Nhập nội dung">
<button onclick="sendText()">Gửi</button>

<br><br>

<!-- SPEED -->
<label>Tốc độ: <span id="speedVal">50</span></label><br>
<input class="slider" type="range" min="10" max="200" value="50"
oninput="updateSpeed(this.value)"
onchange="setSpeed(this.value)">

<br><br>

<!-- BRIGHTNESS -->
<label>Độ sáng: <span id="brightVal">5</span></label><br>
<input class="slider" type="range" min="0" max="15" value="5"
oninput="updateBright(this.value)"
onchange="setBrightness(this.value)">

<script>

// ===== MODE =====
function setMode(m) {
  fetch("/mode?m=" + m);
}

// ===== TEXT =====
function sendText() {
  let t = document.getElementById("msg").value;
  fetch("/text?msg=" + t);
}

// ===== SPEED (đã đảo chiều) =====
function setSpeed(s) {
  let reversed = 210 - s;
  fetch("/speed?s=" + reversed);
}
function updateSpeed(v){
  document.getElementById("speedVal").innerText = v;
}

// ===== BRIGHTNESS =====
function setBrightness(b) {
  fetch("/bright?b=" + b);
}
function updateBright(v){
  document.getElementById("brightVal").innerText = v;
}

</script>

</body>
</html>
)rawliteral";

void setup()
{
  Serial.begin(115200);

  display.begin();
  display.setIntensity(5);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  display.displayText("Dang ket noi WiFi", PA_LEFT, 50, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);

 WiFi.begin(ssid, password);

while (WiFi.status() != WL_CONNECTED)
{
  display.displayAnimate();
}

display.displayClear();
  message = WiFi.localIP().toString();
  Serial.println(message);
  configTime(gmtOffset_sec, 0, ntpServer);

  server.on("/", [](){
    server.send(200, "text/html", webpage);
  });

  server.on("/mode", [](){
    mode = server.arg("m").toInt();
    display.displayClear();
    server.send(200, "text/plain", "OK");
  });

  server.on("/text", [](){
    message = server.arg("msg");
    server.send(200, "text/plain", "OK");
  });

  server.on("/bright", [](){
    int b = server.arg("b").toInt();
    display.setIntensity(b);
    server.send(200, "text/plain", "OK");
  });

  server.begin();
  server.on("/speed", [](){
  scrollSpeed = server.arg("s").toInt();
  server.send(200, "text/plain", "OK");
  });
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW); 
  }

void loop()
{
  server.handleClient();
  handleButton();

  switch (mode)
  {
    case 0: runClock(); break;
    case 1: runText(); break;
    case 2: runMusic(); break;
  }
}

void handleButton()
{
  bool current = digitalRead(BUTTON_PIN);

  if (lastState == HIGH && current == LOW)
  {
    mode++;
    if (mode > 2) mode = 0;
    beep();
    display.displayClear();
    delay(200);
  }

  lastState = current;
}
void beep()
{
  digitalWrite(BUZZER_PIN, HIGH);
  delay(80);
  digitalWrite(BUZZER_PIN, LOW);
}

void runClock()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;

  char timeStr[6];
  strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);

   static bool blink = true;
  blink = !blink;

  if (!blink) {
    timeStr[2] = ' ';  
  }

  display.displayClear();
  display.setTextAlignment(PA_CENTER);
  display.print(timeStr);  

  delay(500); 
}


void runText()
{
  if (display.displayAnimate())
  {
    display.displayText(message.c_str(), PA_LEFT, scrollSpeed, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
  }
}

void runMusic()
{
  int sensitivity = 50;

  for (int i = 0; i < SAMPLES; i++)
  {
    realComponent[i] = analogRead(MIC_PIN) / sensitivity;
    imagComponent[i] = 0;

    delayMicroseconds(1000000 / SAMPLING_FREQUENCY);
  }

  FFT.windowing(realComponent, SAMPLES, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  FFT.compute(realComponent, imagComponent, SAMPLES, FFT_FORWARD);
  FFT.complexToMagnitude(realComponent, imagComponent, SAMPLES);


  display.displayClear();

  for (int i = 2; i < 34; i++)
  {
    double val = realComponent[i];

    val = constrain(val, 0, 200);
    int level = map(val, 0, 200, 0, 8);

    int col = 33 - i;
    display.getGraphicObject()->setColumn(col, spectralHeight[level]);
  }

  display.displayAnimate();
}