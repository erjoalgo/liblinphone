/*
 * Copyright (c) 2010-2020 Belledonne Communications SARL.
 *
 * This file is part of Liblinphone.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

package org.linphone.core.tools.audio;

import android.content.Context;
import android.content.IntentFilter;
import android.media.AudioAttributes;
import android.media.AudioManager;
import android.media.AudioManager.OnAudioFocusChangeListener;
import android.media.MediaPlayer;
import android.net.Uri;
import android.provider.Settings;

import androidx.media.AudioAttributesCompat;
import androidx.media.AudioAttributesCompat.Builder;
import androidx.media.AudioManagerCompat;
import androidx.media.AudioFocusRequestCompat;
import android.media.Ringtone;
import android.media.RingtoneManager;

import java.io.IOException;
import java.io.FileInputStream;
import java.lang.SecurityException;

import org.linphone.core.Core;
import org.linphone.core.AudioDevice;
import org.linphone.core.tools.Log;
import org.linphone.core.tools.receiver.HeadsetReceiver;
import org.linphone.core.tools.service.CoreManager;

public class AudioHelper implements OnAudioFocusChangeListener {
    private AudioManager mAudioManager;
    private AudioFocusRequestCompat mRingingRequest;
    private AudioFocusRequestCompat mCallRequest;
    private MediaPlayer mPlayer;
    private Ringtone mRingtone;
    private int mVolumeBeforeEchoTest;
    private AudioDevice mPreviousDefaultOutputAudioDevice;
    private HeadsetReceiver mHeadsetReceiver;

    public AudioHelper(Context context) {
        mAudioManager = (AudioManager) context.getSystemService(Context.AUDIO_SERVICE);
        mRingingRequest = null;
        mCallRequest = null;

        mHeadsetReceiver = new HeadsetReceiver();
        IntentFilter filter = new IntentFilter(AudioManager.ACTION_HEADSET_PLUG);
        context.registerReceiver(mHeadsetReceiver, filter);
        
        Log.i("[Audio Helper] Helper created");
    }

    public void destroy(Context context) {
        Log.i("[Audio Helper] Destroying");
        if (mHeadsetReceiver != null) {
            context.unregisterReceiver(mHeadsetReceiver);
            mHeadsetReceiver = null;
        }

        stopRinging();
        releaseRingingAudioFocus();
        releaseCallAudioFocus();
        Log.i("[Audio Helper] Destroyed");
    }

    public void startAudioForEchoTestOrCalibration() {
        requestCallAudioFocus();
        routeAudioToSpeaker();

        mVolumeBeforeEchoTest = mAudioManager.getStreamVolume(AudioManager.STREAM_VOICE_CALL);
        int maxVolume = mAudioManager.getStreamMaxVolume(AudioManager.STREAM_VOICE_CALL);
        try {
            mAudioManager.setStreamVolume(AudioManager.STREAM_VOICE_CALL, maxVolume, 0);
        } catch (SecurityException se) {
            Log.e("[Audio Helper] Couldn't increase volume: ", se);
        }
    }

    public void stopAudioForEchoTestOrCalibration() {
        try {
            mAudioManager.setStreamVolume(AudioManager.STREAM_VOICE_CALL, mVolumeBeforeEchoTest, 0);
        } catch (SecurityException se) {
            Log.e("[Audio Helper] Couldn't restore volume: ", se);
        }

        routeAudioToEarpiece();
        releaseCallAudioFocus();
    }

    public void startRinging(Context context, String ringtone) {
        if (mPlayer != null || mRingtone != null) {
            Log.w("[Audio Helper] Already ringing, skipping...");
            return;
        }

        requestRingingAudioFocus();

        AudioAttributes audioAttrs = new AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_NOTIFICATION_RINGTONE)
            .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
            .build();

        if (ringtone == null || ringtone.isEmpty()) {
            Log.i("[Audio Helper] Core ringtone path is null, using device ringtone if possible");

            Uri defaultRingtoneUri = RingtoneManager.getActualDefaultRingtoneUri(context, RingtoneManager.TYPE_RINGTONE);
            if (defaultRingtoneUri == null) {
                Log.i("[Audio Helper] Couldn't get default ringtone URI through RingtoneManager, trying with Settings.System.DEFAULT_RINGTONE_URI");
                ringtone = Settings.System.DEFAULT_RINGTONE_URI.toString();
                playSoundUsingMediaPlayer(context, audioAttrs, ringtone);
            } else {
                mRingtone = RingtoneManager.getRingtone(context, defaultRingtoneUri);
                if (mRingtone != null) {
                    mRingtone.setAudioAttributes(audioAttrs);
                    mRingtone.setLooping(true);
                    mRingtone.play();
                    Log.i("[Audio Helper] Ringtone ringing started");
                } else {
                    Log.e("[Audio Helper] Couldn't retrieve Ringtone object from manager!");
                }
            }

        } else {
            playSoundUsingMediaPlayer(context, audioAttrs, ringtone);
        }
    }

    public void stopRinging() {
        if (mRingtone != null) {
            releaseRingingAudioFocus();

            mRingtone.stop();
            mRingtone = null;
            Log.i("[Audio Helper] Ringtone ringing stopped");
        }

        if (mPlayer != null) {
            releaseRingingAudioFocus();
            
            mPlayer.stop();
            mPlayer.release();
            mPlayer = null;
            Log.i("[Audio Helper] Media player ringing stopped");
        }
    }

    public void requestRingingAudioFocus() {
        if (isAudioFocusDisabled()) {
            Log.i("[Audio Helper] We were asked not to require audio focus, skipping");
            return;
        }

        if (mRingingRequest != null) {
            Log.w("[Audio Helper] Ringing audio focus request still active, skipping");
            return;
        }

        AudioAttributesCompat audioAttrs = new AudioAttributesCompat.Builder()
            .setUsage(AudioAttributesCompat.USAGE_NOTIFICATION_RINGTONE)
            .setContentType(AudioAttributesCompat.CONTENT_TYPE_MUSIC)
            .build();

        // Request AUDIOFOCUS_GAIN_TRANSIENT_EXCLUSIVE to prevent incoming call notification to "beep" at the same time we play the ringtone
        mRingingRequest = new AudioFocusRequestCompat.Builder(AudioManagerCompat.AUDIOFOCUS_GAIN_TRANSIENT_EXCLUSIVE)
            .setAudioAttributes(audioAttrs)
            .setOnAudioFocusChangeListener(this)
            .build();

        int result = AudioManagerCompat.requestAudioFocus(mAudioManager, mRingingRequest);
        if (result == AudioManager.AUDIOFOCUS_REQUEST_GRANTED) {
            Log.i("[Audio Helper] Ringing audio focus request granted");
        } else if (result == AudioManager.AUDIOFOCUS_REQUEST_FAILED) {
            Log.w("[Audio Helper] Ringing audio focus request failed");
        } else if (result == AudioManager.AUDIOFOCUS_REQUEST_DELAYED) {
            Log.w("[Audio Helper] Ringing audio focus request delayed");
        }
    }

    public void releaseRingingAudioFocus() {
        if (mRingingRequest != null) {
            int result = AudioManagerCompat.abandonAudioFocusRequest(mAudioManager, mRingingRequest);
            if (result == AudioManager.AUDIOFOCUS_REQUEST_GRANTED) {
                Log.i("[Audio Helper] Ringing audio focus request abandonned");
                mRingingRequest = null;
            } else {
                if (!isAudioFocusDisabled()) { 
                    Log.e("[Audio Helper] Ringing audio focus abandon request failed");
                }
            }
        }
    }

    public void requestCallAudioFocus() {
        if (isAudioFocusDisabled()) {
            Log.i("[Audio Helper] We were asked not to require audio focus, skipping");
            return;
        }
        
        if (mRingingRequest != null) {
            Log.w("[Audio Helper] Ringing audio focus request not abandonned, let's do it");
            releaseRingingAudioFocus();
        }

        if (mCallRequest != null) {
            Log.w("[Audio Helper] Call audio focus request still active, skipping");
            return;
        }

        AudioAttributesCompat audioAttrs = new AudioAttributesCompat.Builder()
            .setUsage(AudioAttributesCompat.USAGE_VOICE_COMMUNICATION)
            .setContentType(AudioAttributesCompat.CONTENT_TYPE_SPEECH)
            .build();

        // Android doc recommends TRANSIENT audio focus gain for VOIP calls: https://developer.android.com/reference/android/media/AudioManager#AUDIOFOCUS_GAIN_TRANSIENT
        mCallRequest = new AudioFocusRequestCompat.Builder(AudioManagerCompat.AUDIOFOCUS_GAIN_TRANSIENT)
            .setAudioAttributes(audioAttrs)
            .setOnAudioFocusChangeListener(this)
            .build();

        int result = AudioManagerCompat.requestAudioFocus(mAudioManager, mCallRequest);
        if (result == AudioManager.AUDIOFOCUS_REQUEST_GRANTED) {
            Log.i("[Audio Helper] Call audio focus request granted");
            setAudioManagerInCommunicationMode();
        } else {
            if (result == AudioManager.AUDIOFOCUS_REQUEST_FAILED) {
                Log.w("[Audio Helper] Call audio focus request failed");
            } else if (result == AudioManager.AUDIOFOCUS_REQUEST_DELAYED) {
                Log.w("[Audio Helper] Call audio focus request delayed");
            }

            releaseCallAudioFocus();
        }
    }

    public void releaseCallAudioFocus() {
        if (mCallRequest != null) {
            int result = AudioManagerCompat.abandonAudioFocusRequest(mAudioManager, mCallRequest);
            if (result == AudioManager.AUDIOFOCUS_REQUEST_GRANTED) {
                Log.i("[Audio Helper] Call audio focus request abandonned");
                mCallRequest = null;
            } else {
                Log.e("[Audio Helper] Call audio focus abandon request failed");
            }
            setAudioManagerInNormalMode();
        } else {
            if (!isAudioFocusDisabled()) {
                Log.i("[Audio Helper] Call audio focus request was already abandonned");
            }
        }
    }

    @Override
    public void onAudioFocusChange(int focusChange) {
        switch (focusChange) {
            case AudioManager.AUDIOFOCUS_GAIN:
                Log.i("[Audio Helper] Focus gained");
                break;
            case AudioManager.AUDIOFOCUS_LOSS:
                Log.w("[Audio Helper] Focus lost");
                if (CoreManager.isReady()) CoreManager.instance().onAudioFocusLost();
                break;
            case AudioManager.AUDIOFOCUS_LOSS_TRANSIENT:
                Log.w("[Audio Helper] Focus lost (transient)");
                if (CoreManager.isReady()) CoreManager.instance().onAudioFocusLost();
                break;
            case AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK:
                Log.w("[Audio Helper] Focus lost (transient, can duck)");
                //if (CoreManager.isReady()) CoreManager.instance().onAudioFocusLost();
                break;
        }
    }

    public void setAudioManagerInCommunicationMode() {
        Log.i("[Audio Helper] Setting audio manager in communication mode");
        mAudioManager.setMode(AudioManager.MODE_IN_COMMUNICATION);
    }

    public void setAudioManagerInNormalMode() {
        Log.i("[Audio Helper] Setting audio manager in normal mode");
        mAudioManager.setMode(AudioManager.MODE_NORMAL);
    }

    private void routeAudioToEarpiece() {
        if (!CoreManager.isReady()) {
            Log.e("[Audio Helper] CoreManager has been destroyed already!");
            return;
        }

        // Let's restore the default output device before the echo calibration or test
        Core core = CoreManager.instance().getCore();
        core.setDefaultOutputAudioDevice(mPreviousDefaultOutputAudioDevice);
        Log.i("[Audio Helper] Restored previous default output audio device: " + mPreviousDefaultOutputAudioDevice);
        mPreviousDefaultOutputAudioDevice = null;
    }

    private void routeAudioToSpeaker() {
        if (!CoreManager.isReady()) {
            Log.e("[Audio Helper] CoreManager has been destroyed already!");
            return;
        }

        // For echo canceller calibration & echo tester, we can't change the soundcard dynamically as the stream isn't created yet...
        Core core = CoreManager.instance().getCore();
        mPreviousDefaultOutputAudioDevice = core.getDefaultOutputAudioDevice();
        if (mPreviousDefaultOutputAudioDevice.getType() == AudioDevice.Type.Speaker) {
            Log.i("[Audio Helper] Current default output audio device is already the speaker: " + mPreviousDefaultOutputAudioDevice);
            return;
        }
        Log.i("[Audio Helper] Saved current default output audio device: " + mPreviousDefaultOutputAudioDevice);
        
        for (AudioDevice audioDevice : core.getAudioDevices()) {
            if (audioDevice.getType() == AudioDevice.Type.Speaker) {
                Log.i("[Audio Helper] Found speaker device, replacing default output audio device with: " + audioDevice);
                core.setDefaultOutputAudioDevice(audioDevice);
                return;
            }
        }
        Log.e("[Audio Helper] Couldn't find speaker audio device");
    }

    private void playSoundUsingMediaPlayer(Context context, AudioAttributes audioAttrs, String ringtone) {
        Log.i("[Audio Helper] Trying to play ringtone [", ringtone, "]");

        mPlayer = new MediaPlayer();
        mPlayer.setAudioAttributes(audioAttrs);

        try {
            if (ringtone.startsWith("content://")) {
                mPlayer.setDataSource(context, Uri.parse(ringtone));
            } else {
                FileInputStream fis = new FileInputStream(ringtone);
                mPlayer.setDataSource(fis.getFD());
                fis.close();
            }

            mPlayer.prepare();
            mPlayer.setLooping(true);
            mPlayer.start();
            Log.i("[Audio Helper] Media player ringing started");
        } catch (IOException ioe) {
            Log.e("[Audio Helper] Cannot play ringtone [", ringtone, "]: ", ioe);
        } catch (SecurityException se) {
            Log.e("[Audio Helper] Cannot play ringtone [", ringtone, "]: ", se);
        }
    }

    private boolean isAudioFocusDisabled() {
        if (!CoreManager.isReady()) {
            Log.e("[Audio Helper] CoreManager has been destroyed already!");
            return false;
        }
        return CoreManager.instance().getCore().getConfig().getBool("audio", "android_disable_audio_focus_requests", false);
    }
}