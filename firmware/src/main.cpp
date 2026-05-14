/*#include <Arduino.h>
#include <WiFi.h>

// ⚠️ 본인 핫스팟 정보로 바꿔주세요
const char* WIFI_SSID     = "leedj";
const char* WIFI_PASSWORD = "123456789";

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("==========================");
  Serial.println("  Smart Dispenser STEP 1  ");
  Serial.println("==========================");
  Serial.print("Wi-Fi 연결 중: ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // 연결될 때까지 대기 (최대 20초)
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    retry++;
    if (retry > 40) {
      Serial.println();
      Serial.println("❌ Wi-Fi 연결 실패! SSID/비밀번호 확인하세요.");
      return;
    }
  }

  Serial.println();
  Serial.println("✅ Wi-Fi 연결 성공!");
  Serial.print("IP 주소: ");
  Serial.println(WiFi.localIP());
  Serial.print("신호 세기: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
}

void loop() {
  // Wi-Fi 끊기면 재연결
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️ Wi-Fi 끊김 - 재연결 시도 중...");
    WiFi.reconnect();
    delay(5000);
  }
}
*/

#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>
#include <ESP32Servo.h>
#include <LiquidCrystal_I2C.h>

// ─── 핀 정의 ──────────────────────────────────────────
#define SERVO_PIN   18
#define IR_PIN      14
#define BUZZER_PIN  25
#define LED_PIN     26

// ─── 서보 각도 ────────────────────────────────────────
#define ANGLE_MORNING  0
#define ANGLE_LUNCH    120
#define ANGLE_DINNER   240
#define ANGLE_HOME     0

// ─── 부저 LEDC 설정 (수동 부저 = PWM 필요) ────────────
#define BUZZER_CHANNEL  2      // ESP32Servo가 ch0 쓰므로 ch2 사용
#define BUZZER_FREQ     2000   // 2kHz (귀에 잘 들리는 주파수)
#define BUZZER_RES      8      // 8-bit

// ─── 객체 생성 ────────────────────────────────────────
RTC_DS3231 rtc;
Servo dispenserServo;
LiquidCrystal_I2C lcd(0x27, 16, 2);  // ← 안 켜지면 0x3F로 변경

// ─── 상태 변수 ────────────────────────────────────────
bool medicineDispensed = false;
bool medicineTaken     = false;
bool alarmActive       = false;
String currentSlot     = "";

// LCD 상태 추적 (불필요한 clear() 방지 → 깜박임 감소)
enum DisplayState { STANDBY, ALARM, TAKEN };
DisplayState lastDisplayState = STANDBY;

// ─── 부저 제어 ────────────────────────────────────────
void buzzerOn() {
  ledcWrite(BUZZER_CHANNEL, 128);  // 50% duty → 소리 남
}

void buzzerOff() {
  ledcWrite(BUZZER_CHANNEL, 0);    // 0% duty → 무음
}

// ─── 알람 해제 ────────────────────────────────────────
void stopAlarm() {
  alarmActive = false;
  buzzerOff();
  digitalWrite(LED_PIN, LOW);
}

// ─── 서보 이동 ────────────────────────────────────────
void moveServo(int targetAngle) {
  int currentAngle = dispenserServo.read();
  if (currentAngle < targetAngle) {
    for (int pos = currentAngle; pos <= targetAngle; pos++) {
      dispenserServo.write(pos);
      delay(15);
    }
  } else {
    for (int pos = currentAngle; pos >= targetAngle; pos--) {
      dispenserServo.write(pos);
      delay(15);
    }
  }
}

// ─── 약 배출 ──────────────────────────────────────────
void dispenseMedicine(String slot) {
  Serial.print("Dispensing: ");
  Serial.println(slot);
  currentSlot = slot;

  if (slot == "morning")      moveServo(ANGLE_MORNING);
  else if (slot == "lunch")   moveServo(ANGLE_LUNCH);
  else if (slot == "dinner")  moveServo(ANGLE_DINNER);

  delay(1000);
  moveServo(ANGLE_HOME);

  medicineDispensed = true;
  medicineTaken     = false;
  alarmActive       = true;
}

// ─── LCD 업데이트 ─────────────────────────────────────
void updateLCD(DateTime now) {
  DisplayState current;
  if (medicineTaken)   current = TAKEN;
  else if (alarmActive) current = ALARM;
  else                  current = STANDBY;

  // 상태 바뀔 때만 clear() → 깜박임 방지
  if (current != lastDisplayState) {
    lcd.clear();
    lastDisplayState = current;
  }

  char buf[17];

  if (current == TAKEN) {
    lcd.setCursor(0, 0);
    lcd.print("  Dose complete!");
    lcd.setCursor(0, 1);
    sprintf(buf, "    %02d:%02d:%02d    ", now.hour(), now.minute(), now.second());
    lcd.print(buf);

  } else if (current == ALARM) {
    lcd.setCursor(0, 0);
    lcd.print("!! Take medicine");
    lcd.setCursor(0, 1);
    lcd.print("  Hand to sensor");

  } else {
    // STANDBY: 현재 시간 표시
    lcd.setCursor(0, 0);
    sprintf(buf, "Time: %02d:%02d:%02d  ", now.hour(), now.minute(), now.second());
    lcd.print(buf);
    lcd.setCursor(0, 1);
    lcd.print(" Smart Dispenser");
  }
}

// ─── setup ────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("================================");
  Serial.println("  Smart Dispenser STEP 6 + 7   ");
  Serial.println("  LCD + Buzzer + LED            ");
  Serial.println("================================");

  Wire.begin(21, 22);

  // RTC
  if (!rtc.begin()) {
    Serial.println("[ERROR] DS3231 not found!");
    while (1);
  }
  Serial.println("[OK] DS3231");
  if (rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Smart Dispenser");
  lcd.setCursor(0, 1);
  lcd.print(" Initializing...");
  Serial.println("[OK] LCD");
  delay(1500);

  // 서보
  dispenserServo.attach(SERVO_PIN);
  dispenserServo.write(ANGLE_HOME);
  delay(500);
  Serial.println("[OK] Servo");

  // IR 센서
  pinMode(IR_PIN, INPUT);
  Serial.println("[OK] IR Sensor");

  // 부저 (LEDC 초기화 → setup에서 한 번만)
  ledcSetup(BUZZER_CHANNEL, BUZZER_FREQ, BUZZER_RES);
  ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL);
  buzzerOff();
  Serial.println("[OK] Buzzer");

  // LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  Serial.println("[OK] LED");

  Serial.println();
  Serial.println("Commands: m=morning  l=lunch  d=dinner");
}

// ─── loop ─────────────────────────────────────────────
void loop() {
  DateTime now = rtc.now();
  bool detected = (digitalRead(IR_PIN) == LOW);

  // ── 복용 완료 감지 ──────────────────────────────────
  if (medicineDispensed && !medicineTaken && detected) {
    medicineTaken     = true;
    medicineDispensed = false;
    stopAlarm();
    Serial.printf("[OK] Taken! Time: %02d:%02d\n", now.hour(), now.minute());
  }

  // ── 알람: 부저 + LED 0.5초 토글 ─────────────────────
  static unsigned long lastToggle = 0;
  static bool alarmState = false;
  if (alarmActive && (millis() - lastToggle > 500)) {
    alarmState = !alarmState;
    if (alarmState) buzzerOn();
    else            buzzerOff();
    digitalWrite(LED_PIN, alarmState ? HIGH : LOW);
    lastToggle = millis();
  }

  // ── LCD 1초마다 갱신 ─────────────────────────────────
  static unsigned long lastLCD = 0;
  if (millis() - lastLCD > 1000) {
    updateLCD(now);
    lastLCD = millis();
  }

  // ── 시리얼 로그 1초마다 ──────────────────────────────
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 1000) {
    Serial.printf("[%02d:%02d:%02d] IR:%s | %s\n",
      now.hour(), now.minute(), now.second(),
      detected ? "ON" : "OFF",
      medicineTaken ? "Taken" : (alarmActive ? "ALARM" : "Standby"));
    lastPrint = millis();
  }

  // ── 시리얼 입력 ──────────────────────────────────────
  if (Serial.available()) {
    char c = Serial.read();
    if      (c == 'm') dispenseMedicine("morning");
    else if (c == 'l') dispenseMedicine("lunch");
    else if (c == 'd') dispenseMedicine("dinner");
  }
}
