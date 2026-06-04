// ============================================================
//  Smart Medicine Dispenser - Firmware (Serial Bridge Mode)
//  28BYJ-48 스테퍼모터(ULN2003) + RTC + LCD + IR
//  Wi-Fi/Firebase 제거 → USB 시리얼로 호스트(Mac) 브리지와 통신
// ============================================================

#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>
#include <LiquidCrystal_I2C.h>

// -- 스테퍼모터 핀 (ULN2003 IN1~IN4)
#define STEPPER_IN1  18
#define STEPPER_IN2  19
#define STEPPER_IN3  23
#define STEPPER_IN4  5

#define STEPS_PER_REV   4096
#define NUM_SLOTS       8
#define STEPS_PER_SLOT  (STEPS_PER_REV / NUM_SLOTS)

#define SLOT_HOME    0
#define SLOT_MORNING 2
#define SLOT_LUNCH   4
#define SLOT_DINNER  6

#define IR_PIN       14
#define IR_GRACE_MS  2000

#define BUZZER_PIN     25
#define BUZZER_CHANNEL 2
#define BUZZER_FREQ    2000
#define BUZZER_RES     8

#define LED_PIN      26

#define MISSED_TIMEOUT  (10UL * 60UL * 1000UL)

enum State { STANDBY, ALARM, TAKEN };
State currentState = STANDBY;
State prevState    = STANDBY;

struct Schedule {
  String time;
  bool   enabled;
  bool   dispensed;
};
Schedule schedules[3];   // 0=morning, 1=lunch, 2=dinner

const char* slotNames[3] = { "morning", "lunch", "dinner" };
const int   slotNumbers[3] = { SLOT_MORNING, SLOT_LUNCH, SLOT_DINNER };

const int stepPins[4] = { STEPPER_IN1, STEPPER_IN2, STEPPER_IN3, STEPPER_IN4 };
const int stepSequence[8][4] = {
  {1, 0, 0, 0}, {1, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 1, 0},
  {0, 0, 1, 0}, {0, 0, 1, 1}, {0, 0, 0, 1}, {1, 0, 0, 1}
};
int  currentStepIndex = 0;
long currentPos       = 0;

unsigned long dispenseTime = 0;
bool          irReady      = false;
int           activeSlot   = -1;
unsigned long alarmStart   = 0;

RTC_DS3231        rtc;
LiquidCrystal_I2C* lcd = nullptr;
uint8_t           lcdAddr = 0x00;

String serialBuf;

// ============================================================
//  Stepper
// ============================================================

void stopMotor() {
  for (int i = 0; i < 4; i++) digitalWrite(stepPins[i], LOW);
}

void stepOnce(int direction) {
  currentStepIndex = (currentStepIndex + direction + 8) % 8;
  for (int i = 0; i < 4; i++) {
    digitalWrite(stepPins[i], stepSequence[currentStepIndex][i]);
  }
  delayMicroseconds(2000);
}

void moveToSlot(int targetSlot) {
  long targetPos = (long)targetSlot * STEPS_PER_SLOT;
  long diff = targetPos - currentPos;
  if (diff < 0) diff += STEPS_PER_REV;
  for (long i = 0; i < diff; i++) {
    stepOnce(1);
    currentPos = (currentPos + 1) % STEPS_PER_REV;
  }
  stopMotor();
}

void moveToHome() { moveToSlot(SLOT_HOME); }

// ============================================================
//  Buzzer / LED
// ============================================================

void buzzerOn() {
  ledcSetup(BUZZER_CHANNEL, BUZZER_FREQ, BUZZER_RES);
  ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL);
  ledcWrite(BUZZER_CHANNEL, 128);
}

void buzzerOff() {
  ledcWrite(BUZZER_CHANNEL, 0);
  ledcDetachPin(BUZZER_PIN);
}

void stopAlarm() {
  buzzerOff();
  digitalWrite(LED_PIN, LOW);
}

// ============================================================
//  Dispense
// ============================================================

void dispenseMedicine(int slot) {
  Serial.printf("EVT DISPENSED %s\n", slotNames[slot]);
  // 현재 위치에서 +45° (한 칸)만 회전, home 복귀 없음
  for (long i = 0; i < STEPS_PER_SLOT; i++) {
    stepOnce(1);
    currentPos = (currentPos + 1) % STEPS_PER_REV;
  }
  stopMotor();
  Serial.printf("LOG pos=%ld (%.1f deg)\n", currentPos, currentPos * 360.0 / STEPS_PER_REV);
  schedules[slot].dispensed = true;
  dispenseTime = millis();
  irReady      = false;
  activeSlot   = slot;
  alarmStart   = millis();
  currentState = ALARM;
  buzzerOn();
  digitalWrite(LED_PIN, HIGH);
}

// ============================================================
//  LCD
// ============================================================

void updateLCD() {
  if (currentState == prevState) return;
  prevState = currentState;
  if (!lcd) return;
  lcd->clear();
  if (currentState == STANDBY) {
    lcd->setCursor(0, 0);
    lcd->print("Next Dose:");
    lcd->setCursor(0, 1);
    String nextTime = "--:--";
    for (int i = 0; i < 3; i++) {
      if (schedules[i].enabled && !schedules[i].dispensed) {
        nextTime = schedules[i].time;
        break;
      }
    }
    lcd->print(nextTime);
  } else if (currentState == ALARM) {
    lcd->setCursor(0, 0);
    lcd->print("!! Take Medicine");
    lcd->setCursor(0, 1);
    lcd->print("Please take now!");
  } else if (currentState == TAKEN) {
    lcd->setCursor(0, 0);
    lcd->print("Good Job!");
    lcd->setCursor(0, 1);
    lcd->print("Dose recorded :)");
  }
}

// ============================================================
//  Serial protocol
// ============================================================

int slotFromName(const String& s) {
  if (s == "morning") return 0;
  if (s == "lunch")   return 1;
  if (s == "dinner")  return 2;
  return -1;
}

void handleCommand(String line) {
  line.trim();
  if (line.length() == 0) return;

  // DISPENSE <slot>
  if (line.startsWith("DISPENSE ")) {
    String name = line.substring(9); name.trim();
    int slot = slotFromName(name);
    if (slot < 0) { Serial.printf("LOG bad slot: %s\n", name.c_str()); return; }
    if (currentState != STANDBY) { Serial.println("LOG busy"); return; }
    dispenseMedicine(slot);
    return;
  }

  // SCHED <slot> HH:MM <0|1>
  if (line.startsWith("SCHED ")) {
    String rest = line.substring(6);
    int sp1 = rest.indexOf(' ');
    int sp2 = rest.indexOf(' ', sp1 + 1);
    if (sp1 < 0 || sp2 < 0) { Serial.println("LOG bad SCHED"); return; }
    String name = rest.substring(0, sp1);
    String tm   = rest.substring(sp1 + 1, sp2);
    String en   = rest.substring(sp2 + 1); en.trim();
    int slot = slotFromName(name);
    if (slot < 0) { Serial.println("LOG bad slot"); return; }
    schedules[slot].time      = tm;
    schedules[slot].enabled   = (en == "1" || en == "true");
    schedules[slot].dispensed = false;
    Serial.printf("LOG sched %s %s %d\n", name.c_str(), tm.c_str(),
                  schedules[slot].enabled);
    prevState = (State)-1;   // force LCD refresh
    return;
  }

  // TIME YYYY-MM-DDTHH:MM:SS
  if (line.startsWith("TIME ")) {
    String t = line.substring(5); t.trim();
    int Y = t.substring(0, 4).toInt();
    int M = t.substring(5, 7).toInt();
    int D = t.substring(8, 10).toInt();
    int h = t.substring(11, 13).toInt();
    int m = t.substring(14, 16).toInt();
    int s = t.substring(17, 19).toInt();
    rtc.adjust(DateTime(Y, M, D, h, m, s));
    Serial.println("LOG time synced");
    return;
  }

  if (line == "PING") { Serial.println("PONG"); return; }

  Serial.printf("LOG unknown: %s\n", line.c_str());
}

void pollSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      handleCommand(serialBuf);
      serialBuf = "";
    } else {
      serialBuf += c;
      if (serialBuf.length() > 200) serialBuf = "";
    }
  }
}

// ============================================================
//  setup / loop
// ============================================================

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < 4; i++) {
    pinMode(stepPins[i], OUTPUT);
    digitalWrite(stepPins[i], LOW);
  }
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  pinMode(IR_PIN, INPUT);

  Wire.begin(21, 22);

  if (!rtc.begin()) {
    Serial.println("LOG rtc init fail");
    while (1);
  }

  // I2C scan + LCD bind (try 0x27, 0x3F, then scan range)
  Serial.print("LOG i2c scan:");
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf(" 0x%02X", a);
      if (lcdAddr == 0x00 && (a == 0x27 || a == 0x3F || a == 0x20 || a == 0x38)) {
        lcdAddr = a;
      }
    }
  }
  Serial.println();
  if (lcdAddr == 0x00) {
    Serial.println("LOG no LCD found on I2C");
  } else {
    Serial.printf("LOG LCD bound at 0x%02X\n", lcdAddr);
    lcd = new LiquidCrystal_I2C(lcdAddr, 16, 2);
    lcd->init();
    lcd->backlight();
    lcd->setCursor(0, 0);
    lcd->print("Initializing...");
  }

  for (int i = 0; i < 3; i++) {
    schedules[i].time = "--:--";
    schedules[i].enabled = false;
    schedules[i].dispensed = false;
  }

  if (lcd) lcd->clear();
  Serial.println("READY");
}

void loop() {
  pollSerial();

  DateTime now = rtc.now();
  String nowTime = String(now.hour() < 10 ? "0" : "") + now.hour()
                 + ":" + (now.minute() < 10 ? "0" : "") + now.minute();

  if (!irReady && dispenseTime > 0)
    irReady = (millis() - dispenseTime) > IR_GRACE_MS;

  if (currentState == ALARM && irReady) {
    if (digitalRead(IR_PIN) == LOW) {
      stopAlarm();
      Serial.printf("EVT TAKEN %s\n", slotNames[activeSlot]);
      currentState = TAKEN;
      updateLCD();
      delay(3000);
      currentState = STANDBY;
    }
  }

  if (currentState == ALARM && activeSlot >= 0) {
    if ((millis() - alarmStart) >= MISSED_TIMEOUT) {
      stopAlarm();
      Serial.printf("EVT MISSED %s\n", slotNames[activeSlot]);
      currentState = STANDBY;
      activeSlot   = -1;
    }
  }

  static int lastMinute = -1;
  if (now.second() == 0 && now.minute() != lastMinute) {
    lastMinute = now.minute();
    for (int i = 0; i < 3; i++) {
      if (schedules[i].enabled && !schedules[i].dispensed &&
          currentState == STANDBY && schedules[i].time == nowTime) {
        dispenseMedicine(i);
        break;
      }
    }
  }

  if (now.hour() == 0 && now.minute() == 0 && now.second() == 0) {
    for (int i = 0; i < 3; i++) schedules[i].dispensed = false;
    Serial.println("LOG midnight reset");
  }

  updateLCD();
  delay(100);
}
