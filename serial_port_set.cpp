#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>

namespace android_audio_legacy {

extern "C" {
int set_port(int fd, int nSpeed, int nBits, char nEvent, int nStop){

    struct termios newtio, oldtio;

    if( tcgetattr(fd, &oldtio) != 0){
        perror("Setup Serial: get attribut error");
        return -1;
    }

    bzero(&newtio, sizeof(newtio));
    newtio.c_cflag |= CLOCAL | CREAD;
    newtio.c_cflag &= ~CSIZE;

    switch(nBits){

        case 7:
            newtio.c_cflag |= CS7;
            break;

        case 8:
            newtio.c_cflag |= CS8;
            break;
    }

    switch(nEvent){
        case 'O':
            newtio.c_cflag |= PARENB;
            newtio.c_cflag |= PARODD;
            newtio.c_iflag |= (INPCK | ISTRIP);
            break;

        case 'E':
            newtio.c_iflag |= (INPCK | ISTRIP);
            newtio.c_cflag |= PARENB;
            newtio.c_cflag &=  ~ PARODD;
            break;

        case 'N':
            newtio.c_cflag &= ~ PARENB;
            break;
    }

    switch(nSpeed){
        case 2400:
            cfsetispeed(&newtio, B2400);
            cfsetospeed(&newtio, B2400);
            break;
        case 4800:
            cfsetispeed(&newtio, B4800);
            cfsetospeed(&newtio, B4800);
            break;

        case 9600:
            cfsetispeed(&newtio, B9600);
            cfsetospeed(&newtio, B9600);
            break;

        case 115200:
            cfsetispeed(&newtio, B115200);
            cfsetospeed(&newtio, B115200);
            break;
        case 460800:
            cfsetispeed(&newtio, B460800);
            cfsetospeed(&newtio, B460800);
            break;
        default:
            cfsetispeed(&newtio, B9600);
            cfsetospeed(&newtio, B9600);
            break;

    }

    if(nStop == 1)
        newtio.c_cflag &= ~ CSTOPB;
    else if(nStop == 2){
        newtio.c_cflag |=  CSTOPB;
    }

    newtio.c_cc[VTIME] = 0;
    newtio.c_cc[VMIN] = 0;

    tcflush(fd, TCIFLUSH);

    if((tcsetattr(fd, TCSANOW, &newtio))  != 0){
        perror("error on set serial port attribute");
        return -1;
    }

    //printf("Set serial port done. ");
    return 0;
}


int open_port(int comport)
{

    char *dev[] = {"/dev/ttyUSB0", "/dev/ttyUSB1", "/dev/ttyUSB2", "/dev/ttyUSB3"};
    long vdisable;
	int fd;

    if((comport<0) || (comport > 3)){
       
            perror("can't open serial port, the vaild number [0,3]");
            return -1;

    }

        fd = open (dev[comport], O_RDWR|O_NOCTTY|O_NDELAY|O_NONBLOCK);
        if( -1 == fd){

            perror("can't open serial port on dev");
            return -1;
        }

        if((fcntl (fd, F_SETFL, 0) < 0))
            perror("fcntl failed");
        else{

            //printf("fcntl OK ! ");
        }

        /*
        if(isatty(STDIN_FILENO) == 0)
           printf("TEST: standard input is not a terminal devices\n");
         else
           printf("TEST: standard input is a terminal devices\n");

           printf("return fd = %d\n", fd);
           */

           return fd;


 }
};

};

