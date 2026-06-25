import 'dart:async';
import 'dart:io';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

void main() {
  runApp(const AudioStreamReceiverApp());
}

class AudioStreamReceiverApp extends StatelessWidget {
  const AudioStreamReceiverApp({Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'AudioStream Receiver',
      debugShowCheckedModeBanner: false,
      theme: ThemeData.dark().copyWith(
        scaffoldBackgroundColor: const Color(0xFF121214),
        cardColor: const Color(0xFF1C1C1E),
        colorScheme: const ColorScheme.dark(
          primary: Color(0xFF0A84FF),
          secondary: Color(0xFF30D158),
          surface: Color(0xFF1C1C1E),
        ),
      ),
      home: const ReceiverDashboard(),
    );
  }
}

class ReceiverDashboard extends StatefulWidget {
  const ReceiverDashboard({Key? key}) : super(key: key);

  @override
  State<ReceiverDashboard> createState() => _ReceiverDashboardState();
}

class _ReceiverDashboardState extends State<ReceiverDashboard> {
  static const _channel = MethodChannel('com.audiostream/receiver');
  
  bool _isListening = false;
  String _ipAddress = 'Loading...';
  int _packetCount = 0;
  int _underrunCount = 0;
  double _jitterMs = 0.0;
  int _targetDelayPackets = 0;
  Timer? _statsTimer;

  @override
  void initState() {
    super.initState();
    _resolveIpAddress();
    // Listen for native-side events (e.g. notification Stop button tapped)
    _channel.setMethodCallHandler(_handleNativeCall);
  }

  /// Handles method calls pushed FROM native → Flutter.
  Future<void> _handleNativeCall(MethodCall call) async {
    if (call.method == 'onEngineStopped') {
      // The user tapped Stop on the notification — sync the UI state.
      _statsTimer?.cancel();
      if (mounted) {
        setState(() {
          _isListening = false;
          _packetCount = 0;
          _underrunCount = 0;
          _jitterMs = 0.0;
          _targetDelayPackets = 0;
        });
      }
    }
  }

  @override
  void dispose() {
    _statsTimer?.cancel();
    // NOTE: Do NOT stop the engine here. The Foreground Service owns the
    // engine lifecycle and keeps running even when this Activity is destroyed.
    super.dispose();
  }

  Future<void> _resolveIpAddress() async {
    String ip = 'Not Connected';
    try {
      final interfaces = await NetworkInterface.list(
        type: InternetAddressType.IPv4,
        includeLoopback: false,
      );
      for (var interface in interfaces) {
        // Typically WiFi interface name starts with wlan or wl
        if (interface.name.contains('wlan') || interface.name.contains('wl') || ip == 'Not Connected') {
          for (var addr in interface.addresses) {
            ip = addr.address;
          }
        }
      }
    } catch (e) {
      ip = 'Error resolving IP';
    }
    setState(() {
      _ipAddress = ip;
    });
  }

  Future<void> _toggleReceiver() async {
    if (_isListening) {
      // Stop Engine
      await _channel.invokeMethod('stopEngine');
      _statsTimer?.cancel();
      setState(() {
        _isListening = false;
      });
    } else {
      // Start Engine
      final bool success = await _channel.invokeMethod('startEngine', {'port': 8554});
      if (success) {
        setState(() {
          _isListening = true;
          _packetCount = 0;
          _underrunCount = 0;
          _jitterMs = 0.0;
          _targetDelayPackets = 0;
        });
        
        // Start polling stats every 200ms
        _statsTimer = Timer.periodic(const Duration(milliseconds: 200), (timer) async {
          try {
            final Map<dynamic, dynamic>? stats = await _channel.invokeMethod('getStats');
            if (stats != null && mounted) {
              setState(() {
                _packetCount = stats['packetCount'] ?? 0;
                _underrunCount = stats['underrunCount'] ?? 0;
                _jitterMs = (stats['jitterMs'] ?? 0.0).toDouble();
                _targetDelayPackets = stats['targetDelay'] ?? 0;
              });
            }
          } catch (_) {}
        });
      } else {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(
            content: Text('Failed to start C++ Playback Engine'),
            backgroundColor: Color(0xFFFF453A),
          ),
        );
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text(
          'AudioStream Receiver',
          style: TextStyle(fontWeight: FontWeight.bold, fontSize: 20),
        ),
        backgroundColor: Colors.transparent,
        elevation: 0,
        centerTitle: true,
        actions: [
          IconButton(
            icon: const Icon(Icons.refresh),
            onPressed: _resolveIpAddress,
            tooltip: 'Refresh Network IP',
          )
        ],
      ),
      body: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 24.0, vertical: 16.0),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            // 1. Connection Status Card
            Card(
              elevation: 4,
              shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
              child: Padding(
                padding: const EdgeInsets.all(20.0),
                child: Row(
                  children: [
                    Container(
                      width: 12,
                      height: 12,
                      decoration: BoxDecoration(
                        shape: BoxShape.circle,
                        color: _isListening ? const Color(0xFF30D158) : const Color(0xFFFF453A),
                        boxShadow: _isListening
                            ? [
                                BoxShadow(
                                  color: const Color(0xFF30D158).withOpacity(0.5),
                                  blurRadius: 8,
                                  spreadRadius: 2,
                                )
                              ]
                            : [],
                      ),
                    ),
                    const SizedBox(width: 16),
                    Text(
                      _isListening ? 'Receiver Active' : 'Receiver Sleeping',
                      style: TextStyle(
                        fontSize: 16,
                        fontWeight: FontWeight.bold,
                        color: _isListening ? const Color(0xFF30D158) : const Color(0xFFFF453A),
                      ),
                    ),
                    const Spacer(),
                    Column(
                      crossAxisAlignment: CrossAxisAlignment.end,
                      children: [
                        const Text(
                          'DEVICE WIFI IP',
                          style: TextStyle(fontSize: 10, color: Color(0xFF8E8E93), letterSpacing: 1),
                        ),
                        const SizedBox(height: 4),
                        Text(
                          _ipAddress,
                          style: const TextStyle(fontSize: 14, fontWeight: FontWeight.w600),
                        ),
                      ],
                    ),
                  ],
                ),
              ),
            ),
            const SizedBox(height: 32),

            // 2. Glowing Control Toggle
            Expanded(
              child: Center(
                child: GestureDetector(
                  onTap: _toggleReceiver,
                  child: AnimatedContainer(
                    duration: const Duration(milliseconds: 300),
                    width: 200,
                    height: 200,
                    decoration: BoxDecoration(
                      shape: BoxShape.circle,
                      color: _isListening
                          ? const Color(0xFF30D158).withOpacity(0.1)
                          : const Color(0xFF0A84FF).withOpacity(0.1),
                      border: Border.all(
                        color: _isListening ? const Color(0xFF30D158) : const Color(0xFF0A84FF),
                        width: 4,
                      ),
                      boxShadow: [
                        BoxShadow(
                          color: _isListening
                              ? const Color(0xFF30D158).withOpacity(0.3)
                              : const Color(0xFF0A84FF).withOpacity(0.3),
                          blurRadius: 40,
                          spreadRadius: 10,
                        )
                      ],
                    ),
                    child: Column(
                      mainAxisAlignment: MainAxisAlignment.center,
                      children: [
                        Icon(
                          _isListening ? Icons.stop_rounded : Icons.play_arrow_rounded,
                          size: 64,
                          color: _isListening ? const Color(0xFF30D158) : const Color(0xFF0A84FF),
                        ),
                        const SizedBox(height: 12),
                        Text(
                          _isListening ? 'STOP RECEIVER' : 'START RECEIVER',
                          style: TextStyle(
                            fontSize: 12,
                            fontWeight: FontWeight.bold,
                            letterSpacing: 1.5,
                            color: _isListening ? const Color(0xFF30D158) : const Color(0xFF0A84FF),
                          ),
                        ),
                      ],
                    ),
                  ),
                ),
              ),
            ),
            const SizedBox(height: 32),

            // 3. Telemetry Card
            if (_isListening)
              Card(
                elevation: 4,
                shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
                child: Padding(
                  padding: const EdgeInsets.all(20.0),
                  child: Column(
                    children: [
                      Row(
                        mainAxisAlignment: MainAxisAlignment.spaceAround,
                        children: [
                          Expanded(
                            child: Column(
                              children: [
                                const Text(
                                  'PACKETS RECEIVED',
                                  style: TextStyle(fontSize: 10, color: Color(0xFF8E8E93), fontWeight: FontWeight.bold, letterSpacing: 0.5),
                                ),
                                const SizedBox(height: 6),
                                Text(
                                  '$_packetCount',
                                  style: const TextStyle(fontSize: 20, fontWeight: FontWeight.bold, color: Color(0xFF0A84FF)),
                                ),
                              ],
                            ),
                          ),
                          Container(width: 1, height: 35, color: const Color(0xFF2C2C2E)),
                          Expanded(
                            child: Column(
                              children: [
                                const Text(
                                  'STARVATIONS',
                                  style: TextStyle(fontSize: 10, color: Color(0xFF8E8E93), fontWeight: FontWeight.bold, letterSpacing: 0.5),
                                ),
                                const SizedBox(height: 6),
                                Text(
                                  '$_underrunCount',
                                  style: TextStyle(
                                    fontSize: 20,
                                    fontWeight: FontWeight.bold,
                                    color: _underrunCount > 0 ? const Color(0xFFFF453A) : const Color(0xFF30D158),
                                  ),
                                ),
                              ],
                            ),
                          ),
                        ],
                      ),
                      const SizedBox(height: 16),
                      Divider(color: const Color(0xFF2C2C2E), height: 1),
                      const SizedBox(height: 16),
                      Row(
                        mainAxisAlignment: MainAxisAlignment.spaceAround,
                        children: [
                          Expanded(
                            child: Column(
                              children: [
                                const Text(
                                  'ESTIMATED JITTER',
                                  style: TextStyle(fontSize: 10, color: Color(0xFF8E8E93), fontWeight: FontWeight.bold, letterSpacing: 0.5),
                                ),
                                const SizedBox(height: 6),
                                Text(
                                  '${_jitterMs.toStringAsFixed(2)} ms',
                                  style: TextStyle(
                                    fontSize: 20,
                                    fontWeight: FontWeight.bold,
                                    color: _jitterMs > 10.0
                                        ? (_jitterMs > 30.0 ? const Color(0xFFFF453A) : const Color(0xFFFFD60A))
                                        : const Color(0xFF30D158),
                                  ),
                                ),
                              ],
                            ),
                          ),
                          Container(width: 1, height: 35, color: const Color(0xFF2C2C2E)),
                          Expanded(
                            child: Column(
                              children: [
                                const Text(
                                  'BUFFER LATENCY',
                                  style: TextStyle(fontSize: 10, color: Color(0xFF8E8E93), fontWeight: FontWeight.bold, letterSpacing: 0.5),
                                ),
                                const SizedBox(height: 6),
                                Text(
                                  '${_targetDelayPackets * 20} ms ($_targetDelayPackets pkts)',
                                  style: const TextStyle(
                                    fontSize: 20,
                                    fontWeight: FontWeight.bold,
                                    color: Color(0xFFBF5AF2),
                                  ),
                                ),
                              ],
                            ),
                          ),
                        ],
                      ),
                    ],
                  ),
                ),
              ),
            const SizedBox(height: 16),
            const Text(
              'Listening on port 8554. Audio streams in the background — use the notification to stop without opening the app.',
              textAlign: TextAlign.center,
              style: TextStyle(fontSize: 11, color: Color(0xFF8E8E93)),
            ),
          ],
        ),
      ),
    );
  }
}
