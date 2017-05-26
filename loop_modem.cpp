#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <pthread.h>
#include <stdlib.h>
#define LOG_NDEBUG 0
#define LOG_TAG "voice_modem"
#include <utils/Log.h>
#include "AudioHardware.h"
#include <media/AudioRecord.h>
namespace android_audio_legacy {

extern "C" {

#include "serial_port.h"
static pthread_t s_thread_id_reader = -1;
static pthread_t s_thread_id_writer = -1;

#define MAX_BUF_LEN 0x10
int fd_0, fd_1; 
int port_0, port_1;
#define BITS_LENGTH 16
#define OUTPUT_FILE "audio.bin"   
#define INPUT_FILE "/data/local/audio.wav"   
//FILE * output_file;
static FILE * reading_audio_file;
#define LIMIT_OUTPUT_FILESIZE  (1024*1024*8)
static void *modem_reader(void *ptr);
static void *modem_writer(void *ptr);
static int mFd_out = -1;
static int mFd_in = -1;
#define SND_DEV_NAME "/dev/snd/actsnd"
static char const * const kAudioPlaybackName = SND_DEV_NAME;
static char const * const kAudioCaptureName = SND_DEV_NAME;
static int stop_voice = 0;
static struct sigaction signal_actions;
static int modem_reader_alive = 0;
static int modem_writer_alive = 0;
#define MODEM_VOICE_CHANNEL 2
static pid_t	child_pid;

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

#define _DEBUG_MIC_INPUT_
#define _DEBUG_SP_OUTPUT_

#ifdef _DEBUG_MIC_INPUT_
#define DEBUG_MIC_FILE "/data/czx/debug_mic_tmp.pcm"
FILE * debug_mic_fp = NULL;
#endif

#ifdef _DEBUG_SP_OUTPUT_
#define DEBUG_SP_FILE "/data/czx/debug_sp_tmp.pcm"
FILE * debug_sp_fp = NULL;
#endif

void print_out_bits(unsigned int input){
    unsigned int i = 0;
    unsigned int m = 1<< (BITS_LENGTH -1);
    for(i=0; i<BITS_LENGTH; i++){
        if((m>>i)&input){
            printf("1");
        }else{
            printf("0");
        }
		if(i%4 == 3){
           printf(" ");
		}
    }
    printf("\n");
}

static void read_from_file(char *buff, int n_read)
{
	int nread = 0;
	memset(buff, 0, n_read);

	nread = fread(buff, 1, n_read, reading_audio_file);
	if(nread == 0 ){
         printf(" read_from_file()>> input file reach the end, back to the begin\n");
	     fseek(reading_audio_file, 0L + sizeof(struct WAV_HEADER) , SEEK_SET);
	}

}

static void open_snd_card_in()
{
    mFd_in = open(kAudioCaptureName, O_RDONLY);
#ifdef _DEBUG_MIC_INPUT_
    if (debug_mic_fp != NULL) {
        fclose(debug_mic_fp);
        debug_mic_fp = NULL;
    }
    debug_mic_fp = fopen(DEBUG_MIC_FILE, "wb");
    if (debug_mic_fp == NULL) {
       ALOGE("Error on file: %s", DEBUG_MIC_FILE);
    } else {
       ALOGD("OK on openning file: %s", DEBUG_MIC_FILE);
    }
#endif

}

static void close_snd_card_in()
{
	if(mFd_in != -1){
	   close(mFd_in);
       mFd_in = -1;

	}
#ifdef _DEBUG_MIC_INPUT_
    if (debug_mic_fp != NULL) {
        fclose(debug_mic_fp);
        debug_mic_fp = NULL;
    }
#endif

}

static void init_snd_card_in(){
	/* fragsize = (1 << 11) = 2048, fragnum = 3, about 50ms per dma transfer */
	int mFragShift = 11;
	int mFragNum = 3;
	int mSpeakerOn = 0;
	int mOutMode = 0;
	int vol = 40;

	int args = 0;

	if(mFd_in != -1){
		args = (mFragNum << 16) | mFragShift;
    			ioctl(mFd_in, SNDCTL_DSP_SETFRAGMENT, &args);

				/* */
    			args = 8000;
    			ioctl(mFd_in, SNDCTL_DSP_SPEED, &args);

				/* */
    			args = 1;
    			ioctl(mFd_in, SNDCTL_DSP_CHANNELS, &args);
	}
    	
}

static int copy_one_channel(char* dst, int dst_len, char* src, int src_len)
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

static void read_snd_card_in(char *buff, size_t len)
{
	char buff_tmp[2048];
	int nRead;
	int ret_nRead;
	if(mFd_in == -1){
		memset(buff, 0, len);
		ALOGI("read_snd_card_in()>> mFd_in error! ");
		return;
	}

	nRead = read(mFd_in, buff_tmp, len*2);
	if(nRead != len*2){
		memset(buff, 0, len);
		ALOGI("read_snd_card_in()>> nRead error! ");
		return;
	}

//static int copy_one_channel(char* dst, int dst_len, char* src, int src_len)
    ret_nRead = copy_one_channel(buff, len, buff_tmp, nRead);
	//ALOGI("read_snd_card_in()>> read with %d", ret_nRead);

#ifdef _DEBUG_MIC_INPUT_
        if (debug_mic_fp != NULL) {
            //ALOGD("adc original len %d", bytes);
            fwrite(buff, 1, ret_nRead, debug_mic_fp);
        } else {
        	debug_mic_fp = fopen(DEBUG_MIC_FILE, "wb");
			    if (debug_mic_fp == NULL) {
			    	ALOGE("creat adc_tmp file failed!");
			    } else {
			    	ALOGD("open adc_tmp.pcm.");
			    }
        }
#endif




}



static void open_snd_card_out()
{
   mFd_out = open(kAudioPlaybackName, O_RDWR);
 #ifdef _DEBUG_SP_OUTPUT_
    if (debug_sp_fp != NULL) {
        fclose(debug_sp_fp);
        debug_sp_fp = NULL;
    }
    debug_sp_fp = fopen(DEBUG_SP_FILE, "wb");
    if (debug_sp_fp == NULL) {
       ALOGE("Error on open file: %s", DEBUG_SP_FILE);
    } else {
       ALOGD("OK on open file: %s", DEBUG_SP_FILE);
    }
#endif
}

static void close_snd_card_out()
{
	if(mFd_out != -1){
	    close(mFd_out);
        mFd_out = -1;
	}

#ifdef _DEBUG_SP_OUTPUT_
    if (debug_sp_fp != NULL) {
        fclose(debug_sp_fp);
        debug_sp_fp = NULL;
    }
#endif
}


static void write_snd_card_out(char * buff, size_t len)
{
	int nWrite = 0;
	if(mFd_out != -1){
		nWrite = write(mFd_out, buff, len);
		if(nWrite != len ){
		   ALOGI("YEP inside write_snd_card_out()>> write error! ");
		}
#ifdef _DEBUG_SP_OUTPUT_
        if (debug_sp_fp != NULL) {
            //ALOGD("adc original len %d", bytes);
            fwrite(buff, 1, len, debug_sp_fp);
        } else {
        	debug_sp_fp = fopen(DEBUG_SP_FILE, "wb");
			    if (debug_sp_fp == NULL) {
			    	ALOGE("creat sp_out_tmp file failed!");
			    } else {
			    	ALOGD("open sp_out_tmp.pcm.");
			    }
        }
#endif


	}
}


static void init_snd_card_out(){
	/* fragsize = (1 << 11) = 2048, fragnum = 3, about 50ms per dma transfer */
	int mFragShift = 11;
	int mFragNum = 3;
	int mSpeakerOn = 0;
	int mOutMode = 0;
	int vol = 40;

	int args = 0;
		args = (mFragNum << 16) | mFragShift;
		ioctl(mFd_out, SNDCTL_DSP_SETFRAGMENT, &args);
		ALOGI("YEP inside Debug: init_snd_card_out()>> SNDCTL_DSP_SETFRAGMENT: args: 0x%x", args);

		args = 8000;
		ioctl(mFd_out, SNDCTL_DSP_SPEED, &args);
		ALOGI("YEP inside Debug: init_snd_card_out()>> SNDCTL_DSP_SPEED: args: %d, test only", args);

		args = 1;
		ioctl(mFd_out, SNDCTL_DSP_CHANNELS, &args);
		ALOGI("YEP inside Debug: init_snd_card_out()>> SNDCTL_DSP_CHANNELS: args: %d, test only", args);
		
		ioctl(mFd_out, SNDRV_SSPEAKER, &mSpeakerOn);
		ALOGI("YEP inside Debug: init_snd_card_out()>> SNDRV_SSPEAKER: mSpeakerOn: %d", mSpeakerOn);

		ioctl(mFd_out, SNDRV_SOUTMODE, &mOutMode);
		ALOGI("YEP inside Debug: init_snd_card_out()>> SNDRV_SOUTMODE: mOutMode: %d", mOutMode);

        ioctl(mFd_out, SOUND_MIXER_WRITE_VOLUME, &vol);
		ALOGI("YEP inside Debug: init_snd_card_out()>> SOUND_MIXER_WRITE_VOLUME: volume: %d", vol);

}

static void modem_writer_loop(int fd){

  int nread, nwrite, i;
    char buff_read[2048];
	int tmp;
	int read_file_len;

    ALOGV("modem_writer_loop()>>  loop in\n");
    while (stop_voice == 0){

		        nread = 320;
				/* before being blocked, sleep 10ms */
				usleep(1000*10);
				if(stop_voice == 1){
					break;
				}
                ALOGV("modem_writer_loop()>>  read from mic ...\n");
                read_snd_card_in(buff_read, nread);               

				/* before being blocked, sleep 10ms */
				usleep(1000*10);
				if(stop_voice == 1){
					break;
				}
                ALOGV("modem_writer_loop()>>  trying write %d to modem ...\n", nread);
                nwrite = write(fd, buff_read, nread);
                ALOGV("modem_writer_loop()>> finished writing %d bytes in modem\n", nwrite);

				if(nwrite != nread){
                     ALOGV("Warning: write buffer full or Not OK\n");
					 break;
				}
	}
    ALOGV("modem_writer_loop()>>  loop out\n");


}


static void modem_reader_loop(int fd){

    int nread, nwrite, i;
    char buff[1024];
    char buff_read[2048];
	int tmp;
	int read_file_len;

    //ALOGV("modem_reader_loop()>>  loop in\n");
    while (( nread = read (fd, buff, sizeof(buff))) > 0){

                ALOGV("modem_reader_loop()>> read %d bytes from modem\n", nread);

				/* test for the audio syncing, 
				 * reading from  the ttyUSB2 is constant, it fix at 320 
				 * I afraid, the input rate should also fixed in 320
				 * now, I test with 480 to see if the audio failure
				 * 
				 * it can just make the voice become more faster, 
				 * but no noise produced
				 * */



		/*
                read_snd_card_in(buff_read, nread);               

                //read_from_file(buff_read, read_file_len);
				//memset(buff_read, 0, read_file_len);

				nwrite = write(fd, buff_read, nread);

				if(nwrite != nread){
                     ALOGV("Warning: write buffer full \n");

				}
				*/

                write_snd_card_out(buff, nread);
				/* saving the recevied data */
				/*
				fwrite(buff, 1, nread, output_file);
                output_file_size += nread;
				if(output_file_size > LIMIT_OUTPUT_FILESIZE){
					fclose(output_file);
					printf("reach the file limit, exit now");
					exit(0);
				}
				*/

				/* showing the MAX buf info */

				/*
            	if(fd == fd_1){
                    printf("On port %d: ", port_1);

	            }else if(fd == fd_0){
                    printf("On port %d: ", port_0);

	            }
				*/

				/*
                i = 1;
                while((i < nread) &&( i < MAX_BUF_LEN)){
                     //printf("%02x:%02x ",  buff[i-1], buff[i]);
					 //tmp = ((buff[i-1] << 8 )|buff[i]);
                     //printf("%02x:%02x ",  buff[i], buff[i-1]);
					 tmp = ((buff[i-1] )|(buff[i]<<8) );
                     print_out_bits(tmp);
                     i++;
                }*/

				/*
                printf("\t\t :: ");
                i = 0;
                while((i < nread) &&( i < MAX_BUF_LEN)){
					 if((buff[i] == '\r')
							 ||(buff[i] == '\n')){
							 buff[i] = ' ';
					 }
                     printf("%c ",  buff[i]);
                     i++;
                }
                printf("\n");
				*/
            }

            //ALOGV("modem_reader_loop()<< loop out\n");
}


static void init_modem(){

	//output_file = fopen(OUTPUT_FILE, "wb");
    reading_audio_file = fopen(INPUT_FILE, "rb");


	if(reading_audio_file == NULL){

	   //printf(" reading audio input file failed, exit\n");
	   ALOGW("Audio input file not found < %s >, return\n", INPUT_FILE);
       return ;

	}else{

	   ALOGV("succesful open audio file in %s", INPUT_FILE);
	}

	/* this is to skip the WAV header */
	fseek(reading_audio_file, 0L + sizeof(struct WAV_HEADER) , SEEK_SET);

    /* open ttyUSB2 */

	printf("Open ttyUSB%d as the voice channel\n", MODEM_VOICE_CHANNEL);
    if(( fd_0 = open_port(MODEM_VOICE_CHANNEL)) < 0){

        perror("open port error");
	   ALOGW("open port error return\n");
       return ;
    }

    if( ( set_port(fd_0, 115200, 8 , 'N', 1))< 0){
        perror("set opt error");
	   ALOGW("set port error return\n");
       return ;
    }

     open_snd_card_out();
     open_snd_card_in();

     init_snd_card_out();
     init_snd_card_in();

}

static void thread_exit_handler(int sig)
{ 
    pthread_t current_id = pthread_self();
	if(current_id == s_thread_id_reader){
        ALOGW("reader thread get signal, and exit\n");
        pthread_exit(0);

	}else if(current_id == s_thread_id_writer){
        ALOGW("writer thread get signal, closing openning thread\n");
	    if(modem_reader_alive == 0)
                 close(fd_0);

        modem_writer_alive = 0;
        close_snd_card_out();
        close_snd_card_in();

        ALOGV("modem_writer()>> stop and exit by kill \n");
        pthread_exit(0);
	}else{

        ALOGW("Exception signal get, do nothing\n");
	}
}

static void kill_threads()
{ 
	int status;
	//pthread_kill(s_thread_id_reader, SIGUSR1);
	if ( (status = 	pthread_kill(s_thread_id_writer, SIGUSR1)) != 0){ 

        ALOGW("Error cancelling modem writer thread %d, error = %d (%s)", 
				s_thread_id_writer, status, strerror(status));
    } 



}


void start_voice_on_modem(void)
{


    init_modem();
    stop_voice = 0;
    modem_reader_alive = 1;
    modem_writer_alive = 1;

    memset(&signal_actions, 0, sizeof(signal_actions)); 
    sigemptyset(&signal_actions.sa_mask);
    signal_actions.sa_flags = 0; 
    signal_actions.sa_handler = thread_exit_handler;
    sigaction(SIGUSR1,&signal_actions,NULL);





    pthread_create(&s_thread_id_reader, NULL, modem_reader, NULL);
    ALOGV("start modem_reader() with thread id:%d", (int)s_thread_id_reader);
    pthread_create(&s_thread_id_writer, NULL, modem_writer, NULL);
    ALOGV("start modem_writer() with thread id:%d", (int)s_thread_id_writer);
}


void stop_voice_on_modem(void)
{
      ALOGV("stop_voice_on_modem()>>");
     stop_voice = 1;
     kill_threads();
}

static void *modem_reader(void *ptr)
	{

    ALOGV("modem_reader()>> thread start waiting on select \n");
	while(1){
       int max_fd =0; 
       fd_set rd;
	   int rc = 0;
	   struct timeval to;

       FD_ZERO(&rd);
       FD_SET(fd_0, &rd);
       if(max_fd < fd_0){
           max_fd = fd_0;
       }

	   to.tv_sec = 0;
	   to.tv_usec = 100*1000; /* 100 ms */

	   if((rc = select (max_fd + 1, &rd, NULL, NULL, &to)) < 0){

            ALOGV("modem_reader()>> Error in select (%s)\n", strerror(errno));
			break;
	   } else if(!rc){
		   if(stop_voice == 1){
			   break;
		   }
	   } else if (FD_ISSET(fd_0, &rd)){
           modem_reader_loop(fd_0);
	   }

	}

    ALOGV("modem_reader()>> stop and exit \n");
	if(modem_writer_alive == 0)
       close(fd_0);

    close_snd_card_out();
    close_snd_card_in();
    modem_reader_alive = 0;
    return NULL;

}


static void *modem_writer(void *ptr)
	{

    ALOGV("modem_writer()>> thread start waiting on select \n");
	while(1){
       int max_fd =0; 
       fd_set rd;
	   int rc = 0;
	   struct timeval to;

       FD_ZERO(&rd);
       FD_SET(fd_0, &rd);
       if(max_fd < fd_0){
           max_fd = fd_0;
       }

	   to.tv_sec = 0;
	   to.tv_usec = 100*1000; /* 100 ms */
       ALOGV("modem_writer()>> select waiting... \n");

	   if((rc = select (max_fd + 1, NULL, &rd , NULL, &to)) < 0){

            ALOGV("modem_writer()>> Error in select (%s)\n", strerror(errno));
			break;
	   } else if(!rc){
		   if(stop_voice == 1){
			   break;
		   }
	   } else if (FD_ISSET(fd_0, &rd)){
           ALOGV("modem_writer()>> select() writing fd ready, goto write sth to Modem... \n");
           modem_writer_loop(fd_0);
	   }

	}

    ALOGV("modem_writer()>> stop and exit \n");
	if(modem_reader_alive == 0)
       close(fd_0);

    modem_writer_alive = 0;
    close_snd_card_out();
    close_snd_card_in();
    return NULL;

}

};
};

