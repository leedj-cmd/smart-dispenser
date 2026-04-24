import 'package:firebase_auth/firebase_auth.dart';
import 'package:firebase_database/firebase_database.dart';

class AuthService {
  AuthService({FirebaseAuth? firebaseAuth, FirebaseDatabase? database})
    : _auth = firebaseAuth ?? FirebaseAuth.instance,
      _db = database ?? FirebaseDatabase.instance;

  final FirebaseAuth _auth;
  final FirebaseDatabase _db;

  // 로그인 상태 변화 스트림 (main.dart에서 사용)
  Stream<User?> get authStateChanges => _auth.authStateChanges();

  User? get currentUser => _auth.currentUser;

  bool get isSignedIn => currentUser != null;

  // 로그인
  Future<UserCredential> signInWithEmail({
    required String email,
    required String password,
  }) {
    return _auth.signInWithEmailAndPassword(
      email: email.trim(),
      password: password,
    );
  }

  // 회원가입 (Auth + Realtime DB 둘 다 저장)
  Future<UserCredential> signUpWithEmail({
    required String email,
    required String password,
    required String name,
  }) async {
    // 1단계: Firebase Auth에 계정 생성
    final cred = await _auth.createUserWithEmailAndPassword(
      email: email.trim(),
      password: password,
    );

    // 2단계: Realtime DB의 /users/{uid}에 추가 정보 저장
    final uid = cred.user!.uid;
    await _db.ref('users/$uid').set({
      'name': name.trim(),
      'email': email.trim(),
      'createdAt': ServerValue.timestamp,
      // 추후 phoneNumber, patientName, deviceId 등 추가
    });

    return cred;
  }

  // 비밀번호 재설정 메일 전송
  Future<void> sendPasswordResetEmail(String email) {
    return _auth.sendPasswordResetEmail(email: email.trim());
  }

  // 로그아웃
  Future<void> signOut() {
    return _auth.signOut();
  }

  // 한국어 에러 메시지 변환
  static String getErrorMessage(Object error) {
    if (error is FirebaseAuthException) {
      switch (error.code) {
        case 'user-not-found':
          return '가입되지 않은 이메일입니다.';
        case 'wrong-password':
        case 'invalid-credential':
          return '이메일 또는 비밀번호가 틀렸습니다.';
        case 'invalid-email':
          return '이메일 형식이 올바르지 않습니다.';
        case 'email-already-in-use':
          return '이미 가입된 이메일입니다.';
        case 'weak-password':
          return '비밀번호는 6자 이상이어야 합니다.';
        case 'network-request-failed':
          return '인터넷 연결을 확인해주세요.';
        case 'too-many-requests':
          return '너무 많이 시도했습니다. 잠시 후 다시 시도해주세요.';
        case 'keychain-error':
          return '계정은 생성됐지만 macOS 키체인 저장에 실패했습니다. (Android에서는 정상 작동)';
        default:
          return '오류가 발생했습니다: ${error.message ?? error.code}';
      }
    }
    return '알 수 없는 오류: $error';
  }
}
