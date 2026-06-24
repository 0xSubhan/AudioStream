package com.audiostream.audiostream_app

object ReceiverEngine {
    init {
        // Loads our compiled C++ JNI library
        System.loadLibrary("audiostream_receiver")
    }

    external fun nativeStart(port: Int): Boolean
    external fun nativeStop()
    external fun nativeGetPacketCount(): Int
    external fun nativeGetUnderrunCount(): Int
    external fun nativeGetJitterMs(): Double
    external fun nativeGetTargetDelayPackets(): Int
}
