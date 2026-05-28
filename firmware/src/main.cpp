// ============================================================
//  Smart Medicine Dispenser - Firmware (STEP 1~8)
//  28BYJ-48 스테퍼모터(ULN2003) + Firebase + RTC + LCD + IR
// ============================================================

#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <FirebaseESP32.h>

// -- Wi-Fi
#define WIFI_SSID      "leedj"
#define WIFI_PASSWORD  "dlehdwpqlqjs"

// -- Firebase
#define DATABASE_URL    "https://sedal-miracle-49697-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define DATABASE_SECRET "gD5bH5vtIcrBwA1Xm7ruOfBegiuRUYvFvKTcEARs"
#define DEVICE_ID       "device_001"

// -- 스테퍼모터 핀 (ULN2003 IN1~IN4)
#define STEPPER_IN1  18
#define STEPPER_IN2  19
#define STEPPER_IN3  23
#define STEPPER_IN4  5

// -- 스테퍼모터 설정
#define STEPS_PER_REV   4096
#define NUM_SLOTS       8
#define STEPS_PER_SLOT  (STEPS_PER_REV / NUM_SLOTS)  // 512 steps = 45도

// -- 슬롯 정의 (8칸, 각 45도)
//  슬롯:  0     1     2     3     4     5     6     7
//  각도:  0    45    90   135   180   225   270   315
#define SLOT_HOME    0
#define SLOT_MORNING 2   // 90도
#define SLOT_LUNCH   4   // 180도
#define SLOT_DINNER  6   // 270도

// -- IR 센서
#define IR_PIN       14
#define IR_GRACE_MS  2000

// -- 부저
#define BUZZER_PIN     25
#define BUZZER_CHANNEL 2
#define BUZZER_FREQ    2000
#define BUZZER_RES     8

// -- LED
#define LED_PIN      26

// -- 미복용 타임아웃
#define MISSED_TIMEOUT  (10UL * 60UL * 1000UL)

// -- 수동 배출 명령 폴링 간격
#define COMMAND_CHECK_INTERVAL 2000UL

// -- 상태 머신
enum State { STANDBY, ALARM, TAKEN };
State currentState = STANDBY;
State prevState    = STANDBY;

// -- 스케줄 구조체
struct Schedule {
  String time;
  bool   enabled;
  bool   dispensed;
};
Schedule schedules[3];   // 0=아침, 1=점심, 2=저녁

// -- 스테퍼 변수
const int stepPins[4] = { STEPPER_IN1, STEPPER_IN2, STEPPER_IN3, STEPPER_IN4 };

const int stepSequence[8][4] = {
  {1, 0, 0, 0},
  {1, 1, 0, 0},
  {0, 1, 0, 0},
  {0, 1, 1, 0},
  {0, 0, 1, 0},
  {0, 0, 1, 1},
  {0, 0, 0, 1},
  {1, 0, 0, 1}
};

int  currentStepIndex = 0;
long currentPos       = 0;

// -- IR / 알람 타이머
unsigned long dispenseTime = 0;
bool          irReady      = false;
int           activeSlot   = -1;
unsigned long alarmStart   = 0;

// -- 수동 배출 타이머
unsigned long lastCommandCheck = 0;

// -- Firebase / RTC / LCD
FirebaseData   fbData;
FirebaseConfig fbConfig;
FirebaseAuth   fbAuth;
RTC_DS3231     rtc;
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ============================================================
//  스테퍼모터 함수
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

  Serial.printf("[STEPPER] slot %d -> %ld steps\n", targetSlot, diff);

  for (long i = 0; i < diff; i++) {
    stepOnce(1);
    currentPos = (currentPos + 1) % STEPS_PER_REV;
  }
  stopMotor();
  Serial.printf("[STEPPER] 완료 (pos: %ld)\n", currentPos);
}

void moveToHome() { moveToSlot(SLOT_HOME); }

// ============================================================
//  부저 / LED 함수
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
//  Firebase 함수
// ============================================================

const char* slotNames[3] = { "morning", "lunch", "dinner" };

void fetchSchedule() {
  for (int i = 0; i < 3; i++) {
    String base = String("/schedules/") + DEVICE_ID + "/" + slotNames[i];
    if (Firebase.getString(fbData, base + "/time"))
      schedules[i].time = fbData.stringData();
    if (Firebase.getBool(fbData, base + "/enabled"))
      schedules[i].enabled = fbData.boolData();
    schedules[i].dispensed = false;
    Serial.printf("[FB] %s | time=%s enabled=%d\n",
      slotNames[i], schedules[i].time.c_str(), schedules[i].enabled);
  }
}

void writeTaken(int slot) {
  String path = String("/schedules/") + DEVICE_ID + "/" + slotNames[slot] + "/taken";
  Firebase.setBool(fbData, path, true);
  Serial.printf("[FB] taken=true -> %s\n", slotNames[slot]);
}

void writeMissed(int slot) {
  String path = String("/schedules/") + DEVICE_ID + "/" + slotNames[slot] + "/missed";
  Firebase.setBool(fbData, path, true);
  Serial.printf("[FB] missed=true -> %s\n", slotNames[slot]);
}

// ============================================================
//  약 배출 함수
// ============================================================

const int slotNumbers[3] = { SLOT_MORNING, SLOT_LUNCH, SLOT_DINNER };

void dispenseMedicine(int slot) {
  Serial.printf("[DISPENSE] %s 배출 시작\n", slotNames[slot]);
  moveToSlot(slotNumbers[slot]);
  delay(500);
  moveToHome();
  schedules[slot].dispensed = true;
  dispenseTime = millis();
  irReady      = false;
  activeSlot   = slot;
  alarmStart   = millis();
  currentState = ALARM;
  buzzerOn();
  digitalWrite(LED_PIN, HIGH);
  Serial.println("[DISPENSE] 완료 -> ALARM");
}

// ============================================================
//  수동 배출 명령 확인 (앱 버튼 → Firebase → 즉시 실행)
// ============================================================

void checkManualDispense() {
  String path = String("/dispense/") + DEVICE_ID + "/command";
  if (!Firebase.getString(fbData, path)) return;

  String cmd = fbData.stringData();
  cmd.trim();
  if (cmd.length() == 0) return;

  // 중복 실행 방지: 읽자마자 명령 초기화
  Firebase.setString(fbData, path, "");

  int slot = -1;
  if (cmd == "morning")      slot = 0;
  else if (cmd == "lunch")   slot = 1;
  else if (cmd == "dinner")  slot = 2;

  if (slot < 0) {
    Serial.printf("[MANUAL] 알 수 없는 명령: %s\n", cmd.c_str());
    return;
  }

  if (currentState != STANDBY) {
    Serial.printf("[MANUAL] 이미 동작 중 (%d) - 명령 무시\n", currentState);
    return;
  }

  Serial.printf("[MANUAL] %s 즉시 배출\n", slotNames[slot]);
  dispenseMedicine(slot);
}

// ============================================================
//  LCD 출력 함수
// ============================================================

void updateLCD(DateTime now) {
  if (currentState == prevState) return;
  prevState = currentState;
  lcd.clear();

  if (currentState == STANDBY) {
    lcd.setCursor(0, 0);
    lcd.print("Next Dose:");
    lcd.setCursor(0, 1);
    String nextTime = "--:--";
    for (int i = 0; i < 3; i++) {
      if (schedules[i].enabled && !schedules[i].dispensed) {
        nextTime = schedules[i].time;
        break;
      }
    }
    lcd.print(nextTime);
  } else if (currentState == ALARM) {
    lcd.setCursor(0, 0);
    lcd.print("!! Take Medicine");
    lcd.setCursor(0, 1);
    lcd.print("Please take now!");
  } else if (currentState == TAKEN) {
    lcd.setCursor(0, 0);
    lcd.print("Good Job!");
    lcd.setCursor(0, 1);
    lcd.print("Dose recorded :)");
  }
}

// ============================================================
//  Wi-Fi 연결
// ============================================================

void connectWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[WiFi] 연결 중");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[WiFi] 연결 완료: " + WiFi.localIP().toString());
}

// ============================================================
//  setup / loop
// ============================================================

void setup() {
  Serial.begin(115200);

  // 스테퍼 핀 초기화
  for (int i = 0; i < 4; i++) {
    pinMode(stepPins[i], OUTPUT);
    digitalWrite(stepPins[i], LOW);
  }

  // 부저 / LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // IR
  pinMode(IR_PIN, INPUT);

  // I2C
  Wire.begin(21, 22);

  // RTC
  if (!rtc.begin()) {
    Serial.println("[RTC] 초기화 실패!");
    while (1);
  }

  // LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Initializing...");

  // Wi-Fi
  connectWiFi();

  // Firebase
  fbConfig.database_url = DATABASE_URL;
  fbConfig.signer.tokens.legacy_token = DATABASE_SECRET;
  Firebase.begin(&fbConfig, &fbAuth);
  Firebase.reconnectWiFi(true);

  // 스케줄 가져오기
  fetchSchedule();

  lcd.clear();
  Serial.println("[SETUP] 완료");
}

void loop() {
  DateTime now = rtc.now();
  String nowTime = String(now.hour() < 10 ? "0" : "") + now.hour()
                 + ":" + (now.minute() < 10 ? "0" : "") + now.minute();

  // IR 준비 여부
  if (!irReady && dispenseTime > 0)
    irReady = (millis() - dispenseTime) > IR_GRACE_MS;

  // ALARM: IR 감지 -> 복용 완료
  if (currentState == ALARM && irReady) {
    if (digitalRead(IR_PIN) == LOW) {
      Serial.println("[IR] 손 감지 -> 복용 완료");
      stopAlarm();
      writeTaken(activeSlot);
      currentState = TAKEN;
      updateLCD(now);
      delay(3000);
      currentState = STANDBY;
    }
  }

  // ALARM: 10분 경과 -> 미복용
  if (currentState == ALARM && activeSlot >= 0) {
    if ((millis() - alarmStart) >= MISSED_TIMEOUT) {
      Serial.printf("[TIMEOUT] %s 미복용\n", slotNames[activeSlot]);
      stopAlarm();
      writeMissed(activeSlot);
      currentState = STANDBY;
      activeSlot   = -1;
    }
  }

  // 매 분 스케줄 체크 (시간 기반 자동 배출)
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

  // 수동 배출 명령 폴링 (앱 버튼)
  if (millis() - lastCommandCheck >= COMMAND_CHECK_INTERVAL) {
    lastCommandCheck = millis();
    checkManualDispense();
  }

  // 자정 리셋
  if (now.hour() == 0 && now.minute() == 0 && now.second() == 0) {
    fetchSchedule();
    Serial.println("[RESET] 자정 스케줄 갱신");
  }

  updateLCD(now);
  delay(200);
}
