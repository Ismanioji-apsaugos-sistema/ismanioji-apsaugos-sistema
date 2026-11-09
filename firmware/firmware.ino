/*
  Išmanioji apsaugos sistema
  RFID (PN532) + atstumo jutiklis (HC-SR04) + garsinis signalizatorius
  + 20x4 I2C LCD ekranas + žiniatinklio valdymo skydelis
 
  Plokštė:
    - Arduino Nano 33 IoT -> tikra įranga (WiFiNINA + PN532)
    - ESP32               -> Wokwi simuliacija (WiFi.h, mygtukas pakeičia RFID kortelę)
 
  Būsenos: UNARMED -> ARMED -> ALARM
    - Leistina kortelė: UNARMED -> ARMED, ARMED -> UNARMED, ALARM -> UNARMED
    - Žiniatinklis (su PIN): Arm / Disarm. ALARM būseną galima atšaukti tik kortele
      (nustačius WEB_CAN_RESET_ALARM = true, leidžiama ir per žiniatinklį).
*/
 
#include <Wire.h>                 // I2C magistralė (PN532 ir LCD)
#include <LiquidCrystal_I2C.h>    // 20x4 LCD per PCF8574 I2C adapterį
 
#if defined(ARDUINO_ARCH_ESP32)
  #define SIMULATION 1            // kompiliuojama Wokwi ESP32 simuliacijai
  #include <WiFi.h>
#else
  #define SIMULATION 0            // kompiliuojama Arduino Nano 33 IoT plokštei
  #include <WiFiNINA.h>           // NINA-W102 Wi-Fi modulio biblioteka
  #include <Adafruit_PN532.h>     // PN532 RFID skaitytuvo biblioteka
#endif
 
// ======================= NUSTATYMAI =======================
#if SIMULATION
const char WIFI_SSID[] = "Wokwi-GUEST";   // Wokwi virtualus tinklas
const char WIFI_PASS[] = "";
#else
const char WIFI_SSID[] = "YOUR_WIFI_NAME";       // įrašyti savo tinklo pavadinimą
const char WIFI_PASS[] = "YOUR_WIFI_PASSWORD";   // įrašyti savo tinklo slaptažodį
#endif
 
const char WEB_PIN[]                = "1234";  // PIN kodas nuotoliniam valdymui
const long ALARM_DISTANCE_CM        = 100;     // arčiau nei ši riba -> ALARM
const unsigned long EXIT_DELAY_MS   = 10000;   // laikas išeiti po įjungimo (10 s)
const bool WEB_CAN_RESET_ALARM      = false;   // ALARM atšaukiamas tik kortele
 
// Leistinų kortelių UID sąrašas. Kortelės UID nuskaitomas Serial Monitor lange.
struct CardUid { uint8_t len; uint8_t uid[7]; };
const CardUid AUTHORIZED_CARDS[] = {
  {4, {0xDE, 0xAD, 0xBE, 0xEF}},   // <- pakeisti savo kortelės UID
  {4, {0x12, 0x34, 0x56, 0x78}},   // <- pakeisti savo žetono UID
};
 
// ======================= IŠVADAI =======================
#if SIMULATION
const int PIN_TRIG = 5, PIN_ECHO = 18, PIN_BUZZER = 19, PIN_CARD_BTN = 4;
#else
const int PIN_TRIG = 2, PIN_ECHO = 3, PIN_BUZZER = 5;
const int PIN_PN532_IRQ = 6, PIN_PN532_RST = 7;
Adafruit_PN532 nfc(PIN_PN532_IRQ, PIN_PN532_RST);   // PN532 I2C režimu
bool nfcOk = false;                                  // ar skaitytuvas rastas
#endif
 
LiquidCrystal_I2C lcd(0x27, 20, 4);   // jei ekranas tuščias – bandyti 0x3F
WiFiServer server(80);                // HTTP serveris 80 prievade
 
// ======================= BŪSENŲ MAŠINA =======================
enum State { UNARMED, ARMED, ALARM };
State state = UNARMED;                // dabartinė sistemos būsena
unsigned long stateSince = 0;         // kada pereita į dabartinę būseną (ms)
long lastDistanceCm = -1;             // paskutinis išmatuotas atstumas
 
const char* stateName(State s) {      // būsenos pavadinimas tekstu (žiniatinkliui)
  switch (s) {
    case UNARMED: return "UNARMED";
    case ARMED:   return "ARMED";
    default:      return "ALARM";
  }
}
 
// ======================= ĮVYKIŲ ŽURNALAS (žiedinis buferis) =======================
const int LOG_SIZE = 10;              // saugomi 10 paskutinių įvykių
char logs[LOG_SIZE][48];
int logHead = 0, logCount = 0;
 
void addLog(const char* msg) {
  unsigned long s = millis() / 1000;  // veikimo laikas nuo paleidimo (nėra RTC)
  snprintf(logs[logHead], sizeof(logs[0]), "%02lu:%02lu:%02lu %s",
           s / 3600, (s / 60) % 60, s % 60, msg);
  Serial.println(logs[logHead]);      // dubliuojama į Serial Monitor
  logHead = (logHead + 1) % LOG_SIZE; // seniausias įrašas perrašomas
  if (logCount < LOG_SIZE) logCount++;
}
 
// ======================= LCD EKRANAS =======================
void drawLcd() {                      // pilnas ekrano perpiešimas pasikeitus būsenai
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(F("Security system"));
  lcd.setCursor(0, 1);
  if (state == UNARMED) lcd.print(F("System Unarmed"));
  if (state == ARMED)   lcd.print(F("System Armed"));
  if (state == ALARM)   lcd.print(F("ALARM!"));
  lcd.setCursor(0, 3);                // 4-oje eilutėje – IP adresas
  if (WiFi.status() == WL_CONNECTED) lcd.print(WiFi.localIP());
  else                               lcd.print(F("WiFi: offline"));
}
 
void updateLcd() {                    // kviečiama kiekviename cikle, atnaujina kas 500 ms
  static unsigned long last = 0;
  if (millis() - last < 500) return;
  last = millis();
 
  lcd.setCursor(0, 2);
  if (state == ARMED && millis() - stateSince < EXIT_DELAY_MS) {
    char buf[21];                     // išėjimo laiko atgalinis skaičiavimas
    snprintf(buf, sizeof(buf), "Leave in %2lus       ",
             (EXIT_DELAY_MS - (millis() - stateSince)) / 1000 + 1);
    lcd.print(buf);
  } else if (state == ALARM) {
    lcd.setCursor(0, 1);              // „ALARM!“ mirksi kas 500 ms
    lcd.print((millis() / 500) % 2 ? F("ALARM!") : F("      "));
  } else {
    lcd.print(F("                    "));
  }
}
 
// ======================= BŪSENOS KEITIMAS =======================
void setState(State s, const char* reason) {
  state = s;
  stateSince = millis();
  digitalWrite(PIN_BUZZER, LOW);      // išjungiamas garsas keičiant būseną
  addLog(reason);                     // kiekvienas perėjimas registruojamas
  drawLcd();
}
 
// ======================= GARSINIS SIGNALIZATORIUS =======================
void updateBuzzer() {                 // ALARM būsenoje garsas pulsuoja (300 ms)
  if (state == ALARM) digitalWrite(PIN_BUZZER, (millis() / 300) % 2);
}
 
void shortBeep() {                    // trumpas signalas atmetus kortelę
  digitalWrite(PIN_BUZZER, HIGH); delay(80); digitalWrite(PIN_BUZZER, LOW);
}
 
// ======================= ATSTUMO JUTIKLIS =======================
long readDistanceCm() {
  digitalWrite(PIN_TRIG, LOW);  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH); delayMicroseconds(10);   // 10 µs paleidimo impulsas
  digitalWrite(PIN_TRIG, LOW);
  unsigned long us = pulseIn(PIN_ECHO, HIGH, 30000UL);   // aido trukmė, max ~5 m
  if (us == 0) return -1;                                // objektas nerastas
  return us / 58;                                        // µs -> cm (garso greitis)
}
 
void checkSensor() {
  static unsigned long last = 0;
  static int hits = 0;                // iš eilės „artimų“ matavimų skaičius
  if (millis() - last < 150) return;  // matuojama kas 150 ms
  last = millis();
 
  lastDistanceCm = readDistanceCm();  // 1) duomenų nuskaitymas
 
  // 2) apdorojimas: jutiklis vertinamas tik ARMED būsenoje ir praėjus išėjimo laikui
  bool active = (state == ARMED) && (millis() - stateSince >= EXIT_DELAY_MS);
  if (!active) { hits = 0; return; }
 
  // 3) analizė: 3 matavimai iš eilės turi būti arčiau ribos (filtruojamas triukšmas)
  if (lastDistanceCm > 0 && lastDistanceCm < ALARM_DISTANCE_CM) {
    if (++hits >= 3) {
      char msg[48];                   // 4) sprendimas: pereiti į ALARM
      snprintf(msg, sizeof(msg), "ALARM: object at %ld cm", lastDistanceCm);
      setState(ALARM, msg);
      hits = 0;
    }
  } else {
    hits = 0;
  }
}
 
// ======================= RFID =======================
void onCard(bool authorized) {        // sprendimas priglaudus kortelę
  if (!authorized) {
    addLog("Unknown card rejected");  // nežinoma kortelė – tik žurnalas ir signalas
    shortBeep();
    return;
  }
  if (state == UNARMED)    setState(ARMED,   "Armed by card");
  else if (state == ARMED) setState(UNARMED, "Disarmed by card");
  else                     setState(UNARMED, "Alarm reset by card");
}
 
#if SIMULATION
// Wokwi neturi PN532 modelio: mygtukas imituoja leistiną kortelę
void setupCardReader() { pinMode(PIN_CARD_BTN, INPUT_PULLUP); }
 
void checkCard() {
  static bool wasPressed = false;
  static unsigned long lastPress = 0;
  bool pressed = digitalRead(PIN_CARD_BTN) == LOW;
  if (pressed && !wasPressed && millis() - lastPress > 300) {   // kontaktų virpėjimo filtras
    lastPress = millis();
    onCard(true);
  }
  wasPressed = pressed;
}
#else
bool isAuthorized(const uint8_t* uid, uint8_t len) {   // UID lyginimas su sąrašu
  for (const CardUid& c : AUTHORIZED_CARDS) {
    if (c.len == len && memcmp(c.uid, uid, len) == 0) return true;
  }
  return false;
}
 
void setupCardReader() {
  nfc.begin();
  if (!nfc.getFirmwareVersion()) {    // ar PN532 atsako I2C magistralėje
    addLog("PN532 not found!");
    return;
  }
  nfc.SAMConfig();                    // įjungiamas kortelių skaitymo režimas
  nfc.startPassiveTargetIDDetection(PN532_MIFARE_ISO14443A);  // neblokuojantis laukimas
  nfcOk = true;
}
 
void checkCard() {
  if (!nfcOk || digitalRead(PIN_PN532_IRQ) != LOW) return;    // kortelės dar nėra
 
  static unsigned long lastRead = 0;
  uint8_t uid[7], len = 0;
  if (nfc.readDetectedPassiveTargetID(uid, &len) && millis() - lastRead > 2000) {
    lastRead = millis();              // ta pati kortelė nepriimama dažniau nei kas 2 s
    Serial.print(F("Card UID: "));
    for (uint8_t i = 0; i < len; i++) { Serial.print(F("0x")); Serial.print(uid[i], HEX); Serial.print(' '); }
    Serial.println();
    onCard(isAuthorized(uid, len));
  }
  nfc.startPassiveTargetIDDetection(PN532_MIFARE_ISO14443A);  // laukiama kitos kortelės
}
#endif
 
// ======================= WI-FI =======================
void connectWiFi() {
  lcd.clear(); lcd.print(F("Connecting WiFi..."));
#if !SIMULATION
  if (WiFi.status() == WL_NO_MODULE) { addLog("WiFi module missing"); return; }
#endif
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) delay(500);  // iki 20 s
 
  if (WiFi.status() == WL_CONNECTED) {
    server.begin();
    Serial.print(F("Dashboard: http://")); Serial.println(WiFi.localIP());
    addLog("WiFi connected");
  } else {
    addLog("WiFi failed");
  }
}
 
// ======================= ŽINIATINKLIO SERVERIS =======================
// Valdymo skydelio puslapis saugomas flash atmintyje (const), kad būtų taupoma SRAM.
// JavaScript kas 2 s užklausia /status ir atnaujina būseną bei žurnalą.
const char PAGE[] = R"rawliteral(<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Security system</title>
<style>
body{font-family:sans-serif;max-width:480px;margin:20px auto;padding:0 12px}
#st{font-size:28px;font-weight:bold;padding:16px;border-radius:8px;text-align:center;color:#fff;background:#888}
#st.UNARMED{background:#2e7d32}#st.ARMED{background:#f9a825}#st.ALARM{background:#c62828}
button{font-size:18px;padding:10px 20px;margin:4px}input{font-size:18px;width:110px}
li{font-family:monospace}
</style></head><body>
<h2>Security system</h2>
<div id="st">...</div>
<p>Distance: <span id="d">-</span></p>
<p>PIN: <input id="pin" type="password" inputmode="numeric">
<button onclick="act('arm')">Arm</button><button onclick="act('disarm')">Disarm</button></p>
<p id="msg"></p>
<h3>Log</h3><ul id="log"></ul>
<script>
async function upd(){try{
 const s=await (await fetch('/status')).json();
 const e=document.getElementById('st');e.textContent=s.state;e.className=s.state;
 document.getElementById('d').textContent=s.distance<0?'-':s.distance+' cm';
 document.getElementById('log').innerHTML=s.logs.map(l=>'<li>'+l+'</li>').join('');
}catch(e){}}
async function act(a){
 const r=await fetch('/'+a,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
   body:'pin='+encodeURIComponent(document.getElementById('pin').value)});
 document.getElementById('msg').textContent=(await r.json()).msg;upd();
}
setInterval(upd,2000);upd();
</script></body></html>)rawliteral";
 
void sendHeader(WiFiClient& c, int code, const char* type) {   // HTTP atsakymo antraštė
  c.print(F("HTTP/1.1 ")); c.print(code);
  c.println(code == 200 ? " OK" : code == 403 ? " Forbidden" : " Not Found");
  c.print(F("Content-Type: ")); c.println(type);
  c.println(F("Connection: close"));
  c.println();
}
 
void sendStatus(WiFiClient& c) {      // būsena, atstumas ir žurnalas JSON formatu
  sendHeader(c, 200, "application/json");
  c.print(F("{\"state\":\"")); c.print(stateName(state));
  c.print(F("\",\"distance\":")); c.print(lastDistanceCm);
  c.print(F(",\"logs\":["));
  for (int i = 0; i < logCount; i++) {            // naujausi įrašai pirmi
    int idx = (logHead - 1 - i + LOG_SIZE) % LOG_SIZE;
    if (i) c.print(',');
    c.print('"'); c.print(logs[idx]); c.print('"');
  }
  c.print(F("]}"));
}
 
// Nuotolinio veiksmo sprendimas: tikrinamas PIN ir leistini perėjimai
const char* webAction(bool arm, const String& pin, int& code) {
  code = 200;
  if (pin != WEB_PIN) { addLog("Web: wrong PIN"); code = 403; return "Wrong PIN"; }
  if (arm) {
    if (state == UNARMED) { setState(ARMED, "Armed from web"); return "Armed"; }
    return state == ARMED ? "Already armed" : "Alarm is active";
  }
  if (state == ARMED) { setState(UNARMED, "Disarmed from web"); return "Disarmed"; }
  if (state == ALARM) {
    if (WEB_CAN_RESET_ALARM) { setState(UNARMED, "Alarm reset from web"); return "Alarm reset"; }
    code = 403; return "Alarm can only be reset with a card";
  }
  return "Already unarmed";
}
 
void handleWeb() {
  WiFiClient client = server.available();
  if (!client) return;                // užklausų nėra – grįžtama į pagrindinį ciklą
  client.setTimeout(1000);            // apsauga nuo „pakibusio“ kliento
 
  // Užklausos eilutė, pvz., "POST /arm HTTP/1.1"
  String req = client.readStringUntil('\n');
  int s1 = req.indexOf(' '), s2 = req.indexOf(' ', s1 + 1);
  String method = req.substring(0, s1);
  String path   = req.substring(s1 + 1, s2);
 
  // Antraštės: reikalinga tik Content-Length
  int contentLen = 0;
  while (true) {
    String h = client.readStringUntil('\n');
    h.trim();
    if (h.length() == 0) break;
    h.toLowerCase();
    if (h.startsWith("content-length:")) contentLen = h.substring(15).toInt();
  }
 
  // Užklausos turinys, pvz., "pin=1234" (ribojama iki 63 simbolių)
  char body[64] = {0};
  if (contentLen > 0) client.readBytes(body, min(contentLen, (int)sizeof(body) - 1));
  String b(body), pin;
  int p = b.indexOf("pin=");
  if (p >= 0) { int e = b.indexOf('&', p); pin = b.substring(p + 4, e < 0 ? b.length() : e); }
 
  // Maršrutizavimas
  if (method == "GET" && path == "/") {
    sendHeader(client, 200, "text/html");
    client.print(PAGE);
  } else if (method == "GET" && path == "/status") {
    sendStatus(client);
  } else if (method == "POST" && (path == "/arm" || path == "/disarm")) {
    int code;
    const char* msg = webAction(path == "/arm", pin, code);
    sendHeader(client, code, "application/json");
    client.print(F("{\"msg\":\"")); client.print(msg); client.print(F("\"}"));
  } else {
    sendHeader(client, 404, "text/plain");
    client.print(F("Not found"));
  }
  delay(1);
  client.stop();
}
 
// ======================= SETUP / LOOP =======================
void setup() {
  Serial.begin(115200);
  delay(1500);                        // neblokuojama laukiant USB jungties
 
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
 
  Wire.begin();                       // I2C magistralė
  lcd.init();
  lcd.backlight();
 
  setupCardReader();
  connectWiFi();
  setState(UNARMED, "System started");
}
 
void loop() {                         // neblokuojantis pagrindinis ciklas
  checkCard();      // RFID (Wokwi – mygtukas)
  checkSensor();    // HC-SR04 -> ALARM
  updateBuzzer();   // pulsuojantis garsas ALARM būsenoje
  updateLcd();      // mirksėjimas / išėjimo laikas
  handleWeb();      // valdymo skydelis
 
  // Nutrūkus Wi-Fi, prisijungiama iš naujo kas 30 s (ne ALARM metu, kad neužstrigtų sirena)
  static unsigned long lastWifiTry = 0;
  if (WiFi.status() != WL_CONNECTED && state != ALARM && millis() - lastWifiTry > 30000) {
    lastWifiTry = millis();
    connectWiFi();
    drawLcd();
  }
}
