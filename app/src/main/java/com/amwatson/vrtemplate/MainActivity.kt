package com.amwatson.vrtemplate

import android.os.Bundle
import android.util.Log
import androidx.activity.ComponentActivity

class MainActivity : ComponentActivity() {
    private var mHandle: Long = 0

    companion object {
        private const val TAG = "VrTemplate"
        // Load native libraries
        init {
            try {
                System.loadLibrary("vrtemplate")
            } catch (e: Exception) {
                Log.e(TAG, "Failed to load native libraries", e)
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        Log.i(TAG, "onCreate()")
        super.onCreate(savedInstanceState)

        // Create our native VR session
        mHandle = nativeOnCreate()
    }

    override fun onDestroy() {
        Log.i(TAG, "onDestroy()")
        if (mHandle != 0L) {
            nativeOnDestroy(mHandle)
            mHandle = 0L
        }
        super.onDestroy()
    }

    override fun onStart() {
        Log.i(TAG, "onStart()")
        super.onStart()
    }

    override fun onResume() {
        Log.i(TAG, "onResume()")
        super.onResume()
    }

    override fun onPause() {
        Log.i(TAG, "onPause()")
        super.onPause()
    }

    override fun onStop() {
        Log.i(TAG, "onStop()")
        super.onStop()
    }

    // Native JNI methods
    private external fun nativeOnCreate(): Long
    private external fun nativeOnDestroy(handle: Long)
}