package com.audiostream.audiostream_app

import android.content.Context
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.os.Build
import android.os.Bundle
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel

class MainActivity : FlutterActivity() {
    private val CHANNEL = "com.audiostream/receiver"
    private var nsdManager: NsdManager? = null
    private var registrationListener: NsdManager.RegistrationListener? = null
    private var serviceName: String = "AudioStream-${Build.MODEL}"

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        registerMdnsService()
    }

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        
        MethodChannel(flutterEngine.dartExecutor.binaryMessenger, CHANNEL).setMethodCallHandler { call, result ->
            when (call.method) {
                "startEngine" -> {
                    val port = call.argument<Int>("port") ?: 8554
                    val success = ReceiverEngine.nativeStart(port)
                    result.success(success)
                }
                "stopEngine" -> {
                    ReceiverEngine.nativeStop()
                    result.success(true)
                }
                "getStats" -> {
                    val stats = mapOf(
                        "packetCount" to ReceiverEngine.nativeGetPacketCount(),
                        "underrunCount" to ReceiverEngine.nativeGetUnderrunCount(),
                        "jitterMs" to ReceiverEngine.nativeGetJitterMs(),
                        "targetDelay" to ReceiverEngine.nativeGetTargetDelayPackets()
                    )
                    result.success(stats)
                }
                else -> result.notImplemented()
            }
        }
    }

    private fun registerMdnsService() {
        nsdManager = getSystemService(Context.NSD_SERVICE) as NsdManager
        
        val serviceInfo = NsdServiceInfo().apply {
            serviceType = "_audiostream._udp."
            serviceName = this@MainActivity.serviceName
            port = 8554
        }

        registrationListener = object : NsdManager.RegistrationListener {
            override fun onServiceRegistered(info: NsdServiceInfo) {
                // Store the actual registered name (handles namespace conflicts)
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

    override fun onDestroy() {
        ReceiverEngine.nativeStop()
        unregisterMdnsService()
        super.onDestroy()
    }
}
