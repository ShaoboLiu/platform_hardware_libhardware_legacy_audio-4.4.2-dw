/*
 **
 ** Copyright 2007, The Android Open Source Project
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

#define LOG_NDEBUG 0
#define LOG_TAG "AudioHardware"
#include <utils/Log.h>
#include <fcntl.h>
#include <cutils/properties.h>

#include "AudioHardware.h"
#include <media/AudioRecord.h>

namespace android_audio_legacy {

// ----------------------------------------------------------------------------

#define SND_DEV_NAME "/dev/snd/actsnd"
static char const * const kAudioPlaybackName = SND_DEV_NAME;
static char const * const kAudioCaptureName = SND_DEV_NAME;
static char const * const kAudioControlName = SND_DEV_NAME;

#define _DEBUG_DAC_
#define _DEBUG_ADC_

#define SPK_STATE "/sys/class/switch/h2w/state"
static char const * const kSpkState = SPK_STATE;
/* */

extern "C" {
#include "loop_modem.h"
static FILE * reading_audio_file = NULL;
#define INPUT_FILE "/data/local/audio.wav"
// WAVE file header format
struct WAV_HEADER {
	unsigned char riff[4];						// RIFF string
	unsigned int overall_size	;				// overall size of file in bytes
	unsigned char wave[4];						// WAVE string
	unsigned char fmt_chunk_marker[4];			// fmt string with trailing null char
	unsigned int length_of_fmt;					// length of the format data
	unsigned short format_type;					// format type. 1-PCM, 3- IEEE float, 6 - 8bit A law, 7 - 8bit mu law
	unsigned short channels;						// no.of channels
	unsigned int sample_rate;					// sampling rate (blocks per second)
	unsigned int byterate;						// SampleRate * NumChannels * BitsPerSample/8
	unsigned short block_align;					// NumChannels * BitsPerSample/8
	unsigned short bits_per_sample;				// bits per sample, 8- 8bits, 16- 16 bits etc
	unsigned char data_chunk_header [4];		// DATA string or FLLR string
	unsigned int data_size;						// NumSamples * NumChannels * BitsPerSample/8 - size of the next chunk that will be read
};

static void test_yep_read_from_file(char *buff, int n_read)
{
	if(reading_audio_file == NULL){
       reading_audio_file = fopen(INPUT_FILE, "rb");
       fseek(reading_audio_file, 0L + sizeof(struct WAV_HEADER) , SEEK_SET);
	}
	int nread = 0;
	memset(buff, 0, n_read);

	nread = fread(buff, 1, n_read, reading_audio_file);
	if(nread == 0 ){
         printf(" read_from_file()>> input file reach the end, back to the begin\n");
	     fseek(reading_audio_file, 0L + sizeof(struct WAV_HEADER) , SEEK_SET);
	}

}

};
//read integer from file, return -1 if failed.
int cat_file(const char *filename) {
    FILE *fp = NULL;
    int ret;
    fp = fopen(filename, "r");
    if (!fp) {
        ALOGW("can't open %s\n", filename);
        return -1;
    }
    if (fscanf(fp, "%d", &ret)) {
        fclose(fp);
        fp = NULL;
        return ret;
    }
    fclose(fp);
    fp = NULL;
    return -1;
}

#ifdef _DEBUG_DAC_
FILE *dac_fp = NULL;
#endif

#ifdef _DEBUG_ADC_
FILE *adc_fp = NULL;
#endif

// ----------------------------------------------------------------------------

AudioHardware::AudioHardware() :
	mOutput(0), mInput(0), mMicMute(false), mFd(-1) {
}

AudioHardware::~AudioHardware() {
	if (mFd > 0) {
		::close(mFd);
		mFd = -1;
	}

	closeOutputStream((AudioStreamOut *) mOutput);
	closeInputStream((AudioStreamIn *) mInput);
}

status_t AudioHardware::initCheck() {
	return NO_ERROR;
}

AudioStreamOut* AudioHardware::openOutputStream(uint32_t devices, int *format,
		uint32_t *channels, uint32_t *sampleRate, status_t *status) {
	AutoMutex lock(mLock);

	// only one output stream allowed
	if (mOutput) {
		if (status) {
			*status = INVALID_OPERATION;
		}
		return 0;
	}

	// create new output stream
	AudioStreamOutACTxx* out = new AudioStreamOutACTxx();
	status_t lStatus = out->set(this, devices, format, channels, sampleRate);
	if (status) {
		*status = lStatus;
	}
	if (lStatus == NO_ERROR) {
		mOutput = out;
	} else {
		delete out;
	}

	return mOutput;
}

void AudioHardware::closeOutputStream(AudioStreamOut* out) {
	if (mOutput && out == mOutput) {
		delete mOutput;
		mOutput = 0;
	}
}

AudioStreamIn* AudioHardware::openInputStream(uint32_t devices, int *format,
		uint32_t *channels, uint32_t *sampleRate, status_t *status,
		AudioSystem::audio_in_acoustics acoustics) {
	// check for valid input source
	if (!AudioSystem::isInputDevice((AudioSystem::audio_devices) devices)) {
		return 0;
	}

	AutoMutex lock(mLock);

	// support more than one input stream
	if (mInput) {
		ALOGD("more than one input");
//		//only one output stream allowed
//		if (status) {
//			*status = INVALID_OPERATION;
//		}
//		return 0;
	}

	// create new output stream
	AudioStreamInACTxx* in = new AudioStreamInACTxx();
	status_t lStatus = in->set(this, devices, format, channels, sampleRate,
			acoustics);
	if (status) {
		*status = lStatus;
	}
	if (lStatus == NO_ERROR) {
		mInput = in;
	} else {
		delete in;
	}
	return mInput;
}

void AudioHardware::closeInputStream(AudioStreamIn* in) {
	if (mInput && in == mInput) {
		delete mInput;
		mInput = 0;
		in = 0;
	}
	
	//support more input stream
	if (in) {
		ALOGD("mInput != in");
		delete in;
		in = 0;
	}
}


static const char* routingModeStrings[] =
{
    "OUT OF RANGE",
    "INVALID",
    "CURRENT",
    "NORMAL",
    "RINGTONE",
    "IN_CALL",
    "IN_COMMUNICATION"
};

static const char* routeNone = "NONE";
static int in_call = 0;

static const char* displayMode(int mode)
{
    if ((mode < AudioSystem::MODE_INVALID) || (mode >= AudioSystem::NUM_MODES))
        return routingModeStrings[0];
    return routingModeStrings[mode+3];
}

status_t AudioHardware::setMode(int mode) {
	ALOGV("AudioHardware::setMode()>> Mode: %d", mode);
	ALOGV("AudioHardware::setMode()>> Mode: %s", displayMode(mode));
	if(mode == 2){
        in_call = 1;
		mOutput->standby();
		start_voice_on_modem();
		/**/
	}else{
		in_call = 0;
		stop_voice_on_modem();
	}
	return NO_ERROR;
}

status_t AudioHardware::setVoiceVolume(float v) {
	ALOGV("AudioHardware::setVoiceVolume()>> v = %f", v);
	return NO_ERROR;
}

status_t AudioHardware::setMasterVolume(float v) {
	// Implement: set master volume
	ALOGV("AudioHardware::setMasterVolume()>> v = %f", v);
	if (v < 0.0) {
		v = 0.0;
	} else if (v > 1.0) {
		v = 1.0;
	}

	int vol = int(v * 40);
	if (getFd() > 0) {
	    ALOGV("AudioStreamOutACTxx::setMasterVolume()>> vol = %d",vol);
		ioctl(mFd, SOUND_MIXER_WRITE_VOLUME, &vol);
		::close(mFd);
		mFd = -1;
		
	}

	return NO_ERROR;
}

status_t AudioHardware::setMicMute(bool state) {
	int micFlag = 0;
	micFlag = (int) state;
	ALOGV("AudioHardware::setMicMute()>> state = %d", micFlag);
	if (getFd() > 0) {
		ioctl(mFd, SOUND_MIXER_WRITE_MIC, &micFlag);
		::close(mFd);
		mFd = -1;
	}

	mMicMute = state;
	return NO_ERROR;
}

status_t AudioHardware::getMicMute(bool* state) {	
	*state = mMicMute;
	return NO_ERROR;
}

size_t AudioHardware::getInputBufferSize(uint32_t sampleRate, int format,
		int channelCount) {
	if (format != AudioSystem::PCM_16_BIT) {
		ALOGW("getInputBufferSize bad format: %d", format);
		return 0;
	}
	if (channelCount < 1 || channelCount > 2) {
		ALOGW("getInputBufferSize bad channel count: %d", channelCount);
		return 0;
	}
	ALOGD("AudioHardware::getInputBufferSize %d ",2048 * channelCount);

	return 2048 * channelCount;
}

int32_t AudioHardware::getFd() {
	if(in_call == 1){

		ALOGI("AudioHardware::getFd()>>  in call return -1");
		return -1;

	}
	if (mFd > 0) {
		return mFd;
	}

	mFd = ::open(kAudioControlName, O_RDONLY);
	if (mFd > 0) {
		ALOGI("open control drv");
		return mFd;
	}

	ALOGE("Cannot open %s read errno: %d, str:%s", kAudioControlName, errno, strerror(errno));
	return -1;
}

status_t AudioHardware::dumpInternals(int fd, const Vector<String16>& args) {
	const size_t SIZE = 256;
	char buffer[SIZE];
	String8 result;
	result.append("AudioHardware::dumpInternals\n");
	snprintf(buffer, SIZE, "\tmMicMute: %s\n", mMicMute ? "true" : "false");
	result.append(buffer);
	::write(fd, result.string(), result.size());
	return NO_ERROR;
}

status_t AudioHardware::dump(int fd, const Vector<String16>& args) {
	dumpInternals(fd, args);
	if (mInput) {
		mInput->dump(fd, args);
	}
	if (mOutput) {
		mOutput->dump(fd, args);
	}
	return NO_ERROR;
}

// ----------------------------------------------------------------------------
AudioStreamOutACTxx::AudioStreamOutACTxx() :
	mHardware(0), mFd(-1), mChannels(AudioSystem::CHANNEL_OUT_STEREO),
			mDevice(0), mStandby(true), mOutMode(O_MODE_I2S_2), mSpeakerOn(0),
			mFilterOn(0) {
	/* fragsize = (1 << 11) = 2048, fragnum = 3, about 50ms per dma transfer */
	mFragShift = 10;
	mFragNum = 10;
	
	filterConfig();
	
	if (mFilterType != NO_FILTER) {	
	    ALOGD("use filter type:%d", mFilterType);
		pout = (int16_t *) malloc(FILTER_SIZE);
		poutleft = (real *) malloc(FILTER_SIZE);
		poutright = (real *) malloc(FILTER_SIZE);
		pinleft = (real *) malloc(FILTER_SIZE);
		pinright = (real *) malloc(FILTER_SIZE);	
		/* TODO: how to free malloc */
	}else{

	    ALOGD("No filter for output for this design");
	}

}

status_t AudioStreamOutACTxx::set(AudioHardware *hw, uint32_t devices,
		int *pFormat, uint32_t *pChannels, uint32_t *pRate) {
	int lFormat = pFormat ? *pFormat : 0;
	uint32_t lChannels = pChannels ? *pChannels : 0;
	uint32_t lRate = pRate ? *pRate : 0;

	// fix up defaults
	if (lFormat == 0)
		lFormat = format();
	if (lChannels == 0)
		lChannels = channels();
	if (lRate == 0)
		lRate = sampleRate();

	// check values
	if ((lFormat != format()) || (lChannels != channels()) || (lRate
			!= sampleRate())) {
		if (pFormat)
			*pFormat = format();
		if (pChannels)
			*pChannels = channels();
		if (pRate)
			*pRate = sampleRate();
		return BAD_VALUE;
	}

	if (pFormat)
		*pFormat = lFormat;
	if (pChannels)
		*pChannels = lChannels;
	if (pRate)
		*pRate = lRate;

	mHardware = hw;
	mDevice = devices;
	return NO_ERROR;
}

AudioStreamOutACTxx::~AudioStreamOutACTxx() {
	
    ALOGI("YEP Inside: ~AudioStreamOutACTxx()>> ");
#ifdef _DEBUG_DAC_
    if (dac_fp != NULL) {
    	ALOGD("dac_fp != NULL");
        fclose(dac_fp);
        dac_fp = NULL;
    }
#endif

	standby();
    ALOGI("YEP Inside: ~AudioStreamOutACTxx()<< ");
}

#if 0
uint32_t AudioStreamOutACTxx::latency() {
	uint32_t mLatency = 0;
	if (mFd > 0) {
		ioctl(mFd, SNDRV_GINUSE, &mLatency);
		mLatency = mLatency * 1000 / 44100;
	} else {
		mLatency = 30;
	}
	return mLatency;
}
#endif

status_t AudioStreamOutACTxx::setVolume(float l, float r) {
	status_t status = NO_INIT;
	if (l != r)
		return INVALID_OPERATION;
	if (l < 0.0) {
		l = 0.0;
	} else if (l > 1.0) {
		l = 1.0;
	}
	ALOGV("AudioStreamOutACTxx::setVolume()>> l = %f, r = %f", l, r);

	int vol = int(l * 40);
	vol = 40;
	if (getFd() > 0) {
	    ALOGV("AudioStreamOutACTxx::setVolume()>> vol = %d",vol);
		ioctl(mFd, SOUND_MIXER_WRITE_VOLUME, &vol);
	}

	return NO_ERROR;
}

ssize_t AudioStreamOutACTxx::write(const void* buffer, size_t bytes) {
	Mutex::Autolock _l(mLock);
	status_t status = NO_INIT;
	const int16_t* p = static_cast<const int16_t*> (buffer);
	
	int count = 0;
	int samples = bytes / 4;

	//ALOGI("YEP inside Debug: AudioStreamOutACTxx::write()>> ");
	//ALOGI("YEP inside Debug: AudioStreamOutACTxx::write()>> buffer_length = %d",bytes );
	//ALOGI("YEP inside Debug: AudioStreamOutACTxx::write()>> frameSize(): %d", frameSize());
	if (mStandby) {
		mEnable = false;
		if (getFd() > 0) {
        	char value[PROPERTY_VALUE_MAX];
        	/*
        	 * if .bypass enable, need to decide whether .playback is enable or not
        	 * otherwise, always enable playback
        	 * */
        	property_get("ro.actions.audio.bypass", value, "disable");
        	if (strncmp(value, "enable", 6) == 0) {
        		property_get("hw.actions.audio.playback", value, "disable");
        	} else {
        		strcpy(value, "enable");        		
        	}

			if (strncmp(value, "enable", 6) == 0) {
				int args = 0;
				args = (mFragNum << 16) | mFragShift;
				ioctl(mFd, SNDCTL_DSP_SETFRAGMENT, &args);
		        ALOGI("YEP inside Debug: AudioStreamOutACTxx::write()>> SNDCTL_DSP_SETFRAGMENT: args: 0x%x", args);

				args = sampleRate();
				ioctl(mFd, SNDCTL_DSP_SPEED, &args);
		        ALOGI("YEP inside Debug: AudioStreamOutACTxx::write()>> SNDCTL_DSP_SPEED: args: %d", args);

				args = AudioSystem::popCount(mChannels);
				ioctl(mFd, SNDCTL_DSP_CHANNELS, &args);
		        ALOGI("YEP inside Debug: AudioStreamOutACTxx::write()>> SNDCTL_DSP_CHANNELS: args: %d", args);
				
				ioctl(mFd, SNDRV_SSPEAKER, &mSpeakerOn);
		        ALOGI("YEP inside Debug: AudioStreamOutACTxx::write()>> SNDRV_SSPEAKER: mSpeakerOn: %d", mSpeakerOn);
    			ioctl(mFd, SNDRV_SOUTMODE, &mOutMode);
		        ALOGI("YEP inside Debug: AudioStreamOutACTxx::write()>> SNDRV_SOUTMODE: mOutMode: %d", mOutMode);

				mEnable = true;
			}
		}
		mStandby = false;		
	}

	if (mEnable) {

#ifdef _DEBUG_DAC_
        if (dac_fp != NULL) {
            fwrite(p, 1, bytes, dac_fp);
        }
#endif

		if ((bytes & 0x1f) != 0) {
			ALOGE("write bytes should be burst 8, bytes:%d ", bytes);
		}
		
		if (mFilterType != NO_FILTER) {
			if (mFilterOn == 1) {
				int left_val, right_val;
				
				if (bytes > FILTER_SIZE) {
					//ALOGV("=========bytes is too large for filter");
					/* write FILTER_SIZE bytes at most, considering deal with the malloc buffer */
					samples = FILTER_SIZE / 4;
				}
				/* deinterlearing */
				for (count = 0; count < samples; count++) {
					*pinleft++ = (real)(*p++);
					*pinright++ = (real)(*p++);
				}
				
				pinleft -= samples;
				pinright -= samples;

				if (mFilterType == BAND_PASS) {
					iir_filter(mFilter.left1, pinleft, poutleft, mFilterCoef.b0, mFilterCoef.a0, samples);
					iir_filter(mFilter.left2, poutleft, pinleft, mFilterCoef.b1, mFilterCoef.a1, samples);
					iir_filter(mFilter.right1, pinright, poutright, mFilterCoef.b0, mFilterCoef.a0, samples);
					iir_filter(mFilter.right2, poutright, pinright, mFilterCoef.b1, mFilterCoef.a1, samples);
					/* interlacing */
					for (count = 0; count < samples; count++) {
						left_val = *pinleft++;
						right_val = *pinright++;
						*pout++ = (int16_t)SATURATE(left_val);
						*pout++ = (int16_t)SATURATE(right_val);
					}
					pinleft -= samples;
					pinright -= samples;
				} else {
					iir_filter(mFilter.left1, pinleft, poutleft, mFilterCoef.b0, mFilterCoef.a0, samples);
					iir_filter(mFilter.right1, pinright, poutright, mFilterCoef.b0, mFilterCoef.a0, samples);
					/* interlacing */
					for (count = 0; count < samples; count++) {
						left_val = *poutleft++;
						right_val = *poutright++;
						*pout++ = (int16_t)SATURATE(left_val);
						*pout++ = (int16_t)SATURATE(right_val);
					}
					poutleft -= samples;
					poutright -= samples;
				}

				pout -= samples * 2;
				
				/* attention: we just want to pass value of bytes, no releted to type of pout */
				count = ssize_t(::write(mFd, pout, samples * 4));
				
				return count;
			} else {
				count = ssize_t(::write(mFd, p, bytes));
	            ALOGI("YEP inside Debug: AudioStreamOutACTxx::write() count = %d  << ", count);
                return count;
			}
		} else {
            //test_yep_read_from_file((char *)p, bytes);
			count = ssize_t(::write(mFd, p, bytes));
	        //ALOGI("YEP inside Debug: AudioStreamOutACTxx::write() NO filtre applied << ");
	        //ALOGI("YEP inside Debug: AudioStreamOutACTxx::write() count = %d  << ", count);
            return count;
		}
	} else {
		// Simulate audio output timing in case of error
		usleep(bytes * 1000000 / frameSize() / sampleRate());
	    ALOGI("YEP inside Debug: AudioStreamOutACTxx::write()<< ");
		return bytes;
	}
}

status_t AudioStreamOutACTxx::standby() {
    ALOGI("YEP Inside: AudioStreamOutACTxx::standby()>> ");
	if (!mStandby && mFd > 0) {
        ALOGI("YEP Inside: AudioStreamOutACTxx::standby()>> close mFd");
		::close(mFd);
		mFd = -1;
		
		if (mFilterType != NO_FILTER) {
			filterInitZero(&mFilter);
		}
        //ALOGI("YEP Inside: AudioStreamOutACTxx::standby()>> dump outputStream >.");
        //yep_dump_outputStream();
        //ALOGI("YEP Inside: AudioStreamOutACTxx::standby()>> dump outputStream <.");
	}

	mStandby = true;

    ALOGI("YEP Inside: AudioStreamOutACTxx::standby()<< ");
	return NO_ERROR;
}

int32_t AudioStreamOutACTxx::getFd() {

	if(in_call == 1){
    	ALOGD("AudioStreamOutACTxx::getFd()>> in Call return -1");
		return -1;
	}

	if (mFd > 0) {
		return mFd;
	}
	
#ifdef _DEBUG_DAC_
    if (dac_fp != NULL) {
        fclose(dac_fp);
        dac_fp = NULL;
    }
    dac_fp = fopen("/data/czx/dac_tmp.pcm", "wb");
    if (dac_fp == NULL) {
    	ALOGE("/data creat dac_tmp file failed! errno:%d, str:%s", errno, strerror(errno));
    } else {
    	ALOGD("/data open dac_tmp.pcm.");
    }
#endif

	mFd = ::open(kAudioPlaybackName, O_RDWR);
	if (mFd > 0) {
		ALOGI("YEP debug audio: open playback drv");
		return mFd;
	}

	ALOGE("Cannot open %s write, errno: %d, str:%s", kAudioPlaybackName, errno, strerror(errno));

	return -1;
}

static int32_t filterCoef(int filterType, int freqLevel, filter_coef_t *coef) {
	ALOGD("in func:%s, filterType:%d, freqLevel:%d", __FUNCTION__, filterType, freqLevel);
	int retVal = 0;
	switch (filterType) {
	
	case NO_FILTER:
		coef->a0 = NULL;
		coef->b0 = NULL;
		coef->a1 = NULL;
		coef->b1 = NULL;
		retVal = 0;
		break;
		
	case HIGH_PASS:
		if ((freqLevel < 0) || (freqLevel > 2)) {
			retVal = -1;
		} else {
			coef->a0 = iir_hp_a[freqLevel];
			coef->b0 = iir_hp_b[freqLevel];
			coef->a1 = NULL;
			coef->b1 = NULL;
			retVal = 0;
		}
		break;
		
	case BAND_PASS:
		if (freqLevel != 0) {
			retVal = -1;
		} else {
			coef->a0 = iir_bp_a0[freqLevel];
			coef->b0 = iir_bp_b0[freqLevel];
			coef->a1 = iir_bp_a1[freqLevel];
			coef->b1 = iir_bp_b1[freqLevel];
			retVal = 0;
		}
		break;
	
	default:
		retVal = -1;
		break;
	}
	
	return retVal;
}

static void filterMalloc(int filterType, filter_t *filter) {
	switch (filterType) {
	case NO_FILTER:
		filter->left1 = NULL;
		filter->right1 = NULL;
		filter->left2 = NULL;
		filter->right2 = NULL;
		break;
	
	case HIGH_PASS:
		filter->left1 = (iir_filter_t *) malloc(sizeof(iir_filter_t));
		filter->right1 = (iir_filter_t *) malloc(sizeof(iir_filter_t));
		memset(filter->left1, 0, sizeof(iir_filter_t));
		memset(filter->right1, 0, sizeof(iir_filter_t));
		filter->left2 = NULL;
		filter->right2 = NULL;
		break;
	
	case BAND_PASS:
		filter->left1 = (iir_filter_t *) malloc(sizeof(iir_filter_t));
		filter->right1 = (iir_filter_t *) malloc(sizeof(iir_filter_t));
		memset(filter->left1, 0, sizeof(iir_filter_t));
		memset(filter->right1, 0, sizeof(iir_filter_t));
		
		filter->left2 = (iir_filter_t *) malloc(sizeof(iir_filter_t));
		filter->right2 = (iir_filter_t *) malloc(sizeof(iir_filter_t));
		memset(filter->left2, 0, sizeof(iir_filter_t));
		memset(filter->right2, 0, sizeof(iir_filter_t));
		break;
	
	default:
		break;
	}
}

void AudioStreamOutACTxx::filterInitZero(filter_t *filter) {

	if (filter->left1 != NULL) {
		memset(filter->left1, 0, sizeof(iir_filter_t));
	}
	if (filter->right1 != NULL) {
		memset(filter->right1, 0, sizeof(iir_filter_t));
	}
	if (filter->left2 != NULL) {
		memset(filter->left2, 0, sizeof(iir_filter_t));
	}
	if (filter->right2 != NULL) {
		memset(filter->right2, 0, sizeof(iir_filter_t));
	}
}

void AudioStreamOutACTxx::filterConfig() {
	char filterType[PROPERTY_VALUE_MAX];
	char lowFreq[PROPERTY_VALUE_MAX];
	char highFreq[PROPERTY_VALUE_MAX];
	int freqLevel = 0;
	int retVal = 0;
	property_get("ro.audiohal.filtertype", filterType, "nofilter");
    ALOGD("value:%s", filterType);
	if (strncmp(filterType, "bandpass", 8) == 0) {
		property_get("ro.audiohal.lowfreq", lowFreq, "0");
		property_get("ro.audiohal.highfreq", highFreq, "0");
		if ((strncmp(lowFreq, "700", 3) == 0) && 
			(strncmp(highFreq, "7000", 4) == 0)) {
			mFilterType = BAND_PASS;
			freqLevel = 0;
			ALOGD("use band pass filter");
		} else {
			ALOGD("no support cutoff freq:%s ~ %s in band pass filter", lowFreq, highFreq);
			mFilterType = NO_FILTER;
		}
	} else if (strncmp(filterType, "highpass", 8) == 0) {
		property_get("ro.audiohal.lowfreq", lowFreq, "0");
		if ((strncmp(lowFreq, "300", 3) == 0)) {
			mFilterType = HIGH_PASS;
			freqLevel = 0;
			ALOGD("use high pass filter, cutoff freq:%s", lowFreq);
		} else if ((strncmp(lowFreq, "500", 3) == 0)) {
			mFilterType = HIGH_PASS;
			freqLevel = 1;
			ALOGD("use high pass filter, cutoff freq:%s", lowFreq);
		} else if ((strncmp(lowFreq, "700", 3) == 0)) {
			mFilterType = HIGH_PASS;
			freqLevel = 2;
			ALOGD("use high pass filter, cutoff freq:%s", lowFreq);
		} else {
			mFilterType = NO_FILTER;
			ALOGD("no support cutoff freq:%s in high pass filter", lowFreq);
		}
	} else {
		mFilterType = NO_FILTER;
		ALOGD("no filter");
	}

	retVal = filterCoef(mFilterType, freqLevel, &mFilterCoef);
	if (retVal != 0) {
		ALOGD("filterCoef fail, no filter");
		mFilterType = NO_FILTER;
	}
	filterMalloc(mFilterType, &mFilter);
}

status_t AudioStreamOutACTxx::yep_dump_outputStream() {
	const size_t SIZE = 256;
	char buffer[SIZE];
	String8 result;
	snprintf(buffer, SIZE, "YEP: yep_dump_outputStream()\n");
	result.append(buffer);
	snprintf(buffer, SIZE, "\tsample rate: %d\n", sampleRate());
	result.append(buffer);
	snprintf(buffer, SIZE, "\tbuffer size: %d\n", bufferSize());
	result.append(buffer);
	snprintf(buffer, SIZE, "\tchannels: %d\n", channels());
	result.append(buffer);
	snprintf(buffer, SIZE, "\tformat: %d\n", format());
	result.append(buffer);
	snprintf(buffer, SIZE, "\tdevice: %d\n", mDevice);
	result.append(buffer);
	snprintf(buffer, SIZE, "\tmAudioHardware: %p\n", mHardware);
	result.append(buffer);
	snprintf(buffer, SIZE, "\tmFd: %d\n", mFd);
	result.append(buffer);
	ALOGV("yep_dump() %s", result.string());

	return NO_ERROR;
}


status_t AudioStreamOutACTxx::dump(int fd, const Vector<String16>& args) {
	const size_t SIZE = 256;
	char buffer[SIZE];
	String8 result;
	snprintf(buffer, SIZE, "AudioStreamOutACTxx::dump\n");
	result.append(buffer);
	snprintf(buffer, SIZE, "\tsample rate: %d\n", sampleRate());
	result.append(buffer);
	snprintf(buffer, SIZE, "\tbuffer size: %d\n", bufferSize());
	result.append(buffer);
	snprintf(buffer, SIZE, "\tchannels: %d\n", channels());
	result.append(buffer);
	snprintf(buffer, SIZE, "\tformat: %d\n", format());
	result.append(buffer);
	snprintf(buffer, SIZE, "\tdevice: %d\n", mDevice);
	result.append(buffer);
	snprintf(buffer, SIZE, "\tmAudioHardware: %p\n", mHardware);
	result.append(buffer);
	snprintf(buffer, SIZE, "\tmFd: %d\n", mFd);
	result.append(buffer);
	::write(fd, result.string(), result.size());
	return NO_ERROR;
}

status_t AudioStreamOutACTxx::setParameters(const String8& keyValuePairs) {
	AudioParameter param = AudioParameter(keyValuePairs);
	String8 keyRouting = String8(AudioParameter::keyRouting);
	status_t status = NO_ERROR;
	int device, state= -1;
    ALOGI("YEP inside: AudioStreamOutACTxx::setParameters()>> ");
	ALOGV("AudioStreamOutACTxx setParameters() %s", keyValuePairs.string());

	if (param.getInt(keyRouting, device) == NO_ERROR) {
		mDevice = device;

		ALOGI("YEP inside: AudioStreamOutACTxx::setParameters()>> set output routing %x, mSpeakerOn:%d, mStandby:%s",
			mDevice, mSpeakerOn, (mStandby == true) ? "true" : "false");

		// FIXME setForceUse: speaker may not effective when headphone is on.
		if ((device & AudioSystem::DEVICE_OUT_AUX_DIGITAL) == AudioSystem::DEVICE_OUT_AUX_DIGITAL) {
			mOutMode = O_MODE_HDMI;
			mSpeakerOn = 0;
			mFilterOn = 0;
		} else if (((device & AudioSystem::DEVICE_OUT_SPEAKER) == AudioSystem::DEVICE_OUT_SPEAKER) && 
			((device & AudioSystem::DEVICE_OUT_WIRED_HEADPHONE) != AudioSystem::DEVICE_OUT_WIRED_HEADPHONE) &&
			((device & AudioSystem::DEVICE_OUT_WIRED_HEADSET) != AudioSystem::DEVICE_OUT_WIRED_HEADSET)) {
			mOutMode = O_MODE_I2S_2;
			if (mFilterType != NO_FILTER) {
				mFilterOn = 1;
			}
			if (!mStandby) {
				mSpeakerOn = 1;
			} else {
				/* speaker off when mStandby is true */
				mSpeakerOn = 0;
			}
		} else {
			mOutMode = O_MODE_I2S_2;
			mSpeakerOn = 0;
			mFilterOn = 0;
		}
		
		//mFilterOn = 1; //for debug EQ, open this		
		state = cat_file(kSpkState);
		if (getFd() > 0) {
			ioctl(mFd, SNDRV_SSPEAKER, &mSpeakerOn);
			ioctl(mFd, SNDRV_SOUTMODE, &mOutMode);
			ioctl(mFd, SNDRV_SH2WDEVICE, &state);
		    ALOGI("YEP inside: AudioStreamOutACTxx::setParameters()>> SNDRV_SSPEAKER mSpeakerOn: %d", mSpeakerOn);
		    ALOGI("YEP inside: AudioStreamOutACTxx::setParameters()>> SNDRV_SOUTMODE mOutMode: %d", mOutMode);
		    ALOGI("YEP inside: AudioStreamOutACTxx::setParameters()>> SNDRV_SH2WDEVICE : %d", state);
		}else{

           ALOGI("YEP inside: AudioStreamOutACTxx::setParameters()>> do nothing, as getFd < 0");
		}
		if (mFilterType != NO_FILTER) {
			if (mFilterOn == 1) {
				filterInitZero(&mFilter);
			}
		}

		param.remove(keyRouting);
	}

	if (param.size()) {
		status = BAD_VALUE;
	}
	//yep_dump_outputStream();
    ALOGI("YEP inside: AudioStreamOutACTxx::setParameters()<<");
	return status;
}

String8 AudioStreamOutACTxx::getParameters(const String8& keys) {
	AudioParameter param = AudioParameter(keys);
	String8 value;
	String8 key = String8(AudioParameter::keyRouting);

	if (param.get(key, value) == NO_ERROR) {
		param.addInt(key, (int) mDevice);
	}

	ALOGV("getParameters() %s", param.toString().string());
	return param.toString();
}

status_t AudioStreamOutACTxx::getRenderPosition(uint32_t *dspFrames) {
	return INVALID_OPERATION;
}

// ----------------------------------------------------------------------------
AudioStreamInACTxx::AudioStreamInACTxx() :
	mHardware(0), mFd(-1), mDevice(0), mStandby(true) {
	/* fragsize = (1 << 11) = 2048, fragnum = 3, about 50ms per dma transfer */
	mFragShift = 11;
	mFragNum = 3;
	
#ifdef _DEBUG_ADC_
    if (adc_fp != NULL) {
        fclose(adc_fp);
        adc_fp = NULL;
    }
    adc_fp = fopen("/data/czx/adc_tmp.pcm", "wb");
    if (adc_fp == NULL) {
       ALOGE("creat adc_tmp file failed!");
    } else {
    	ALOGD("open adc_tmp.pcm.");
    }
#endif
	
}

// record functions
status_t AudioStreamInACTxx::set(AudioHardware *hw, uint32_t devices,
		int *pFormat, uint32_t *pChannels, uint32_t *pRate,
		AudioSystem::audio_in_acoustics acoustics) {
	if (pFormat == 0 || pChannels == 0 || pRate == 0)
		return BAD_VALUE;
	
	mChannels = AudioSystem::popCount(*pChannels);
	mBufferSize = 2048 * mChannels;
	if(*pRate != sampleRate()) {
		//*pRate = sampleRate();
	}
	ALOGD("AudioStreamInACTxx::set(%p, %d, %d, %u)", hw, *pFormat, *pChannels, *pRate);
	// check values
	//    if ((*pFormat != format()) ||
	//        (*pChannels != channels()) ||
	//        (*pRate != sampleRate())) {
	//        ALOGE("Error opening input channel");
	//        *pFormat = format();
	//        *pChannels = channels();
	//        *pRate = sampleRate();
	//        return BAD_VALUE;
	//    }

	mHardware = hw;
	mDevice = devices;
	return NO_ERROR;
}

AudioStreamInACTxx::~AudioStreamInACTxx() {
	
#ifdef _DEBUG_ADC_
    if (adc_fp != NULL) {
        fclose(adc_fp);
        adc_fp = NULL;
    }
#endif

	standby();
}

status_t AudioStreamInACTxx::setGain(float g) {
	status_t status = NO_INIT;
	if (g < 0.0) {
		g = 0.0;
	} else if (g > 1.0) {
		g = 1.0;
	}

	int gain = int(g * 25);
	if (getFd() > 0) {
	    ALOGV("AudioStreamInACTxx::setGain()>> gain = %d",gain);
		ioctl(mFd, SOUND_MIXER_WRITE_IGAIN, &gain);
	}

	return NO_ERROR;
}
ssize_t AudioStreamInACTxx::copy_one_channel(char* dst, int dst_len, char* src, int src_len)
{
	int i = 0, j=0;
	memset(dst, 0, (src_len >> 1));
	for(i = 0; i < src_len; i++){
		if(i%4 == 0){
			dst[j] = src[i];
			dst[j+1] = src[i+1];
			j+=2;
		}

	}
	return j;

}
#define BUFFER_SIZE_ONE_CHANNEL (4096 + 1024)
ssize_t AudioStreamInACTxx::read(void* buffer, ssize_t bytes) {
	AutoMutex lock(mLock);
	status_t status = NO_INIT;
	int8_t* p = static_cast<int8_t*> (buffer);
	char buffer_tmp[BUFFER_SIZE_ONE_CHANNEL];
	int buffer_tmp_len =0;

	if (mStandby) {
		mEnable = false;
		if (getFd() > 0) {
        	char value[PROPERTY_VALUE_MAX];
        	property_get("hw.actions.audio.capture", value, "enable");
        	if (strncmp(value, "enable", 6) == 0) {
    			int args = 0;

				/* */
    			args = (mFragNum << 16) | mFragShift;
    			ioctl(mFd, SNDCTL_DSP_SETFRAGMENT, &args);

				/* */
    			args = sampleRate();
    			ioctl(mFd, SNDCTL_DSP_SPEED, &args);

				/* */
    			args = AudioSystem::popCount(mChannels);
    			ioctl(mFd, SNDCTL_DSP_CHANNELS, &args);
    
    			mEnable = true;
    		}
   		}
		
		mStandby = false;
	}
	
	if (mEnable) {
		ssize_t len = ::read(mFd, p, bytes);
	
#ifdef _DEBUG_ADC_
        if (adc_fp != NULL) {
            //ALOGD("adc original len %d", bytes);
            buffer_tmp_len = copy_one_channel(buffer_tmp, BUFFER_SIZE_ONE_CHANNEL, (char*)p,bytes );
            //ALOGD("adc new len %d", buffer_tmp_len);
            //fwrite(p, 1, bytes, adc_fp);
            fwrite(buffer_tmp, 1, buffer_tmp_len, adc_fp);
        } else {
        	adc_fp = fopen("/data/czx/adc_tmp.pcm", "wb");
			    if (adc_fp == NULL) {
			    	ALOGE("creat adc_tmp file failed!");
			    } else {
			    	ALOGD("open adc_tmp.pcm.");
			    }
        }
#endif

		return len;
	} else {
		// Simulate audio input timing in case of error
		memset(p, 0, bytes);
		usleep(bytes * 1000000 / frameSize() / sampleRate());
		return bytes;
	}
}

status_t AudioStreamInACTxx::standby() {

	ALOGI("AudioStreamInACTxx::standby()>> ");
	if (!mStandby && mFd > 0 && (in_call == 0)) {
	    ALOGI("AudioStreamInACTxx::standby()>> close mFd ");
		::close(mFd);
		mFd = -1;
	}else{

	    ALOGI("AudioStreamInACTxx::standby()>> do nothing, as in call");
	}

	mStandby = true;
	ALOGI("AudioStreamInACTxx::standby()<< ");
	return NO_ERROR;
}

int32_t AudioStreamInACTxx::getFd() {
	if(in_call == 1){
		ALOGI("AudioStreamInACTxx::getFd()>> in call return -1");
		return -1;
	}
	if (mFd > 0) {
		return mFd;
	}

	mFd = ::open(kAudioCaptureName, O_RDONLY);
	if (mFd > 0) {
		ALOGI("YEP inside Debug: open capture drv");
		return mFd;
	}

	ALOGE("Cannot open %s read errno: %d, str:%s", kAudioCaptureName, errno, strerror(errno));

	return -1;
}

status_t AudioStreamInACTxx::yep_dump_inputStream() {
	const size_t SIZE = 256;
	char buffer[SIZE];
	String8 result;
	snprintf(buffer, SIZE, "yep_dump_inputStream()p\n");
	result.append(buffer);
	snprintf(buffer, SIZE, "\tsample rate: %d\n", sampleRate());
	result.append(buffer);
	snprintf(buffer, SIZE, "\tbuffer size: %d\n", bufferSize());
	result.append(buffer);
	snprintf(buffer, SIZE, "\tchannels: %d\n", channels());
	result.append(buffer);
	snprintf(buffer, SIZE, "\tformat: %d\n", format());
	result.append(buffer);
	snprintf(buffer, SIZE, "\tdevice: %d\n", mDevice);
	result.append(buffer);
	snprintf(buffer, SIZE, "\tmHardware: %p\n", mHardware);
	result.append(buffer);
	snprintf(buffer, SIZE, "\tmFd: %d\n", mFd);
	result.append(buffer);
	ALOGV("yep_dump() %s", result.string());
	return NO_ERROR;
}


status_t AudioStreamInACTxx::dump(int fd, const Vector<String16>& args) {
	const size_t SIZE = 256;
	char buffer[SIZE];
	String8 result;
	snprintf(buffer, SIZE, "AudioStreamInACTxx::dump\n");
	result.append(buffer);
	snprintf(buffer, SIZE, "\tsample rate: %d\n", sampleRate());
	result.append(buffer);
	snprintf(buffer, SIZE, "\tbuffer size: %d\n", bufferSize());
	result.append(buffer);
	snprintf(buffer, SIZE, "\tchannels: %d\n", channels());
	result.append(buffer);
	snprintf(buffer, SIZE, "\tformat: %d\n", format());
	result.append(buffer);
	snprintf(buffer, SIZE, "\tdevice: %d\n", mDevice);
	result.append(buffer);
	snprintf(buffer, SIZE, "\tmHardware: %p\n", mHardware);
	result.append(buffer);
	snprintf(buffer, SIZE, "\tmFd: %d\n", mFd);
	result.append(buffer);
	::write(fd, result.string(), result.size());
	return NO_ERROR;
}

status_t AudioStreamInACTxx::setParameters(const String8& keyValuePairs) {
	AudioParameter param = AudioParameter(keyValuePairs);
	String8 keyRouting = String8(AudioParameter::keyRouting);
	status_t status = NO_ERROR;
	int device;
	ALOGV("AudioStreamInACTxx setParameters() %s", keyValuePairs.string());

	if (param.getInt(keyRouting, device) == NO_ERROR) {
		mDevice = device;
		ALOGV("set input routing %x", mDevice);
		param.remove(keyRouting);
	}

	if (param.size()) {
		status = BAD_VALUE;
	}
    yep_dump_inputStream();
	return status;
}

String8 AudioStreamInACTxx::getParameters(const String8& keys) {
	AudioParameter param = AudioParameter(keys);
	String8 value;
	String8 keyRouting = String8(AudioParameter::keyRouting);

	if (param.get(keyRouting, value) == NO_ERROR) {
		param.addInt(keyRouting, (int) mDevice);
	}

	ALOGV("getParameters() %s", param.toString().string());

	return param.toString();
}

// ----------------------------------------------------------------------------
extern "C" AudioHardwareInterface* createAudioHardware(void) {
	return new AudioHardware();
}

}; // namespace android
