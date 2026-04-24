import 'package:firebase_auth/firebase_auth.dart';
import 'package:firebase_database/firebase_database.dart';
import 'package:flutter/material.dart';

// TODO: 디바이스 등록 화면 만들면 동적으로 변경. 지금은 1개 디스펜서 가정.
const String kDeviceId = 'device_001';

class ScheduleScreen extends StatefulWidget {
  const ScheduleScreen({super.key});

  @override
  State<ScheduleScreen> createState() => _ScheduleScreenState();
}

class _ScheduleScreenState extends State<ScheduleScreen> {
  final _user = FirebaseAuth.instance.currentUser;
  late final DatabaseReference _ref;

  // 3개 시간대 데이터 (기본값)
  final Map<String, ScheduleSlot> _slots = {
    'morning': ScheduleSlot(time: '08:00', enabled: true, medications: []),
    'afternoon': ScheduleSlot(time: '12:00', enabled: true, medications: []),
    'evening': ScheduleSlot(time: '18:00', enabled: true, medications: []),
  };

  bool _loading = true;
  bool _saving = false;
  bool _hasChanges = false;

  @override
  void initState() {
    super.initState();
    if (_user == null) {
      // 로그인 안 된 상태 (이론상 발생 안 함, AuthGate가 막음)
      _loading = false;
      return;
    }
    _ref = FirebaseDatabase.instance.ref(
      'schedules/${_user.uid}/$kDeviceId',
    );
    _loadSchedule();
  }

  Future<void> _loadSchedule() async {
    try {
      final snapshot = await _ref.get();
      if (snapshot.exists && snapshot.value != null) {
        final data = Map<String, dynamic>.from(snapshot.value as Map);
        for (final slotName in ['morning', 'afternoon', 'evening']) {
          if (data[slotName] != null) {
            _slots[slotName] = ScheduleSlot.fromMap(
              Map<String, dynamic>.from(data[slotName] as Map),
            );
          }
        }
      }
    } catch (e) {
      print('🔴 스케줄 로드 에러: $e');
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('불러오기 실패: $e'), backgroundColor: Colors.red),
        );
      }
    } finally {
      if (mounted) setState(() => _loading = false);
    }
  }

  Future<void> _saveSchedule() async {
    setState(() => _saving = true);
    try {
      final data = <String, dynamic>{};
      _slots.forEach((key, slot) {
        data[key] = slot.toMap();
      });
      // updatedAt도 같이 기록 (펌웨어가 변경 감지에 활용 가능)
      data['updatedAt'] = ServerValue.timestamp;
      await _ref.set(data);
      if (!mounted) return;
      setState(() => _hasChanges = false);
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('✅ 저장되었습니다'),
          backgroundColor: Colors.green,
          duration: Duration(seconds: 2),
        ),
      );
    } catch (e) {
      print('🔴 스케줄 저장 에러: $e');
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('저장 실패: $e'), backgroundColor: Colors.red),
        );
      }
    } finally {
      if (mounted) setState(() => _saving = false);
    }
  }

  void _toggleEnabled(String slotKey, bool value) {
    setState(() {
      _slots[slotKey] = _slots[slotKey]!.copyWith(enabled: value);
      _hasChanges = true;
    });
  }

  Future<void> _changeTime(String slotKey) async {
    final current = _slots[slotKey]!.time.split(':');
    final picked = await showTimePicker(
      context: context,
      initialTime: TimeOfDay(
        hour: int.parse(current[0]),
        minute: int.parse(current[1]),
      ),
      helpText: '복용 시간 선택',
    );
    if (picked != null) {
      final formatted =
          '${picked.hour.toString().padLeft(2, '0')}:${picked.minute.toString().padLeft(2, '0')}';
      setState(() {
        _slots[slotKey] = _slots[slotKey]!.copyWith(time: formatted);
        _hasChanges = true;
      });
    }
  }

  Future<void> _addMedication(String slotKey) async {
    final result = await _showMedicationDialog();
    if (result != null && result.isNotEmpty) {
      setState(() {
        _slots[slotKey]!.medications.add(Medication(name: result, count: 1));
        _hasChanges = true;
      });
    }
  }

  Future<void> _editMedicationName(String slotKey, int index) async {
    final med = _slots[slotKey]!.medications[index];
    final result = await _showMedicationDialog(initialName: med.name);
    if (result != null && result.isNotEmpty) {
      setState(() {
        _slots[slotKey]!.medications[index] = med.copyWith(name: result);
        _hasChanges = true;
      });
    }
  }

  Future<String?> _showMedicationDialog({String? initialName}) {
    final controller = TextEditingController(text: initialName ?? '');
    return showDialog<String>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text(initialName == null ? '약 추가' : '약 이름 수정'),
        content: TextField(
          controller: controller,
          autofocus: true,
          decoration: const InputDecoration(
            labelText: '약 이름',
            hintText: '예: 혈압약',
            border: OutlineInputBorder(),
          ),
          onSubmitted: (v) => Navigator.pop(ctx, v.trim()),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx),
            child: const Text('취소'),
          ),
          ElevatedButton(
            onPressed: () => Navigator.pop(ctx, controller.text.trim()),
            child: Text(initialName == null ? '추가' : '저장'),
          ),
        ],
      ),
    );
  }

  void _removeMedication(String slotKey, int index) {
    setState(() {
      _slots[slotKey]!.medications.removeAt(index);
      _hasChanges = true;
    });
  }

  void _updateCount(String slotKey, int index, int delta) {
    final med = _slots[slotKey]!.medications[index];
    final newCount = (med.count + delta).clamp(1, 99);
    if (newCount == med.count) return;
    setState(() {
      _slots[slotKey]!.medications[index] = med.copyWith(count: newCount);
      _hasChanges = true;
    });
  }

  // 뒤로가기 시 저장 안 한 변경 있으면 경고
  Future<bool> _onWillPop() async {
    if (!_hasChanges) return true;
    final result = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('변경사항이 있습니다'),
        content: const Text('저장하지 않고 나가시겠어요?'),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx, false),
            child: const Text('취소'),
          ),
          TextButton(
            onPressed: () => Navigator.pop(ctx, true),
            child: const Text('나가기', style: TextStyle(color: Colors.red)),
          ),
        ],
      ),
    );
    return result ?? false;
  }

  @override
  Widget build(BuildContext context) {
    return PopScope(
      canPop: !_hasChanges,
      onPopInvokedWithResult: (didPop, _) async {
        if (didPop) return;
        final shouldPop = await _onWillPop();
        if (shouldPop && mounted) Navigator.of(context).pop();
      },
      child: Scaffold(
        appBar: AppBar(
          title: const Text('약 복용 스케줄'),
          actions: [
            TextButton.icon(
              onPressed: (_saving || !_hasChanges) ? null : _saveSchedule,
              icon: _saving
                  ? const SizedBox(
                      width: 16,
                      height: 16,
                      child: CircularProgressIndicator(strokeWidth: 2),
                    )
                  : const Icon(Icons.save, color: Colors.white),
              label: const Text(
                '저장',
                style: TextStyle(
                  color: Colors.white,
                  fontSize: 16,
                  fontWeight: FontWeight.bold,
                ),
              ),
            ),
          ],
        ),
        body: _loading
            ? const Center(child: CircularProgressIndicator())
            : ListView(
                padding: const EdgeInsets.all(16),
                children: [
                  if (_hasChanges)
                    Container(
                      margin: const EdgeInsets.only(bottom: 16),
                      padding: const EdgeInsets.all(12),
                      decoration: BoxDecoration(
                        color: Colors.amber.shade50,
                        border: Border.all(color: Colors.amber),
                        borderRadius: BorderRadius.circular(8),
                      ),
                      child: const Row(
                        children: [
                          Icon(Icons.warning_amber, color: Colors.orange),
                          SizedBox(width: 8),
                          Expanded(
                            child: Text(
                              '변경사항이 있습니다. 우측 상단 "저장"을 눌러주세요.',
                              style: TextStyle(color: Colors.orange),
                            ),
                          ),
                        ],
                      ),
                    ),
                  _buildScheduleCard('morning', '🌅 아침', Colors.orange),
                  const SizedBox(height: 16),
                  _buildScheduleCard('afternoon', '🌞 점심', Colors.amber.shade700),
                  const SizedBox(height: 16),
                  _buildScheduleCard('evening', '🌙 저녁', Colors.indigo),
                  const SizedBox(height: 32),
                ],
              ),
      ),
    );
  }

  Widget _buildScheduleCard(String key, String title, Color color) {
    final slot = _slots[key]!;
    return Card(
      elevation: 2,
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // 헤더: 시간대 + 활성 토글
            Row(
              children: [
                Text(
                  title,
                  style: TextStyle(
                    fontSize: 20,
                    fontWeight: FontWeight.bold,
                    color: slot.enabled ? color : Colors.grey,
                  ),
                ),
                const Spacer(),
                Switch(
                  value: slot.enabled,
                  onChanged: (v) => _toggleEnabled(key, v),
                ),
              ],
            ),
            if (slot.enabled) ...[
              const Divider(height: 24),
              // 시간 (탭하면 TimePicker)
              InkWell(
                borderRadius: BorderRadius.circular(8),
                onTap: () => _changeTime(key),
                child: Padding(
                  padding: const EdgeInsets.symmetric(
                    vertical: 8,
                    horizontal: 4,
                  ),
                  child: Row(
                    children: [
                      const Icon(Icons.access_time, size: 22),
                      const SizedBox(width: 8),
                      Text(
                        slot.time,
                        style: const TextStyle(
                          fontSize: 28,
                          fontWeight: FontWeight.w500,
                        ),
                      ),
                      const SizedBox(width: 8),
                      Icon(
                        Icons.edit,
                        size: 16,
                        color: Colors.grey.shade600,
                      ),
                    ],
                  ),
                ),
              ),
              const SizedBox(height: 12),
              // 약 목록
              Row(
                children: [
                  const Text(
                    '💊 복용할 약',
                    style: TextStyle(fontWeight: FontWeight.bold, fontSize: 14),
                  ),
                  const SizedBox(width: 8),
                  Text(
                    '(${slot.medications.length}개)',
                    style: TextStyle(color: Colors.grey.shade600, fontSize: 13),
                  ),
                ],
              ),
              const SizedBox(height: 8),
              if (slot.medications.isEmpty)
                Padding(
                  padding: const EdgeInsets.symmetric(vertical: 8),
                  child: Text(
                    '아직 등록된 약이 없습니다',
                    style: TextStyle(
                      color: Colors.grey.shade600,
                      fontStyle: FontStyle.italic,
                    ),
                  ),
                ),
              ...slot.medications.asMap().entries.map((entry) {
                final i = entry.key;
                final med = entry.value;
                return Padding(
                  padding: const EdgeInsets.symmetric(vertical: 4),
                  child: Row(
                    children: [
                      Expanded(
                        child: InkWell(
                          onTap: () => _editMedicationName(key, i),
                          child: Padding(
                            padding: const EdgeInsets.symmetric(vertical: 8),
                            child: Text(
                              med.name,
                              style: const TextStyle(fontSize: 16),
                            ),
                          ),
                        ),
                      ),
                      IconButton(
                        icon: const Icon(Icons.remove_circle_outline, size: 22),
                        onPressed: med.count > 1
                            ? () => _updateCount(key, i, -1)
                            : null,
                        visualDensity: VisualDensity.compact,
                      ),
                      Container(
                        constraints: const BoxConstraints(minWidth: 40),
                        alignment: Alignment.center,
                        child: Text(
                          '${med.count}정',
                          style: const TextStyle(
                            fontWeight: FontWeight.bold,
                            fontSize: 15,
                          ),
                        ),
                      ),
                      IconButton(
                        icon: const Icon(Icons.add_circle_outline, size: 22),
                        onPressed: med.count < 99
                            ? () => _updateCount(key, i, 1)
                            : null,
                        visualDensity: VisualDensity.compact,
                      ),
                      IconButton(
                        icon: const Icon(
                          Icons.delete_outline,
                          size: 22,
                          color: Colors.red,
                        ),
                        onPressed: () => _removeMedication(key, i),
                        visualDensity: VisualDensity.compact,
                      ),
                    ],
                  ),
                );
              }),
              const SizedBox(height: 8),
              SizedBox(
                width: double.infinity,
                child: OutlinedButton.icon(
                  icon: const Icon(Icons.add),
                  label: const Text('약 추가'),
                  onPressed: () => _addMedication(key),
                ),
              ),
            ],
          ],
        ),
      ),
    );
  }
}

// ───────────────────────────── Data Models ─────────────────────────────

class ScheduleSlot {
  String time;
  bool enabled;
  List<Medication> medications;

  ScheduleSlot({
    required this.time,
    required this.enabled,
    required this.medications,
  });

  ScheduleSlot copyWith({
    String? time,
    bool? enabled,
    List<Medication>? medications,
  }) {
    return ScheduleSlot(
      time: time ?? this.time,
      enabled: enabled ?? this.enabled,
      medications: medications ?? this.medications,
    );
  }

  Map<String, dynamic> toMap() => {
    'time': time,
    'enabled': enabled,
    'medications': medications.map((m) => m.toMap()).toList(),
  };

  factory ScheduleSlot.fromMap(Map<String, dynamic> map) {
    return ScheduleSlot(
      time: map['time'] as String? ?? '08:00',
      enabled: map['enabled'] as bool? ?? true,
      medications: (map['medications'] as List?)
              ?.map((m) => Medication.fromMap(
                    Map<String, dynamic>.from(m as Map),
                  ))
              .toList() ??
          [],
    );
  }
}

class Medication {
  String name;
  int count;

  Medication({required this.name, required this.count});

  Medication copyWith({String? name, int? count}) {
    return Medication(
      name: name ?? this.name,
      count: count ?? this.count,
    );
  }

  Map<String, dynamic> toMap() => {'name': name, 'count': count};

  factory Medication.fromMap(Map<String, dynamic> map) {
    return Medication(
      name: map['name'] as String? ?? '',
      count: (map['count'] as num?)?.toInt() ?? 1,
    );
  }
}
