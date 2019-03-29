#define MINUS_BUTTON_PIN 12
#define PLUS_BUTTON_PIN 13
#define MODE_BUTTON_PIN 14
#define HTTP_OK_CODE 200
#define INTERRUPT_TIMEOUT 300UL
#define LAST_ACTION_TIMEOUT 7000UL
#define MIN_TEMPERATURE 10UL
#define MAX_TEMPERATURE 30UL
#define MIN_DURATION 0UL
#define MAX_DURATION 24UL
#define MANUAL_MODE_DUTARION 1L // 2h
#define ONE_HOUR_SECONDS 3600L
#define LED 15

#include "ESP8266WiFi.h"
#include "ESP8266HTTPClient.h"
#include "ArduinoJson.h"
#include "base64.h"
#include "Wire.h"
#include "LiquidCrystal_I2C.h"
#include "TimeLib.h"
#include "Timezone.h"

enum Mode {TEMP, DURATION};

const String WIFI_SSID = "xxx";
const String WIFI_PASSWORD = "xxx";
const String FIBARO_LOGIN = "xxx";
const String FIBARO_PASSWORD = "xxx";
const String FIBARO_HEATING_GET = "http://xx:xx:xx:xx:80/api/panels/heating/x"; // add ip and heating zone id
const String FIBARO_SETTINGS_INFO = "http://xx:xx:xx:xx:80/api/settings/info"; // add ip
const String FIBARO_WEATHER = "http://xx:xx:xx:xx:80/api/weather"; // add ip

volatile unsigned long lastActionMillis = 0;
volatile unsigned long currentTemperature = 0;
volatile unsigned long newTemperature = 0;
volatile unsigned long newDuration = MANUAL_MODE_DUTARION;
volatile unsigned long lastShownedManualTemperature = 0;
volatile unsigned long lastShownedDuration = 0;
volatile enum Mode lastShownedMode = TEMP;
volatile enum Mode mode = TEMP;
volatile boolean isTemperatureChanged = false;
String modeTemp = "";

LiquidCrystal_I2C lcd(0x27, 16, 2);

void setup(void)
{ 
  Serial.begin(115200);
  Serial.println("");
  pinMode(LED, OUTPUT);
  digitalWrite(LED, HIGH);
  Wire.begin(4, 5);
  lcd.begin();
  lcd.backlight();
  printToLcd("Connecting...", "");

  boolean isConnectedToWiFi = connectToWiFi();

  if (isConnectedToWiFi) {
    getCurrentTemperature();
    if (modeTemp != "Vacation") {
      pinMode(MINUS_BUTTON_PIN, INPUT_PULLUP);
      pinMode(PLUS_BUTTON_PIN, INPUT_PULLUP);
      pinMode(MODE_BUTTON_PIN, INPUT_PULLUP);
      attachInterrupt(digitalPinToInterrupt(MINUS_BUTTON_PIN), minusCallback, RISING);
      attachInterrupt(digitalPinToInterrupt(PLUS_BUTTON_PIN), plusCallback, RISING);
      attachInterrupt(digitalPinToInterrupt(MODE_BUTTON_PIN), modeCallback, RISING);
    }
  } else {
    printToLcd("Connection error", "");
  }
  lastActionMillis = millis();
}

boolean connectToWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int i = 0;
  while (WiFi.status() != WL_CONNECTED && i < 200) {
    delay(50);
    i++;
  }
  return WiFi.status() == WL_CONNECTED;
}

void printToLcd(String firstLine, String secondLine) {
  lcd.clear();
  lcd.home();
  lcd.print(firstLine);
  lcd.setCursor(0, 1);
  lcd.print(secondLine);
}

int getCurrentTimestamp() {
  HTTPClient client;
  client.begin(FIBARO_SETTINGS_INFO); 
  String authorizationPlain = FIBARO_LOGIN + ":" + FIBARO_PASSWORD;
  String authorizationEncoded = base64::encode(authorizationPlain);
  client.addHeader("Authorization", "Basic " + authorizationEncoded);

  int httpCode = client.GET(); 
  int timestamp = 0;
  if (httpCode == HTTP_OK_CODE) {
    String payload = client.getString();
    DynamicJsonBuffer jsonBuffer;
    JsonObject& response = jsonBuffer.parseObject(payload);
    timestamp = response["timestamp"];
  } else {
    printToLcd("Connection error", "");
  }
  lastActionMillis = millis();
  client.end();
  return timestamp;
}

void getCurrentTemperature() {
  HTTPClient client;
  client.begin(FIBARO_HEATING_GET); 
  String authorizationPlain = FIBARO_LOGIN + ":" + FIBARO_PASSWORD;
  String authorizationEncoded = base64::encode(authorizationPlain);
  client.addHeader("Authorization", "Basic " + authorizationEncoded);

  int httpCode = client.GET(); 
  
  if (httpCode == HTTP_OK_CODE) {
    String payload = client.getString();
    printTemp(payload);
  } else {
    printToLcd("Connection error", "");
  }
  lastActionMillis = millis();
  client.end();
}

String getOutdoorTemperature() {
  HTTPClient client;
  client.begin(FIBARO_WEATHER); 
  String authorizationPlain = FIBARO_LOGIN + ":" + FIBARO_PASSWORD;
  String authorizationEncoded = base64::encode(authorizationPlain);
  client.addHeader("Authorization", "Basic " + authorizationEncoded);

  int httpCode = client.GET(); 

  if (httpCode == HTTP_OK_CODE) {
    String payload = client.getString();
    DynamicJsonBuffer jsonBuffer;
    JsonObject& response = jsonBuffer.parseObject(payload);
    String temp = response["Temperature"].as<String>();
    String lcdText = temp;
    lcdText += (char) 223;
    lcdText += "C";
    return lcdText;
  }
  lastActionMillis = millis();
  client.end();
  return "";
}

void setManualTemperature(long timpestamp) {
  HTTPClient client;
  client.begin(FIBARO_HEATING_GET); 
  String authorizationPlain = FIBARO_LOGIN + ":" + FIBARO_PASSWORD;
  String authorizationEncoded = base64::encode(authorizationPlain);
  client.addHeader("Authorization", "Basic " + authorizationEncoded);
  client.addHeader("Content-Type", "application/json");

  String request = "{\"properties\": {\"handTemperature\": ";
  request += newTemperature;
  request += ",\"handTimestamp\": ";
  request += (newDuration * ONE_HOUR_SECONDS + timpestamp);
  request += "}}";

  currentTemperature = 0;
  newTemperature = 0;
  newDuration = MANUAL_MODE_DUTARION;
  lastShownedManualTemperature = 0;
  lastShownedDuration = 0;
  lastShownedMode = TEMP;
  mode = TEMP;
  isTemperatureChanged = false;
  modeTemp = "";
  
  int httpCode = client.PUT(request); 

  if (httpCode == HTTP_OK_CODE) {
    String payload = client.getString();
    printTemp(payload);
    attachInterrupt(digitalPinToInterrupt(MINUS_BUTTON_PIN), minusCallback, RISING);
    attachInterrupt(digitalPinToInterrupt(PLUS_BUTTON_PIN), plusCallback, RISING);
    attachInterrupt(digitalPinToInterrupt(MODE_BUTTON_PIN), modeCallback, RISING);
  } else {
    printToLcd("Connection error", "");
  }

  lastActionMillis = millis();
  client.end();
}

void printTemp(String payload) {
  DynamicJsonBuffer jsonBuffer;
  JsonObject& response = jsonBuffer.parseObject(payload);
  modeTemp = response["mode"].as<String>();
  currentTemperature = response["properties"]["currentTemperature"];
  newTemperature = currentTemperature;
  String firstLine = getTemperatureLineText();
  String outdoorTemp = getOutdoorTemperature();
  for (int i = 0; i < (10 - outdoorTemp.length()); i++) {
    firstLine += " ";
  }
  firstLine += outdoorTemp;
  String secondLine = modeTemp;
  secondLine = secondLine + " " + getEndTimeText(response);
  printToLcd(firstLine, secondLine);
}

String getTemperatureLineText() {
  String lcdText = "";
  if (mode == TEMP && isTemperatureChanged) {
    lcdText = lcdText + ">";
  }
  lcdText = lcdText + newTemperature + ".0";
  lcdText = lcdText + (char) 223;
  lcdText = lcdText + "C";
  return lcdText;
}

String getDurationLineText() {
  String lcdText = "";
  if (mode == DURATION && isTemperatureChanged) {
    lcdText = lcdText + ">";
  }
  if (newDuration == 0 ) {
    lcdText += "disable";
  } else {
    lcdText = lcdText + newDuration;
    lcdText = lcdText + "h";
  }
  return lcdText;
}

String getEndTimeText(JsonObject& response) {
  String mode = response["mode"];
  if (mode == "Manual") {
    String handTimestamp = response["properties"]["handTimestamp"];
    int numer = handTimestamp.toInt();
    TimeChangeRule *tcr;
    time_t t = getLocal(numer, tcr);
    char buf[40];
    sprintf(buf, "    %.2d:%.2d", hour(t), minute(t), tcr -> abbrev);
    return buf;
  } 
  if (mode == "Schedule") {
    int timpestamp = getCurrentTimestamp();
    TimeChangeRule *tcr; 
    time_t t = getLocal(timpestamp, tcr);
    int h = hour(t);
    int m = minute(t);
    String intervals[4] = {"morning", "day", "evening", "night"};
    int nowWeekday = weekday(t);
    for (int i = 0; i <= 7; i++) {
      char* weekday2 = dayStr((nowWeekday + i) % 7);
      String w = String(weekday2);
      w.toLowerCase();
      for (int j = 0; j< 4; j++) {
        String restHour = response["properties"][w][intervals[j]]["hour"];
        String restMinute = response["properties"][w][intervals[j]]["minute"];
        String restTemp = response["properties"][w][intervals[j]]["temperature"];
        if (i == 0 && (restHour.toInt() * 60 + restMinute.toInt()) < (h *60 + m)) {
          continue;
        }
        if (restTemp.toInt() != currentTemperature) {
          char buf[40];
          sprintf(buf, "  %.2d:%.2d", restHour.toInt(), restMinute.toInt());
          return buf;
        }
      }
    }
  } 
  return "";
}

time_t getLocal(int timestamp, TimeChangeRule *tcr) {
  TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 2, 120}; // Central European Summer Time
  TimeChangeRule CET = {"CET ", Last, Sun, Oct, 3, 60}; // Central European Standard Time
  Timezone CE(CEST, CET);
  return CE.toLocal(timestamp, &tcr);
}

void plusCallback(){
  if (isInterruptTimeoutReached() || (mode == TEMP && (newTemperature + 1) > MAX_TEMPERATURE) || (mode == DURATION && (newDuration + 1) > MAX_DURATION)) { 
    return;
  }
  lastActionMillis = millis();
  switch (mode) {
    case TEMP:
      newTemperature++;
      break;
    case DURATION:
      newDuration++;
      break;
  }
  isTemperatureChanged = true;
}

void minusCallback(){
  if (isInterruptTimeoutReached() || (mode == TEMP && (newTemperature - 1) < MIN_TEMPERATURE) || (mode == DURATION && (newDuration - 1) < MIN_DURATION) || (mode == DURATION && newDuration == 0)) { 
    return;
  }
  if (mode == DURATION && modeTemp != "Manual" && ((newDuration - 1) == MIN_DURATION)) {
    return;
  }
  lastActionMillis = millis();
  switch (mode) {
    case TEMP:
      newTemperature--;
      break;
    case DURATION:
      newDuration--;
      break;
  }
  isTemperatureChanged = true;
}

void modeCallback(){
  if (isInterruptTimeoutReached() || newDuration == 0 ) { 
    return;
  }
  lastActionMillis = millis();
  if (isTemperatureChanged) {
    mode = mode == TEMP ? DURATION : TEMP;
  }
  isTemperatureChanged = true;
}

boolean isInterruptTimeoutReached() {
  return millis() - lastActionMillis < INTERRUPT_TIMEOUT;
}

void loop() {
  if (isTemperatureChanged && (newTemperature != lastShownedManualTemperature || lastShownedDuration != newDuration || lastShownedMode != mode)) {
    String firstLine = getTemperatureLineText();
    firstLine += " ";
    if (newDuration == 0 ) {
      firstLine = getDurationLineText();
    } else {
      firstLine = firstLine + getDurationLineText();
    }
    printToLcd(firstLine, "Manual set");
    lastShownedManualTemperature = newTemperature;
    lastShownedDuration = newDuration;
    lastShownedMode = mode;
  }
  if (millis() - lastActionMillis < LAST_ACTION_TIMEOUT) {
    return;
  }
  detachInterrupt(digitalPinToInterrupt(MINUS_BUTTON_PIN));
  detachInterrupt(digitalPinToInterrupt(PLUS_BUTTON_PIN));
  detachInterrupt(digitalPinToInterrupt(MODE_BUTTON_PIN));
  if ((newTemperature != 0 && newTemperature != currentTemperature) || (newDuration != MANUAL_MODE_DUTARION)) {
    printToLcd("Sending...", "");;
    int timpestamp = getCurrentTimestamp();
    setManualTemperature(timpestamp);
    lastActionMillis = millis();
    return;
  }
  lcd.noDisplay();
  lcd.noBacklight();
  ESP.deepSleep(0);
}
