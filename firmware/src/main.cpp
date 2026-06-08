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

// 슬롯 번호(1~8): 1=0°, 2=45°, 3=90°, 4=135°, 5=180°, 6=225°, 7=270°, 8=315°
#define MEAL_MORNING_SLOT 2   // 45°
#define MEAL_LUNCH_SLOT   4   // 135°
#define MEAL_DINNER_SLOT  6   // 225°

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
const int   mealSlot[3]  = { MEAL_MORNING_SLOT, MEAL_LUNCH_SLOT, MEAL_DINNER_SLOT };

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

// 슬롯 번호(1~8) 절대 위치로 정방향 회전. 1=0°, 2=45°, ..., 8=315°.
void moveToSlot(int slotNum) {
  long targetPos = (long)((slotNum - 1) % NUM_SLOTS) * STEPS_PER_SLOT;
  long diff = (targetPos - currentPos + STEPS_PER_REV) % STEPS_PER_REV;
  Serial.printf("LOG slot=%d target=%ld diff=%ld\n", slotNum, targetPos, diff);
  for (long i = 0; i < diff; i++) {
    stepOnce(1);
    currentPos = (currentPos + 1) % STEPS_PER_REV;
  }
  stopMotor();
  Serial.printf("LOG pos=%ld (%.1f deg)\n",
                currentPos, currentPos * 360.0 / STEPS_PER_REV);
}

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

void dispenseMedicine(int meal) {
  Serial.printf("EVT DISPENSED %s\n", slotNames[meal]);
  moveToSlot(mealSlot[meal]);   // 아침=2(45°), 점심=4(135°), 저녁=6(225°)
  schedules[meal].dispensed = true;
  dispenseTime = millis();
  irReady      = false;
  activeSlot   = meal;
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

  // JOG <signed_steps>: 모터 직접 회전 (양수=정방향, 음수=역방향)
  if (line.startsWith("JOG ")) {
    long steps = line.substring(4).toInt();
    int dir = (steps >= 0) ? 1 : -1;
    long n = labs(steps);
    Serial.printf("LOG jog %ld steps (dir=%d)\n", steps, dir);
    for (long i = 0; i < n; i++) {
      stepOnce(dir);
      if (dir > 0) currentPos = (currentPos + 1) % STEPS_PER_REV;
      else         currentPos = (currentPos + STEPS_PER_REV - 1) % STEPS_PER_REV;
    }
    stopMotor();
    Serial.printf("LOG pos=%ld (%.1f deg)\n", currentPos, currentPos * 360.0 / STEPS_PER_REV);
    return;
  }

  // SETPOS <steps>: 현재 위치 좌표만 갱신 (모터 동작 X, 보정용)
  if (line.startsWith("SETPOS ")) {
    long p = line.substring(7).toInt();
    currentPos = ((p % STEPS_PER_REV) + STEPS_PER_REV) % STEPS_PER_REV;
    Serial.printf("LOG setpos=%ld (%.1f deg)\n",
                  currentPos, currentPos * 360.0 / STEPS_PER_REV);
    return;
  }

  // HOME: 슬롯 1번(0°)로 정방향 회전
  if (line == "HOME") {
    moveToSlot(1);
    return;
  }

  // LCDRST: LCD 재초기화 + 테스트 패턴 5회 반복 (안정화 + state 갱신 차단)
  if (line == "LCDRST") {
    if (!lcd) { Serial.println("LOG no lcd"); return; }
    lcd->init();
    delay(50);
    lcd->backlight();
    delay(20);
    for (int rep = 0; rep < 5; rep++) {
      lcd->clear();
      delay(10);
      lcd->setCursor(0, 0);
      lcd->print("AAAAAAAAAAAAAAAA");
      delay(10);
      lcd->setCursor(0, 1);
      lcd->print("BBBBBBBBBBBBBBBB");
      delay(300);
    }
    delay(500);
    lcd->clear();
    delay(10);
    lcd->setCursor(0, 0);
    lcd->print("0123456789ABCDEF");
    delay(10);
    lcd->setCursor(0, 1);
    lcd->print("LCD TEST OK!");
    prevState = currentState;
    Serial.println("LOG lcd reinit");
    return;
  }

  // LCD <text>: 임의 문자열을 row 0에 표시 (최대 16자)
  if (line.startsWith("LCD ")) {
    if (!lcd) { Serial.println("LOG no lcd"); return; }
    String text = line.substring(4);
    lcd->clear();
    lcd->setCursor(0, 0);
    lcd->print(text.substring(0, 16));
    prevState = currentState;
    Serial.printf("LOG lcd: %s\n", text.c_str());
    return;
  }

  // SLOT <1~8>: 지정 슬롯의 절대 위치로 정방향 회전
  if (line.startsWith("SLOT ")) {
    int s = line.substring(5).toInt();
    if (s < 1 || s > NUM_SLOTS) {
      Serial.printf("LOG bad slot num: %d\n", s);
      return;
    }
    moveToSlot(s);
    return;
  }

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
  Wire.setClock(50000);   // 50 kHz - 신호 안정화 (배선/풀업 마진 확보)

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

  // LCD 자동 재연결: 부팅 시 못 찾았거나 빠진 경우 3초마다 재스캔
  static unsigned long lastLcdScan = 0;
  if (!lcd && (millis() - lastLcdScan) >= 3000UL) {
    lastLcdScan = millis();
    for (uint8_t a : {0x27, 0x3F, 0x20, 0x38}) {
      Wire.beginTransmission(a);
      if (Wire.endTransmission() == 0) {
        lcdAddr = a;
        lcd = new LiquidCrystal_I2C(a, 16, 2);
        lcd->init();
        lcd->backlight();
        prevState = (State)-1;   // 다음 updateLCD에서 그리기
        Serial.printf("LOG LCD reconnected at 0x%02X\n", a);
        break;
      }
    }
  }

  updateLCD();
  delay(100);
}
