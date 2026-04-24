import 'package:firebase_database/firebase_database.dart';
import 'package:flutter/material.dart';
import 'auth_service.dart';
import 'schedule_screen.dart';

class HomeScreen extends StatelessWidget {
  const HomeScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final authService = AuthService();
    final user = authService.currentUser;

    return Scaffold(
      appBar: AppBar(
        title: const Text('세달의 기적'),
        actions: [
          IconButton(
            icon: const Icon(Icons.logout),
            tooltip: '로그아웃',
            onPressed: () async {
              await authService.signOut();
              // StreamBuilder가 자동으로 LoginScreen으로 전환함
            },
          ),
        ],
      ),
      body: SingleChildScrollView(
        padding: const EdgeInsets.all(24.0),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            // 환영 메시지 (이름은 RTDB에서 실시간으로)
            if (user != null)
              StreamBuilder<DatabaseEvent>(
                stream: FirebaseDatabase.instance
                    .ref('users/${user.uid}/name')
                    .onValue,
                builder: (context, snapshot) {
                  final name = snapshot.data?.snapshot.value as String?;
                  return Container(
                    padding: const EdgeInsets.all(20),
                    decoration: BoxDecoration(
                      gradient: LinearGradient(
                        colors: [
                          Colors.deepPurple.shade400,
                          Colors.deepPurple.shade700,
                        ],
                      ),
                      borderRadius: BorderRadius.circular(16),
                    ),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(
                          '안녕하세요, ${name ?? "보호자"}님 👋',
                          style: const TextStyle(
                            color: Colors.white,
                            fontSize: 22,
                            fontWeight: FontWeight.bold,
                          ),
                        ),
                        const SizedBox(height: 4),
                        Text(
                          user.email ?? '',
                          style: TextStyle(
                            color: Colors.white.withValues(alpha: 0.8),
                            fontSize: 14,
                          ),
                        ),
                      ],
                    ),
                  );
                },
              ),
            const SizedBox(height: 24),

            // 메뉴 섹션
            const Text(
              '관리',
              style: TextStyle(fontSize: 16, fontWeight: FontWeight.bold),
            ),
            const SizedBox(height: 12),

            // 약 복용 스케줄 카드 (★ 메인 기능)
            _buildMenuCard(
              context: context,
              icon: Icons.schedule,
              iconColor: Colors.deepPurple,
              title: '약 복용 스케줄',
              subtitle: '아침/점심/저녁 복용 시간과 약 설정',
              onTap: () => Navigator.of(context).push(
                MaterialPageRoute(builder: (_) => const ScheduleScreen()),
              ),
            ),
            const SizedBox(height: 12),

            // 복용 기록 (추후 구현)
            _buildMenuCard(
              context: context,
              icon: Icons.history,
              iconColor: Colors.green,
              title: '복용 기록',
              subtitle: '어르신의 복용 이력 확인 (개발 예정)',
              onTap: () {
                ScaffoldMessenger.of(context).showSnackBar(
                  const SnackBar(content: Text('곧 만들어질 예정입니다')),
                );
              },
              enabled: false,
            ),
            const SizedBox(height: 12),

            // 디바이스 설정 (추후 구현)
            _buildMenuCard(
              context: context,
              icon: Icons.devices,
              iconColor: Colors.blue,
              title: '디스펜서 설정',
              subtitle: '디바이스 연결 및 정보 (개발 예정)',
              onTap: () {
                ScaffoldMessenger.of(context).showSnackBar(
                  const SnackBar(content: Text('곧 만들어질 예정입니다')),
                );
              },
              enabled: false,
            ),
            const SizedBox(height: 32),

            // 디버그용: RTDB 데이터 확인
            ExpansionTile(
              title: const Text(
                '🔧 개발자 도구',
                style: TextStyle(fontSize: 12, color: Colors.grey),
              ),
              children: [
                if (user != null)
                  StreamBuilder<DatabaseEvent>(
                    stream: FirebaseDatabase.instance
                        .ref('users/${user.uid}')
                        .onValue,
                    builder: (context, snapshot) {
                      final data = snapshot.data?.snapshot.value;
                      return Container(
                        padding: const EdgeInsets.all(12),
                        margin: const EdgeInsets.symmetric(horizontal: 8),
                        decoration: BoxDecoration(
                          color: Colors.grey.shade100,
                          borderRadius: BorderRadius.circular(8),
                        ),
                        child: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            Text(
                              'UID: ${user.uid}',
                              style: const TextStyle(
                                fontSize: 11,
                                fontFamily: 'monospace',
                              ),
                            ),
                            const SizedBox(height: 4),
                            Text(
                              'RTDB: $data',
                              style: const TextStyle(
                                fontSize: 11,
                                fontFamily: 'monospace',
                              ),
                            ),
                          ],
                        ),
                      );
                    },
                  ),
              ],
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildMenuCard({
    required BuildContext context,
    required IconData icon,
    required Color iconColor,
    required String title,
    required String subtitle,
    required VoidCallback onTap,
    bool enabled = true,
  }) {
    return Card(
      elevation: enabled ? 2 : 0,
      color: enabled ? null : Colors.grey.shade100,
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
      child: InkWell(
        borderRadius: BorderRadius.circular(12),
        onTap: onTap,
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Row(
            children: [
              Container(
                padding: const EdgeInsets.all(12),
                decoration: BoxDecoration(
                  color: enabled
                      ? iconColor.withValues(alpha: 0.1)
                      : Colors.grey.shade200,
                  borderRadius: BorderRadius.circular(12),
                ),
                child: Icon(
                  icon,
                  size: 28,
                  color: enabled ? iconColor : Colors.grey,
                ),
              ),
              const SizedBox(width: 16),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      title,
                      style: TextStyle(
                        fontSize: 16,
                        fontWeight: FontWeight.bold,
                        color: enabled ? Colors.black87 : Colors.grey,
                      ),
                    ),
                    const SizedBox(height: 4),
                    Text(
                      subtitle,
                      style: TextStyle(
                        fontSize: 13,
                        color:
                            enabled ? Colors.grey.shade700 : Colors.grey.shade500,
                      ),
                    ),
                  ],
                ),
              ),
              Icon(
                Icons.chevron_right,
                color: enabled ? Colors.grey : Colors.grey.shade400,
              ),
            ],
          ),
        ),
      ),
    );
  }
}
