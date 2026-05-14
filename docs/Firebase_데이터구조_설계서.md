# Firebase 데이터 구조 설계서

**프로젝트**: 치매어르신을 위한 스마트 IoT 약 복용 관리 디스펜서  
**팀**: 세달의 기적  
**문서 목적**: 보호자 앱(Flutter)과 ESP32 펌웨어 간 Firebase를 통한 데이터 교환 명세

---

## 1. 사용할 Firebase 서비스

| 서비스 | 용도 | 비용 |
|---|---|---|
| **Realtime Database** | 스케줄·이력 실시간 동기화 | 무료 (1GB 저장, 10GB/월 전송) |
| **Authentication** | 보호자 로그인 (이메일/비번 또는 Google) | 무료 |
| **Cloud Messaging (FCM)** | 미복용 시 보호자에게 푸시 알림 | 완전 무료 |
| **Cloud Functions** (선택) | 미복용 감지 시 자동 푸시 트리거 | 무료 (월 200만 호출) |

### 왜 Firestore가 아니라 Realtime Database인가
- ESP32용 라이브러리(`Firebase_ESP_Client`)가 Realtime DB 쪽이 훨씬 안정적
- 실시간 stream listening 지원 → 스케줄 변경 즉시 ESP32에 반영
- 데이터 구조가 단순해서 캡스톤 규모에 적합

---

## 2. 전체 데이터 트리 (한눈에)

```
sedal-miracle/  (Firebase 프로젝트 루트)
│
├── users/
│   └── {uid}/
│       ├── email
│       ├── name
│       ├── phone
│       ├── fcmToken
│       └── linkedDeviceId
│
├── devices/
│   └── {deviceId}/
│       ├── ownerId
│       ├── patientName
│       ├── status/
│       │   ├── online
│       │   ├── lastSeen
│       │   └── wifiSignal
│       └── currentTime
│
├── schedules/
│   └── {deviceId}/
│       ├── morning/
│       ├── lunch/
│       └── dinner/
│
├── history/
│   └── {deviceId}/
│       └── {YYYY-MM-DD}/
│           └── {sessionId}/
│
└── notifications/
    └── {uid}/
        └── {notifId}/
```

---

## 3. 노드별 상세 명세

### 3.1 `users/{uid}`
보호자 계정 정보. Firebase Authentication이 발급한 `uid`를 키로 사용.

| 필드 | 타입 | 필수 | 설명 | 예시 |
|---|---|---|---|---|
| `email` | string | ✅ | 로그인 이메일 | "park@gmail.com" |
| `name` | string | ✅ | 보호자 이름 | "박철수" |
| `phone` | string | ❌ | 연락처 | "010-1234-5678" |
| `fcmToken` | string | ✅ | FCM 푸시 토큰 (앱이 자동 등록) | "dGhpcyBpcyB..." |
| `linkedDeviceId` | string | ✅ | 연결된 디스펜서 ID | "dispenser-001" |

### 3.2 `devices/{deviceId}`
디스펜서 본체 정보. `deviceId`는 ESP32의 MAC 주소 기반으로 생성 (예: "dispenser-A4CF12").

| 필드 | 타입 | 쓰기 주체 | 설명 |
|---|---|---|---|
| `ownerId` | string | 앱 | 보호자 uid |
| `patientName` | string | 앱 | 어르신 성함 |
| `status.online` | boolean | ESP32 | Wi-Fi 연결 여부 (heartbeat) |
| `status.lastSeen` | timestamp | ESP32 | 마지막 통신 시각 (ms) |
| `status.wifiSignal` | number | ESP32 | Wi-Fi 신호 강도 (dBm) |
| `currentTime` | string | ESP32 | RTC 현재 시각 ("YYYY-MM-DD HH:MM:SS") |

### 3.3 `schedules/{deviceId}/{slot}`
복약 스케줄. `slot`은 `morning` / `lunch` / `dinner` 셋 중 하나.

| 필드 | 타입 | 쓰기 주체 | 설명 | 예시 |
|---|---|---|---|---|
| `time` | string | 앱 | "HH:MM" 형식 | "08:00" |
| `enabled` | boolean | 앱 | 활성화 여부 | true |
| `pillCount` | number | 앱 | 몇 알 | 2 |
| `medicineName` | string | 앱 | 약 이름 | "혈압약" |

### 3.4 `history/{deviceId}/{YYYY-MM-DD}/{sessionId}`
복약 이력. 날짜별로 묶고, 세션 단위로 기록. `sessionId` = `"{slot}_{seq}"` (예: `"morning_001"`).

| 필드 | 타입 | 쓰기 주체 | 설명 |
|---|---|---|---|
| `scheduledTime` | string | ESP32 | 예정된 복용 시각 ("12:30") |
| `slot` | enum | ESP32 | "morning" / "lunch" / "dinner" |
| `dispensedAt` | timestamp | ESP32 | 약이 배출된 시각 (ms) |
| `takenAt` | timestamp \| null | ESP32 | 손이 감지된 시각, 미복용은 null |
| `status` | enum | ESP32 | "pending" / "taken" / "missed" |
| `medicineName` | string | ESP32 | 약 이름 (스케줄에서 복사) |

### 3.5 `notifications/{uid}/{notifId}`
보호자가 앱에서 보는 알림 목록. (FCM은 일회성 푸시, 이건 앱 안의 알림함)

| 필드 | 타입 | 설명 |
|---|---|---|
| `type` | enum | "missed" / "low_battery" / "offline" |
| `message` | string | "어머니가 점심약을 드시지 않았어요" |
| `timestamp` | timestamp | 발생 시각 |
| `read` | boolean | 읽음 여부 |

---

## 4. 데이터 흐름 시나리오

### 시나리오 A: 보호자 첫 가입 + 디바이스 등록
1. 앱 → Firebase Auth 회원가입 → `uid` 발급
2. 앱 → `users/{uid}` 노드 생성 (이름, 폰번호 등)
3. 앱 → 디스펜서 QR 코드 스캔으로 `deviceId` 획득
4. 앱 → `devices/{deviceId}/ownerId = uid` 저장
5. 앱 → `users/{uid}/linkedDeviceId = deviceId` 저장

### 시나리오 B: 보호자가 복약 시간 설정
1. 앱에서 "아침 8:00, 혈압약 2알" 입력 + 저장
2. 앱 → `schedules/{deviceId}/morning` 에 write
3. ESP32가 `schedules/{deviceId}` 노드를 stream listening 중
4. ESP32 즉시 반영 → LCD에 "다음 복약: 08:00" 갱신

### 시나리오 C: 복약 시간 도달 + 정상 복용
1. ESP32: RTC 현재 시각 == 스케줄 시각 감지
2. ESP32: 회전판 회전 + 부저 울림 + LCD "약 드세요"
3. ESP32 → `history/{deviceId}/{오늘날짜}/{sessionId}` 생성  
   (`status: "pending"`, `dispensedAt: now`)
4. 어르신이 약 가지러 손 뻗음 → IR 센서 감지
5. ESP32 → 같은 session 업데이트 (`status: "taken"`, `takenAt: now`)
6. 보호자 앱은 `history` 를 listen 중이므로 자동 갱신

### 시나리오 D: 미복용 → 보호자 알림
1. ESP32: 약 배출 후 일정 시간(예: 5분) 경과, IR 미감지
2. ESP32 → 해당 session `status: "missed"`로 업데이트
3. **자동 푸시 발송 방식 2가지 중 택1**:
   - **방식 1**: Cloud Function이 `status` 필드 변경을 trigger로 잡아 FCM 자동 발송
   - **방식 2**: ESP32가 직접 보호자 `fcmToken`을 조회해서 FCM HTTP API 호출
4. 보호자 폰: 백그라운드 푸시 수신 → "어머니가 점심약을 드시지 않았어요"
5. ESP32 → `notifications/{ownerId}` 에도 기록 (앱 알림함용)

> **권장**: 방식 1(Cloud Function)이 더 깔끔하지만, Cloud Function 학습 시간이 들어요. 일정 빠듯하면 방식 2로 시작하고 나중에 리팩토링.

---

## 5. 보안 규칙 (Security Rules)

```json
{
  "rules": {
    "users": {
      "$uid": {
        ".read": "$uid === auth.uid",
        ".write": "$uid === auth.uid"
      }
    },
    "devices": {
      "$deviceId": {
        ".read": "data.child('ownerId').val() === auth.uid",
        ".write": "data.child('ownerId').val() === auth.uid || !data.exists()"
      }
    },
    "schedules": {
      "$deviceId": {
        ".read": "root.child('devices').child($deviceId).child('ownerId').val() === auth.uid",
        ".write": "root.child('devices').child($deviceId).child('ownerId').val() === auth.uid"
      }
    },
    "history": {
      "$deviceId": {
        ".read": "root.child('devices').child($deviceId).child('ownerId').val() === auth.uid"
      }
    },
    "notifications": {
      "$uid": {
        ".read": "$uid === auth.uid",
        ".write": "$uid === auth.uid"
      }
    }
  }
}
```

> **ESP32는 어떻게 인증하나?**  
> ESP32는 Firebase가 발급한 **Database Secret** 또는 **Service Account JSON 키**를 코드에 박아서 사용. 이 경우 보안 규칙 우회 가능 (admin 권한). 캡스톤이라 Database Secret으로 충분.

---

## 6. ESP32 ↔ Firebase 통신 라이브러리

**권장 라이브러리**: `Firebase_ESP_Client` by mobizt  
- GitHub: https://github.com/mobizt/Firebase-ESP-Client  
- Arduino IDE 라이브러리 매니저에서 검색 후 설치
- `setStreamCallback()` 으로 Realtime DB 변경 실시간 감지 가능

**핵심 코드 패턴 (이동제님 참고)**:
```cpp
// 스케줄 변경 감지
Firebase.RTDB.beginStream(&stream, "/schedules/dispenser-001");
Firebase.RTDB.setStreamCallback(&stream, streamCallback, streamTimeoutCallback);

// 이력 기록
FirebaseJson historyJson;
historyJson.set("scheduledTime", "08:00");
historyJson.set("slot", "morning");
historyJson.set("dispensedAt", millis());
historyJson.set("status", "pending");
Firebase.RTDB.setJSON(&fbdo, "/history/dispenser-001/2026-04-24/morning_001", &historyJson);
```

---

## 7. 실제 JSON 예시 (운영 중인 디바이스 1대 기준)

```json
{
  "users": {
    "user_abc123": {
      "email": "park@gmail.com",
      "name": "박철수",
      "phone": "010-1234-5678",
      "fcmToken": "dGhpcyBpcyB...",
      "linkedDeviceId": "dispenser-001"
    }
  },
  "devices": {
    "dispenser-001": {
      "ownerId": "user_abc123",
      "patientName": "박순자",
      "status": {
        "online": true,
        "lastSeen": 1714000000000,
        "wifiSignal": -65
      },
      "currentTime": "2026-04-24 14:30:15"
    }
  },
  "schedules": {
    "dispenser-001": {
      "morning": { "time": "08:00", "enabled": true, "pillCount": 2, "medicineName": "혈압약" },
      "lunch":   { "time": "12:30", "enabled": true, "pillCount": 1, "medicineName": "혈당약" },
      "dinner":  { "time": "19:00", "enabled": true, "pillCount": 2, "medicineName": "혈압약" }
    }
  },
  "history": {
    "dispenser-001": {
      "2026-04-24": {
        "morning_001": {
          "scheduledTime": "08:00",
          "slot": "morning",
          "dispensedAt": 1714000000000,
          "takenAt": 1714000020000,
          "status": "taken",
          "medicineName": "혈압약"
        },
        "lunch_001": {
          "scheduledTime": "12:30",
          "slot": "lunch",
          "dispensedAt": 1714014600000,
          "takenAt": null,
          "status": "missed",
          "medicineName": "혈당약"
        }
      }
    }
  }
}
```

---

## 8. 봉준표 ↔ 이동제 합의 체크리스트

코드 시작 전에 두 분이 ✅ 표시 완료해야 합니다.

- [ ] 모든 timestamp는 **milliseconds (UTC 기준 epoch ms)** 사용
- [ ] `deviceId` 형식은 `"dispenser-{MAC뒤6자리}"` (예: `"dispenser-A4CF12"`)
- [ ] `sessionId` 형식은 `"{slot}_{3자리시퀀스}"` (예: `"morning_001"`)
- [ ] `status` enum 값은 정확히 `"pending"` / `"taken"` / `"missed"` (대소문자, 오타 주의)
- [ ] `slot` enum 값은 정확히 `"morning"` / `"lunch"` / `"dinner"`
- [ ] 미복용 판정 기준 시간: **5분** (회의에서 변경 가능)
- [ ] 한쪽이 스키마 변경할 때는 반드시 **양쪽 합의 후** 이 문서 갱신
- [ ] Firebase 프로젝트 권한: 봉준표(소유자) + 이동제(편집자)

---

## 9. 단계별 작업 분담

### 봉준표 (앱 + Firebase 설정)
1. Firebase 콘솔에서 프로젝트 생성 (`sedal-miracle`)
2. Realtime Database 활성화 + 보안 규칙 적용
3. Authentication 활성화 (이메일/비번)
4. Flutter 앱: Auth 화면 → 디바이스 등록 → 스케줄 설정 → 이력 조회 → 푸시 수신

### 이동제 (펌웨어)
1. ESP32에 `Firebase_ESP_Client` 라이브러리 설치
2. Wi-Fi 연결 + Firebase 인증
3. `schedules/{deviceId}` stream listening 구현
4. 시간 도달 시 회전판 회전 + 이력 write
5. IR 감지 시 status 업데이트
6. 미복용 타이머 + 알림 트리거

---

**문서 버전**: V1 (2026-04-24)  
**작성**: Claude (멘토)  
**검토 요청**: 봉준표 (팀장), 이동제 (펌웨어)
