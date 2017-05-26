/*
** Copyright 2008, The Android Open-Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#ifndef ANDROID_AUDIO_HARDWARE_H
#define ANDROID_AUDIO_HARDWARE_H

#include <stdint.h>
#include <sys/types.h>

#include <utils/threads.h>

#include <hardware_legacy/AudioHardwareBase.h>

#include "filter_coef.h"

#define SNDRV_SOUTMODE                  0xffff0000
#define SNDRV_SSPEAKER					0xffff0001
#define SNDRV_GINUSE                    0xffff0002
#define SNDRV_SH2WDEVICE				0xffff0003

#define FILTER_SIZE						4096
#define SATURATE(x) 	((x)>32767 ? 32767 : ((x)<-32768 ? -32768 : (x)))

enum {
    O_MODE_I2S_2 = 0,
    O_MODE_I2S_51_TDM_A = 1,
    O_MODE_I2S_71_TDM_A = 3,
    O_MODE_SPDIF,
    O_MODE_HDMI = 5,
    I_MODE_GENERAL,
};

enum {
	NO_FILTER = 0,
	HIGH_PASS = 1,
	BAND_PASS = 2,
};

typedef struct {
	iir_filter_t *left1;
	iir_filter_t *left2;
	iir_filter_t *right1;
	iir_filter_t *right2;
} filter_t;

typedef struct {
    real		*a0;
    real		*b0;
    real		*a1;
    real		*b1;
} filter_coef_t;

extern "C" {
#include <linux/soundcard.h>
}

namespace android_audio_legacy {
	using android::Mutex;
	using android::AutoMutex;

class AudioHardware;
// ----------------------------------------------------------------------------
class AudioStreamOutACTxx : public AudioStreamOut {
public:
                        AudioStreamOutACTxx();
    virtual             ~AudioStreamOutACTxx();

    virtual status_t    set(
            AudioHardware *hw,
            uint32_t devices,
            int *pFormat,
            uint32_t *pChannels,
            uint32_t *pRate);

    virtual uint32_t    sampleRate() const { return 44100; }
    virtual size_t      bufferSize() const { return 4096; }
    virtual uint32_t    channels() const { return mChannels; }
    virtual int         format() const { return AudioSystem::PCM_16_BIT; }
    virtual uint32_t    latency() const { return 20; }
    virtual status_t    setVolume(float left, float right);
    virtual ssize_t     write(const void* buffer, size_t bytes);
    virtual status_t    standby();
    virtual status_t    dump(int fd, const Vector<String16>& args);
    virtual status_t    setParameters(const String8& keyValuePairs);
    virtual String8     getParameters(const String8& keys);
    virtual status_t    getRenderPosition(uint32_t *dspFrames);
    virtual status_t    getNextWriteTimestamp(int64_t *timestamp) const { return INVALID_OPERATION; }
            int32_t     getFd();
            void 		filterInitZero(filter_t *filter);
            void		filterConfig();
            status_t yep_dump_outputStream();
            
private:
    AudioHardware *mHardware;
    Mutex       mLock;
    int         mFd;
    uint32_t    mChannels;
    uint32_t 	mFragShift;
    uint32_t	mFragNum;
    uint32_t    mDevice;
    bool        mStandby;
    bool 		mEnable;
    int         mOutMode;
    int         mSpeakerOn;
    int         mFilterOn;
    int	        mFilterType;
    filter_coef_t mFilterCoef;
    filter_t    mFilter;
    int16_t* 	pout;
    real* 		poutleft;
    real* 		poutright;
    real* 		pinleft;
    real* 		pinright;

};

class AudioStreamInACTxx : public AudioStreamIn {
public:
                        AudioStreamInACTxx();
    virtual             ~AudioStreamInACTxx();

    virtual status_t    set(
            AudioHardware *hw,
            uint32_t devices,
            int *pFormat,
            uint32_t *pChannels,
            uint32_t *pRate,
            AudioSystem::audio_in_acoustics acoustics);

    virtual size_t      bufferSize() const { return mBufferSize; }
    virtual uint32_t    channels() const { return AudioSystem::CHANNEL_IN_STEREO; }
    virtual uint32_t    sampleRate() const { return 44100; }
    virtual int         format() const { return AudioSystem::PCM_16_BIT; }
    virtual status_t    setGain(float gain);
    virtual ssize_t     read(void* buffer, ssize_t bytes);
    virtual status_t    dump(int fd, const Vector<String16>& args);
	virtual status_t    standby();
    virtual status_t    setParameters(const String8& keyValuePairs);
    virtual String8     getParameters(const String8& keys);
    virtual uint32_t    getInputFramesLost() const { return 0; }
    virtual status_t 	addAudioEffect(effect_handle_t effect) { return NO_ERROR; }
    virtual status_t 	removeAudioEffect(effect_handle_t effect) { return NO_ERROR; }
			int32_t     getFd();
			status_t yep_dump_inputStream();
			ssize_t  copy_one_channel(char* dst, int dst_len, char* src, int src_len);
private:
    AudioHardware *mHardware;
    Mutex       mLock;
    int         mFd;
    //uint32_t    mSampleRate;    
    uint32_t    mChannels;
    size_t      mBufferSize;
    uint32_t 	mFragShift;
    uint32_t	mFragNum;    
    uint32_t 	mDevice;
    bool        mStandby;
    bool 		mEnable;
};


class AudioHardware : public  AudioHardwareBase
{
public:
                        AudioHardware();
    virtual             ~AudioHardware();
    virtual status_t    initCheck();


    virtual status_t    setVoiceVolume(float volume);
    virtual status_t    setMasterVolume(float volume);
    virtual status_t	getMasterVolume(float volume) const { return INVALID_OPERATION; }

    virtual status_t   	setMode(int mode);

    // mic mute
    virtual status_t    setMicMute(bool state);
    virtual status_t    getMicMute(bool* state);
    // create I/O streams
    virtual AudioStreamOut* openOutputStream(
                                uint32_t devices,
                                int *format=0,
                                uint32_t *channels=0,
                                uint32_t *sampleRate=0,
                                status_t *status=0);

    virtual AudioStreamIn* openInputStream(
                                uint32_t devices,
                                int *format,
                                uint32_t *channels,
                                uint32_t *sampleRate,
                                status_t *status,
                                AudioSystem::audio_in_acoustics acoustics);

    virtual    	void        closeOutputStream(AudioStreamOut* out);
    virtual    	void        closeInputStream(AudioStreamIn* in);

    virtual    	size_t		getInputBufferSize(uint32_t sampleRate, int format, int channelCount);
    			int32_t    	getFd();
protected:
    virtual 	status_t    dump(int fd, const Vector<String16>& args);

private:
    status_t    dumpInternals(int fd, const Vector<String16>& args);

    Mutex                   mLock;
    AudioStreamOutACTxx     *mOutput;
    AudioStreamInACTxx      *mInput;
    bool                    mMicMute;
    int 					mFd;
};

// ----------------------------------------------------------------------------

}; // namespace android

#endif // ANDROID_AUDIO_HARDWARE_H
