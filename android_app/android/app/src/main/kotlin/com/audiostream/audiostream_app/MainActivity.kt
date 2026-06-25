package com.audiostream.audiostream_app

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.os.Build
import android.os.Bundle
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel

class MainActivity : FlutterActivity() {
    private val CHANNEL = "com.audiostream/receiver"

    private var methodChannel: MethodChannel? = null
    private var nsdManager: NsdManager? = null
    private var registrationListener: NsdManager.RegistrationListener? = null
    private var serviceName: String = "AudioStream-${Build.MODEL}"

    // ── BroadcastReceiver: listens for SERVICE → UI "engine stopped" events ──
    private val engineStoppedReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            if (intent?.action == AudioReceiverService.BROADCAST_ENGINE_STOPPED) {
                // Tell Flutter the engine stopped externally (notification Stop button)
                methodChannel?.invokeMethod("onEngineStopped", null)
            }
        }
    }

    // ── Activity lifecycle ────────────────────────────────────────────────────

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        registerMdnsService()

        // Register broadcast receiver for when notification Stop is tapped
        val filter = IntentFilter(AudioReceiverService.BROADCAST_ENGINE_STOPPED)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(engineStoppedReceiver, filter, RECEIVER_NOT_EXPORTED)
        } else {
            @Suppress("UnspecifiedRegisterReceiverFlag")
            registerReceiver(engineStoppedReceiver, filter)
        }
    }

    override fun onDestroy() {
        // Do NOT stop the engine here — the Service owns the engine lifecycle.
        // The Foreground Service continues running even after the Activity is destroyed.
        unregisterReceiver(engineStoppedReceiver)
        unregisterMdnsService()
        super.onDestroy()
    }

    // ── Flutter method channel ────────────────────────────────────────────────

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)

        methodChannel = MethodChannel(flutterEngine.dartExecutor.binaryMessenger, CHANNEL)

        methodChannel!!.setMethodCallHandler { call, result ->
            when (call.method) {
                "startEngine" -> {
                    val port = call.argument<Int>("port") ?: 8554
                    // Delegate to the foreground service — keeps running in background
                    val serviceIntent = Intent(this, AudioReceiverService::class.java).apply {
                        action = AudioReceiverService.ACTION_START
                        putExtra(AudioReceiverService.EXTRA_PORT, port)
                    }
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                        startForegroundService(serviceIntent)
                    } else {
                        startService(serviceIntent)
                    }
                    result.success(true)
                }
                "stopEngine" -> {
                    // Tell the service to stop itself (calls nativeStop internally)
                    val serviceIntent = Intent(this, AudioReceiverService::class.java).apply {
                        action = AudioReceiverService.ACTION_STOP
                    }
                    startService(serviceIntent)
                    result.success(true)
                }
                "getStats" -> {
                    val stats = mapOf(
                        "packetCount"   to ReceiverEngine.nativeGetPacketCount(),
                        "underrunCount" to ReceiverEngine.nativeGetUnderrunCount(),
                        "jitterMs"      to ReceiverEngine.nativeGetJitterMs(),
                        "targetDelay"   to ReceiverEngine.nativeGetTargetDelayPackets()
                    )
                    result.success(stats)
                }
                else -> result.notImplemented()
            }
        }
    }

    // ── mDNS registration ─────────────────────────────────────────────────────

    private fun registerMdnsService() {
        nsdManager = getSystemService(Context.NSD_SERVICE) as NsdManager

        val serviceInfo = NsdServiceInfo().apply {
            serviceType = "_audiostream._udp."
            serviceName = this@MainActivity.serviceName
            port = 8554
        }

        registrationListener = object : NsdManager.RegistrationListener {
            override fun onServiceRegistered(info: NsdServiceInfo) {
                this@MainActivity.serviceName = info.serviceName
            }
            override fun onRegistrationFailed(info: NsdServiceInfo, errorCode: Int) {}
            override fun onServiceUnregistered(info: NsdServiceInfo) {}
            override fun onUnregistrationFailed(info: NsdServiceInfo, errorCode: Int) {}
        }

        nsdManager?.registerService(
            serviceInfo,
            NsdManager.PROTOCOL_DNS_SD,
            registrationListener
        )
    }

    private fun unregisterMdnsService() {
        if (nsdManager != null && registrationListener != null) {
            try {
                nsdManager?.unregisterService(registrationListener)
            } catch (e: Exception) {
                // Ignore if not registered or already closed
            }
        }
    }
}
