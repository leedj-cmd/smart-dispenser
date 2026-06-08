# 세달의 기적 — 스마트 약 복용 디스펜서

치매 어르신을 위한 IoT 약 복용 관리 시스템. 보호자가 Flutter 앱으로 복약 스케줄을
설정하면, ESP32 디스펜서가 정해진 시간에 약을 배출하고 복용 여부(IR 감지)를 보고합니다.

---

## 시스템 구조

```
┌──────────────┐   write/read    ┌──────────────────────┐
│  Flutter 앱  │ ◄─────────────► │  Firebase RTDB       │
│ (보호자)     │                 │  (asia-southeast1)   │
└──────────────┘                 └──────────┬───────────┘
                                            │  poll (1s/30s) · REST
                                            ▼
                                 ┌──────────────────────┐
                                 │  bridge.py (Mac)     │
                                 │  Firebase ⇄ Serial   │
                                 └──────────┬───────────┘
                                            │  USB Serial 115200
                                            ▼
                                 ┌──────────────────────┐
                                 │  ESP32 펌웨어        │
                                 │  스텝모터·RTC·LCD·IR │
                                 └──────────────────────┘
```

> **중요**: ESP32는 Wi-Fi/Firebase에 직접 연결하지 않습니다. 호스트(Mac)에서 도는
> `bridge.py`가 Firebase를 폴링해 시리얼 명령으로 변환하고, 디바이스 이벤트를 다시
> Firebase에 기록합니다. (설계 초안의 `Firebase_ESP_Client` 직결 방식에서 변경됨)

---

## 주요 기능

| 기능 | 설명 | 상태 |
|---|---|---|
| 이메일 로그인 / 회원가입 | Firebase Authentication 기반 | ✅ |
| 자동 로그인 상태 감지 | `AuthGate` + `authStateChanges` 스트림 | ✅ |
| 오늘 날짜 / 요일 표시 | 홈 화면 상단 | ✅ |
| 즉시 배출 (아침/점심/저녁) | 버튼 → `dispense/command` → 디바이스 즉시 배출 | ✅ |
| 아침 / 점심 / 저녁 스케줄 설정 | 시간대별 활성 토글, TimePicker | ✅ |
| 약 추가 / 이름 수정 / 정수 조절 / 삭제 | 시간대별 복용 약 관리 | ✅ |
| 변경사항 미저장 경고 | 뒤로가기 시 다이얼로그 | ✅ |
| 스텝모터 배출 + RTC 스케줄 실행 | 28BYJ-48, 8슬롯 회전판 | ✅ |
| 복용 감지 (IR) + 미복용 타임아웃 | `taken` / `missed` 이벤트 보고 | ✅ |
| 복용 기록 조회 | 디바이스 실제 복용 이력 화면 | 🚧 예정 |
| 디스펜서 설정 | 디바이스 등록 / 다중 디바이스 | 🚧 예정 |
| FCM 푸시 알림 | `firebase_messaging` 의존성 추가됨 | 🚧 예정 |

---

## 기술 스택

**앱**
- **Flutter** — Material 3, SDK `^3.11.5`
- **Firebase**
  - `firebase_core` ^4.7.0
  - `firebase_auth` ^6.4.0
  - `firebase_database` ^12.3.0 — 스케줄 / 사용자 / 배출 명령
  - `firebase_messaging` ^16.2.0 — 알림 (예정)
- **플랫폼**: Android · iOS · Web · Windows · macOS · Linux

**펌웨어 / 브리지**
- **ESP32** (`esp32dev`) + PlatformIO + Arduino framework
- 라이브러리: `RTClib`, `Adafruit BusIO`, `LiquidCrystal_I2C`
- **bridge.py** — Python (`~/venv`), `requests` + `pyserial`

**하드웨어**
- 28BYJ-48 스텝모터 + ULN2003 드라이버 (8슬롯 회전판)
- DS3231 RTC, I2C 16x2 LCD, IR 근접 센서, 부저, LED

---

## 프로젝트 구조

```
.
├── app/                      # Flutter 보호자 앱
│   └── lib/
│       ├── main.dart              # 진입점 + AuthGate (로그인 상태 자동 라우팅)
│       ├── firebase_options.dart  # Firebase 플랫폼별 설정 (flutterfire configure 생성)
│       ├── auth_service.dart      # Firebase Auth 래퍼 + 한글 에러 메시지
│       ├── login_screen.dart      # 이메일/비밀번호 로그인
│       ├── signup_screen.dart     # 회원가입
│       ├── home_screen.dart       # 홈: 날짜 · 즉시 배출 버튼 · 관리 메뉴
│       └── schedule_screen.dart   # 약 복용 스케줄 (아침/점심/저녁)
│
├── firmware/                 # ESP32 (PlatformIO)
│   ├── platformio.ini
│   ├── src/main.cpp               # 디스펜서 펌웨어 (시리얼 브리지 모드)
│   └── bridge/bridge.py           # Firebase ⇄ ESP32 시리얼 브리지 (Mac에서 실행)
│
├── hardware/                 # 회로도 / BOM
├── 3d-models/                # 디스펜서 3D 모델
└── docs/
    └── Firebase_데이터구조_설계서.md
```

---

## Firebase Realtime Database 스키마

```jsonc
{
  "users": {
    "{uid}": {
      "name": "홍길동",
      "email": "guardian@example.com"
    }
  },

  "schedules": {
    "device_001": {                 // 현재 단일 디바이스 하드코딩 (kDeviceId)
      "morning": {
        "time": "08:00",
        "enabled": true,
        "medications": [
          { "name": "혈압약", "count": 1 },
          { "name": "당뇨약", "count": 2 }
        ],
        "dispensed": true,          // 디바이스 → 배출 완료 (브리지가 기록)
        "taken": true,              // 디바이스 → IR 복용 감지
        "missed": false             // 디바이스 → 미복용 타임아웃
      },
      "lunch":  { "time": "12:00", "enabled": true,  "medications": [] },
      "dinner": { "time": "18:00", "enabled": false, "medications": [] },
      "updatedAt": 1715731200000    // 앱 저장 시각
    }
  },

  "dispense": {
    "device_001": {
      "command": "morning"          // 앱이 즉시 배출 요청 → 브리지가 읽고 ""로 클리어
    }
  }
}
```

- 앱은 `schedules/{deviceId}` 와 `dispense/{deviceId}/command` 에 쓰기.
- 브리지는 `dispense/command` 를 1초 폴링, `schedules` 를 30초 폴링해 시리얼로 전달.
- 이벤트 플래그(`dispensed`/`taken`/`missed`)는 브리지가 디바이스 이벤트를 받아 기록.

---

## 펌웨어 ↔ 브리지 시리얼 프로토콜 (115200)

**호스트 → ESP32 (명령)**

| 명령 | 형식 | 설명 |
|---|---|---|
| `TIME` | `TIME YYYY-MM-DDTHH:MM:SS` | RTC 시각 동기화 (브리지 시작 시) |
| `SCHED` | `SCHED <slot> HH:MM <0\|1>` | 스케줄 시간·활성 갱신 |
| `DISPENSE` | `DISPENSE <slot>` | 즉시 배출 (slot: morning/lunch/dinner) |
| `PING` | `PING` | 헬스체크 → `PONG` |

**ESP32 → 호스트 (이벤트/로그)**

| 출력 | 설명 |
|---|---|
| `EVT DISPENSED <slot>` | 약 배출 완료 |
| `EVT TAKEN <slot>` | IR 복용 감지 |
| `EVT MISSED <slot>` | 미복용 타임아웃 (10분) |
| `LOG ...` / `PONG` / `READY` | 디버그 로그 / 응답 / 부팅 완료 |

### 하드웨어 핀맵 (ESP32)

| 부품 | 핀 |
|---|---|
| 스텝모터 (ULN2003 IN1~IN4) | 18, 19, 23, 5 |
| I2C (RTC + LCD) SDA/SCL | 21 / 22 |
| IR 센서 | 14 |
| 부저 | 25 |
| LED | 26 |

> 회전판 8슬롯. 한 번 배출 시 현재 위치에서 +45°(한 칸) 회전. 자정에 `dispensed`
> 플래그 리셋. 미복용 타임아웃 10분.

---

## 시작하기

### 1. 앱 (Flutter)

```bash
git clone https://github.com/sedal-miracle-team/main.git
cd main/app

flutter pub get

# Firebase 설정 (firebase_options.dart 생성)
dart pub global activate flutterfire_cli
flutterfire configure

flutter run
```

**Firebase 콘솔 설정**
1. **Authentication** → "이메일/비밀번호" 로그인 활성화
2. **Realtime Database** 생성 (region: `asia-southeast1`), 아래 규칙 적용

```json
{
  "rules": {
    "users":    { "$uid": { ".read": "$uid === auth.uid", ".write": "$uid === auth.uid" } },
    "schedules":{ "$deviceId": { ".read": "auth != null", ".write": "auth != null" } },
    "dispense": { "$deviceId": { ".read": "auth != null", ".write": "auth != null" } }
  }
}
```

### 2. 펌웨어 (ESP32)

```bash
cd firmware
pio run --target upload      # 빌드 + 업로드
pio device monitor           # 시리얼 모니터 (115200)
```

### 3. 브리지 (Mac)

```bash
# ESP32 포트 확인 후 실행
export ESP32_PORT=/dev/cu.usbserial-1140
~/venv/bin/python firmware/bridge/bridge.py
```

> 브리지의 `DB_URL` / `DB_AUTH` / `DEVICE` 는 `bridge.py` 상단 상수로 설정되어 있음.

---

## 개발 메모

- `kDeviceId = 'device_001'` — `home_screen.dart` · `schedule_screen.dart` 상단에
  하드코딩. 디바이스 등록 화면 구현 시 동적 변경 예정.
- `home_screen.dart` 에 RTDB 원본 데이터 확인용 **🔧 개발자 도구** 섹션이 있음.
- 인증 에러 메시지는 `AuthService.getErrorMessage()` 에서 한글로 매핑됨.
- 즉시 배출은 `dispense/{deviceId}/command` 에 slot 문자열을 쓰면 브리지가 읽고
  `DISPENSE` 명령으로 변환 후 값을 `""` 로 클리어.
- 펌웨어는 Firebase에 직접 붙지 않음. 모든 통신은 `bridge.py` 시리얼 중계 경유.

---

## 향후 작업

- [ ] 복용 기록 화면 (`taken`/`missed`/`dispensed` 이벤트 시각화)
- [ ] 디스펜서 등록 / 다중 디바이스 지원 (`kDeviceId` 동적화)
- [ ] FCM 푸시 알림 (미복용 시 보호자에게 경고)
- [ ] 브리지 자격증명을 코드 상수 → 환경변수/시크릿으로 분리
- [ ] 다중 보호자 (한 디바이스에 여러 계정 연결)

---

## License

내부 팀 프로젝트.
</content>
</invoke>
