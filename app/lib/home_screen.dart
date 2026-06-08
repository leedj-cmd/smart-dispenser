import 'package:firebase_database/firebase_database.dart';
import 'package:flutter/material.dart';
import 'auth_service.dart';
import 'schedule_screen.dart';

const String kDeviceId = 'device_001';

class HomeScreen extends StatelessWidget {
  const HomeScreen({super.key});

  String _formatDate(DateTime dt) {
    const weekdays = ['월', '화', '수', '목', '금', '토', '일'];
    final weekday = weekdays[dt.weekday - 1];
    return '${dt.year}년 ${dt.month}월 ${dt.day}일 ${weekday}요일';
  }

  Future<void> _dispense(BuildContext context, String slot) async {
    await _sendCommand(context, slot, '${_slotLabel(slot)} 배출 명령 전송됨');
  }

  Future<void> _home(BuildContext context) async {
    await _sendCommand(context, 'HOME', '🏠 0° 이동 명령 전송됨');
  }

  Future<void> _sendCommand(
      BuildContext context, String command, String successMsg) async {
    try {
      await FirebaseDatabase.instance
          .ref('dispense/$kDeviceId/command')
          .set(command);
      if (!context.mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(successMsg),
          backgroundColor: Colors.green,
          duration: const Duration(seconds: 2),
        ),
      );
    } catch (e) {
      if (!context.mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('오류: $e'), backgroundColor: Colors.red),
      );
    }
  }

  String _slotLabel(String slot) {
    switch (slot) {
      case 'morning':
        return '🌅 아침';
      case 'lunch':
        return '🌞 점심';
      case 'dinner':
        return '🌙 저녁';
      default:
        return slot;
    }
  }

  @override
  Widget build(BuildContext context) {
    final authService = AuthService();
    final user = authService.currentUser;
    final today = DateTime.now();

    return Scaffold(
      appBar: AppBar(
        title: const Text('세달의 기적'),
        actions: [
          IconButton(
            icon: const Icon(Icons.logout),
            tooltip: '로그아웃',
            onPressed: () async {
              await authService.signOut();
            },
          ),
        ],
      ),
      body: SingleChildScrollView(
        padding: const EdgeInsets.all(24.0),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            // 날짜 + 요일
            Center(
              child: Text(
                _formatDate(today),
                style: const TextStyle(
                  fontSize: 20,
                  fontWeight: FontWeight.bold,
                ),
              ),
            ),
            const SizedBox(height: 24),

            // 환영 메시지
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

            // 즉시 배출 버튼
            const Text(
              '즉시 배출',
              style: TextStyle(fontSize: 16, fontWeight: FontWeight.bold),
            ),
            const SizedBox(height: 12),
            Row(
              children: [
                Expanded(
                  child: _buildDispenseButton(
                    context, 'morning', '🌅', '아침', Colors.orange),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: _buildDispenseButton(
                    context, 'lunch', '🌞', '점심', Colors.amber.shade700),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: _buildDispenseButton(
                    context, 'dinner', '🌙', '저녁', Colors.indigo),
                ),
              ],
            ),
            const SizedBox(height: 12),
            OutlinedButton.icon(
              style: OutlinedButton.styleFrom(
                padding: const EdgeInsets.symmetric(vertical: 16),
                shape: RoundedRectangleBorder(
                    borderRadius: BorderRadius.circular(12)),
                side: BorderSide(color: Colors.deepPurple.shade300),
                foregroundColor: Colors.deepPurple,
              ),
              onPressed: () => _home(context),
              icon: const Text('🏠', style: TextStyle(fontSize: 20)),
              label: const Text(
                '0°로 이동 (HOME)',
                style: TextStyle(fontSize: 15, fontWeight: FontWeight.bold),
              ),
            ),
            const SizedBox(height: 24),

            // 관리 메뉴
            const Text(
              '관리',
              style: TextStyle(fontSize: 16, fontWeight: FontWeight.bold),
            ),
            const SizedBox(height: 12),

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

            // 개발자 도구
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

  Widget _buildDispenseButton(
    BuildContext context,
    String slot,
    String emoji,
    String label,
    Color color,
  ) {
    return ElevatedButton(
      style: ElevatedButton.styleFrom(
        backgroundColor: color,
        foregroundColor: Colors.white,
        padding: const EdgeInsets.symmetric(vertical: 20),
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
        elevation: 3,
      ),
      onPressed: () => _dispense(context, slot),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Text(emoji, style: const TextStyle(fontSize: 28)),
          const SizedBox(height: 6),
          Text(
            label,
            style: const TextStyle(fontSize: 16, fontWeight: FontWeight.bold),
          ),
        ],
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
                        color: enabled
                            ? Colors.grey.shade700
                            : Colors.grey.shade500,
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
