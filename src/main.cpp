#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <esp_int_wdt.h>
#include <esp_task_wdt.h>
#include <EEPROM.h>
#include <SimpleTimer.h>
#include <time.h>

// eeprom settings, no need to change this if no eeprom errors
#define NO_POWER_FLAG 7 
#define WITH_POWER_FLAG 16  
#define EEPROM_ADDR 8 

// Telegram
// please use @myidbot "/getgroupid" command to determine group/channel id
#define TG_CHAT_ID "YOUR OWN VALUE"
#include <UniversalTelegramBot.h>  
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
// please use @BothFather to create a bot, you need to add the bot to the group/channel
#define BOTtoken "YOUR OWN VALUE"
WiFiClientSecure client;
UniversalTelegramBot bot(BOTtoken, client);
// Telegram end

// Wifi connection settings
// #define WIFI_SSID "AJAX | Technical"
// #define WIFI_PASSWORD "ajaX4technics2020"
#define WIFI_SSID "YOUR OWN VALUE"
#define WIFI_PASSWORD "YOUR OWN VALUE"

// Text messages
#define MSG_POWER_ON "💡 Світло з'явилося! 🎉"
#define MSG_POWER_OFF "🪫 Світло скінчилося! 😠 ⚡"

// Power probe pin on the board, up to 3.3v, please do not use 5v directly
#define EXTERNAL_POWER_PROBE_PIN 26

SimpleTimer timer;

boolean isEepromError = false;
time_t powerOffTime; // Змінна для зберігання часу вимкнення світла

boolean isEepromValid(int eeprom) {  
  return eeprom == WITH_POWER_FLAG || eeprom == NO_POWER_FLAG;
}

// Функція для обчислення тривалості без світла та формування повідомлення
String calculatePowerOutageDuration(time_t startTime, time_t endTime) {
  long duration = endTime - startTime;
  int hours = duration / 3600;
  int minutes = (duration % 3600) / 60;

  String durationMessage = "";
  if (hours > 0) {
    durationMessage += String(hours) + " год ";
  }
  durationMessage += String(minutes) + " хв";

  return durationMessage;
}

void readExternalPower() {
  
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  int eeprom = EEPROM.read(EEPROM_ADDR);
  if (!isEepromValid(eeprom)) {    
    //  eeprom write count is limited theoreticaly leading to an error here, 
    //  you can try to change address or use another esp32 board
    Serial.println("EEPROM error!");
    isEepromError = true; 
    return;
  } else {
    isEepromError = false;
  }

  boolean isPowerNow = digitalRead(EXTERNAL_POWER_PROBE_PIN) == HIGH;
  boolean isPowerBefore = eeprom == WITH_POWER_FLAG;

  Serial.print("status: ");
  Serial.println(isPowerNow ? "on" : "off");

  if (isPowerBefore != isPowerNow) {
    Serial.print("status change detected, trying to send the message...");
    if (isPowerNow) {
      // Світло з'явилося
      time_t currentTime = time(nullptr); // Отримуємо поточний час
      String outageDuration = calculatePowerOutageDuration(powerOffTime, currentTime); // Обчислюємо тривалість без світла
      String message = String(MSG_POWER_ON) + "\n⏱️ Світла не було " + outageDuration;
      
      if (!bot.sendMessage(TG_CHAT_ID, message, "")) {
        return;
      }
      EEPROM.write(EEPROM_ADDR, WITH_POWER_FLAG);
      EEPROM.commit();   
    } else {
      // Світло вимкнулося
      powerOffTime = time(nullptr); // Запам'ятовуємо час вимкнення світла
      if (!bot.sendMessage(TG_CHAT_ID, MSG_POWER_OFF, "")) {
        return;
      }
      EEPROM.write(EEPROM_ADDR, NO_POWER_FLAG);
      EEPROM.commit();    
    }
  }
}

void setup() {
  Serial.begin(9600);

  pinMode(EXTERNAL_POWER_PROBE_PIN, INPUT);

  Serial.println();
  Serial.println("Init watchdog:");
  esp_task_wdt_init(60, true);
  esp_task_wdt_add(NULL);

  Serial.println("Init EEPROM:");
  if (!EEPROM.begin(16)) {
    while(true) {
      Serial.println("EEPROM fail");
      sleep(1);
    }
  }
  int eeprom = EEPROM.read(EEPROM_ADDR);
  Serial.println(eeprom);
  if (!isEepromValid(eeprom)) {
    // first start ever
    EEPROM.write(EEPROM_ADDR, WITH_POWER_FLAG);
    EEPROM.commit();  
    Serial.println("EEPROM initialized");
    eeprom = EEPROM.read(EEPROM_ADDR);
    Serial.println(eeprom);    
  } else {
    // next restarts
    Serial.println("EEPROM old value is valid");
  }

  // attempt to connect to Wifi network:
  Serial.print("Connecting to Wifi SSID ");
  Serial.print(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  client.setCACert(TELEGRAM_CERTIFICATE_ROOT); // Add root certificate for api.telegram.org
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.print("\nWiFi connected. IP address: ");
  Serial.println(WiFi.localIP());

  Serial.print("Retrieving time: ");
  configTime(0, 0, "pool.ntp.org"); // get UTC time via NTP
  time_t now = time(nullptr);
  while (now < 24 * 3600) {
    Serial.print(".");
    delay(100);
    now = time(nullptr);
  }
  Serial.println(now);

  // the default value is too small leading to duplicated messages because "ok" from TG server is discarded
  bot.waitForResponse = 25000;

  timer.setInterval(5000, readExternalPower);
}

void loop() {
  if (WiFi.status() == WL_CONNECTED && !isEepromError) {
    // watchdog: if esp_task_wdt_reset() is not called for too long,
    // the board is restarted in an attempt to fix itself
    esp_task_wdt_reset();
  }
  timer.run();
}
