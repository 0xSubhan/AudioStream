package com.audiostream.audiostream_app

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat

class AudioReceiverService : Service() {

    companion object {
        const val ACTION_START   = "com.audiostream.ACTION_START"
        const val ACTION_STOP    = "com.audiostream.ACTION_STOP"
        const val EXTRA_PORT     = "extra_port"
        const val BROADCAST_ENGINE_STOPPED = "com.audiostream.ENGINE_STOPPED"

        private const val CHANNEL_ID   = "audiostream_foreground"
        private const val CHANNEL_NAME = "AudioStream Receiver"
        private const val NOTIFICATION_ID = 101
    }

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_START -> {
                val port = intent.getIntExtra(EXTRA_PORT, 8554)
                startForeground(NOTIFICATION_ID, buildNotification())
                ReceiverEngine.nativeStart(port)
            }
            ACTION_STOP -> {
                stopSelf()
            }
        }
        // If the system kills the service, do not restart it automatically.
        return START_NOT_STICKY
    }

    override fun onDestroy() {
        // Stop the C++ engine and remove the foreground notification.
        ReceiverEngine.nativeStop()
        stopForeground(STOP_FOREGROUND_REMOVE)
        // Broadcast so MainActivity can tell Flutter the engine stopped
        // (handles the case where Stop was tapped on the notification).
        sendBroadcast(Intent(BROADCAST_ENGINE_STOPPED))
        super.onDestroy()
    }

    // Foreground services are not bound — return null.
    override fun onBind(intent: Intent?): IBinder? = null

    // ── Notification ──────────────────────────────────────────────────────────

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                CHANNEL_NAME,
                NotificationManager.IMPORTANCE_LOW  // Silent — no sound/vibration
            ).apply {
                description = "Shows while AudioStream is actively receiving audio"
            }
            val manager = getSystemService(NOTIFICATION_SERVICE) as NotificationManager
            manager.createNotificationChannel(channel)
        }
    }

    private fun buildNotification(): Notification {
        // Tapping the notification re-opens the app.
        val openAppIntent = Intent(this, MainActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_SINGLE_TOP
        }
        val openAppPendingIntent = PendingIntent.getActivity(
            this, 0, openAppIntent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        // "Stop" action — sends ACTION_STOP back to this service.
        val stopIntent = Intent(this, AudioReceiverService::class.java).apply {
            action = ACTION_STOP
        }
        val stopPendingIntent = PendingIntent.getService(
            this, 1, stopIntent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("AudioStream Active")
            .setContentText("Receiving audio from PC…")
            .setSmallIcon(android.R.drawable.ic_media_play)
            .setContentIntent(openAppPendingIntent)
            .setOngoing(true)          // Cannot be dismissed by swipe
            .setShowWhen(false)
            .addAction(
                android.R.drawable.ic_media_pause,
                "Stop",
                stopPendingIntent
            )
            .build()
    }
}
