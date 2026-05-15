
  ## 주요 기능                                                         
   
  | 기능 | 설명 | 상태 |                                               
  |---|---|---|                                                      
  | 이메일 로그인 / 회원가입 | Firebase Authentication 기반 | ✅ |
  | 자동 로그인 상태 감지 | `AuthGate` + `authStateChanges` 스트림 | ✅
   |                                                                   
  | 아침 / 점심 / 저녁 스케줄 설정 | 시간대별 활성 토글, TimePicker |  
  ✅ |                                                                 
  | 약 추가 / 이름 수정 / 정수 조절 / 삭제 | 시간대별 복용 약 관리 | ✅
   |                                                                   
  | 변경사항 미저장 경고 | 뒤로가기 시 다이얼로그 | ✅ |             
  | 복용 기록 조회 | 디바이스 실제 복용 이력 | 🚧 예정 |               
  | 디스펜서 설정 | 디바이스 등록 / 다중 디바이스 | 🚧 예정 |          
  | FCM 푸시 알림 | `firebase_messaging` 의존성 추가됨 | 🚧 예정 |     
                                                                       
  ---                                                                  
                                                                       
  ## 기술 스택                                                       

  - **Flutter** — Material 3, SDK `^3.11.5`                            
  - **Firebase**
    - `firebase_core` ^4.7.0                                           
    - `firebase_auth` ^6.4.0                                           
    - `firebase_database` ^12.3.0 — 스케줄 / 사용자 데이터
    - `firebase_messaging` ^16.2.0 — 알림 (예정)                       
  - **플랫폼**: Android · iOS · Web · Windows · macOS · Linux        
                                                                       
  ---                                                                
                                                                       
  ## 프로젝트 구조                                                   

  ```
  app/
  ├── lib/
  │   ├── main.dart              # 진입점 + AuthGate (로그인 상태 자동 
  라우팅)                                                              
  │   ├── firebase_options.dart  # Firebase 플랫폼별 설정 (flutterfire 
  configure로 생성)                                                    
  │   ├── auth_service.dart      # Firebase Auth 래퍼 + 한글 에러    
  메시지                                                               
  │   ├── login_screen.dart      # 이메일/비밀번호 로그인            
  │   ├── signup_screen.dart     # 회원가입 (이름·이메일·비밀번호·확인)
  │   ├── home_screen.dart       # 메인 메뉴 + RTDB 실시간 환영 메시지
  │   └── schedule_screen.dart   # 약 복용 스케줄 (아침/점심/저녁)     
  ├── android/  ios/  web/  windows/  macos/  linux/                   
  ├── test/                                                            
  ├── pubspec.yaml                                                     
  ├── firebase.json                                                  
  └── analysis_options.yaml
  ```                                                                  
   
  ### 화면 흐름                                                        
                                                                     
  ```
         ┌─────────────┐
         │  main.dart  │
         └──────┬──────┘
                ▼                                                      
         ┌─────────────┐  로그인 X   ┌──────────────┐
         │  AuthGate   │────────────►│ LoginScreen  │◄──┐              
         └──────┬──────┘             └──────┬───────┘   │              
                │ 로그인 O                  │            │             
                ▼                          ▼            │              
         ┌─────────────┐             ┌──────────────┐  │             
         │ HomeScreen  │             │ SignupScreen │──┘               
         └──────┬──────┘             └──────────────┘                
                ▼                                                      
         ┌──────────────────┐                                          
         │ ScheduleScreen   │
         └──────────────────┘                                          
  ```                                                                

  ---

  ## Firebase Realtime Database 스키마                                 
   
  ```jsonc                                                             
  {                                                                  
    "users": {
      "{uid}": {
        "name": "홍길동",
        "email": "guardian@example.com",                               
        "createdAt": 1715731200000
      }                                                                
    },                                                               
    "schedules": {                                                     
      "device_001": {                 // 현재 단일 디바이스 하드코딩 
        "morning": {                                                   
          "time": "08:00",
          "enabled": true,                                             
          "medications": [                                           
            { "name": "혈압약", "count": 1 },
            { "name": "당뇨약", "count": 2 }                           
          ]
        },                                                             
        "lunch":  { "time": "12:00", "enabled": true,  "medications":
  [] },                                                                
        "dinner": { "time": "18:00", "enabled": false, "medications":
  [] },                                                                
        "updatedAt": 1715731200000    // 펌웨어가 변경 감지에 사용   
      }                                                                
    }
  }                                                                    
  ```                                                                

  > 펌웨어가 `schedules/{deviceId}`를 구독하여 시간이 되면 약을        
  배출합니다.
                                                                       
  ---                                                                

  ## 시작하기

  ### 사전 요구사항                                                    
   
  - Flutter SDK `^3.11.5`                                              
  - Firebase CLI + FlutterFire CLI                                   
  - Firebase 프로젝트 (Authentication / Realtime Database 활성화)      
                                                                       
  ### 설치                                                             
                                                                       
  ```bash                                                            
  # 1. 클론
  git clone https://github.com/sedal-miracle-team/main.git
  cd main/app                                                          
   
  # 2. 의존성 설치                                                     
  flutter pub get                                                    

  # 3. Firebase 설정 (firebase_options.dart 생성)
  dart pub global activate flutterfire_cli                             
  flutterfire configure
                                                                       
  # 4. 실행                                                          
  flutter run
  ```                                                                  
   
  ### Firebase 콘솔 설정                                               
                                                                     
  1. **Authentication** → "이메일/비밀번호" 로그인 활성화              
  2. **Realtime Database** 생성 (테스트 모드 또는 아래 규칙 적용)
                                                                       
  ```json                                                            
  {                                                                    
    "rules": {                                                       
      "users": {
        "$uid": {
          ".read": "$uid === auth.uid",
          ".write": "$uid === auth.uid"                                
        }
      },                                                               
      "schedules": {                                                 
        "$deviceId": {
          ".read": "auth != null",
          ".write": "auth != null"
        }                                                              
      }
    }                                                                  
  }                                                                  
  ```

  ---

  ## 개발 메모

  - `kDeviceId = 'device_001'` — `schedule_screen.dart` 상단에         
  하드코딩되어 있음. 디바이스 등록 화면 구현 시 동적 변경 예정.
  - `home_screen.dart`에 RTDB 원본 데이터 확인용 **🔧 개발자 도구**    
  섹션이 있음.                                                         
  - 인증 에러 메시지는 `AuthService.getErrorMessage()`에서 한글로
  매핑됨.                                                              
  - 스케줄 저장 시 `ServerValue.timestamp`로 `updatedAt` 갱신 →      
  펌웨어가 폴링 대신 변경 감지 가능.                                   
                                                                     
  ---                                                                  
                                                                     
  ## 향후 작업                                                         
   
  - [ ] 복용 기록 화면 (디바이스가 보고한 실제 복용 이벤트 표시)       
  - [ ] 디스펜서 등록 / 다중 디바이스 지원 (`kDeviceId` 동적화)      
  - [ ] FCM 푸시 알림 (미복용 시 보호자에게 경고)                      
  - [ ] 다중 보호자 (한 디바이스에 여러 계정 연결)                     
                                                                       
  ---                                                                  
                                                                       
  ## License                                                         

  내부 팀 프로젝트.                                        
