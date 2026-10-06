// RCOM 2026/2027
//
// Link layer protocol implementation

#include "link_layer.h"
#include "serial_port.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source
#define BUF_SIZE 5
int alarmEnabled = FALSE;
int alarmCount= 0;

//maquinadeestados

int maquinadeestados(){

	volatile int STOP = FALSE;
	int nBytesBuf = 0;
    //paro de tentar ler apenas quando o alarm acabar e for reposto.
    while (STOP==FALSE && alarmEnabled==TRUE){
        unsigned char byte;
        int bytes = readByteSerialPort(&byte);
       if (bytes==0){continue;}
        nBytesBuf += bytes;

        printf("Byte received: 0x%02X\n", byte) ; 

        if(nBytesBuf == 1){ if(byte == 0X7E){continue;}else{STOP=TRUE;}}
        if(nBytesBuf == 2){ if(byte == 0X01){continue;}else{STOP=TRUE;}}
        
        if(nBytesBuf == 3){ if(byte == 0X07){continue;}else{STOP=TRUE;}}
        if(nBytesBuf == 4){ if(byte == (0x01 ^0X07)){continue;}else{STOP=TRUE;}}
        if(nBytesBuf==5)
        {
        if(byte == 0X7E){
        printf("Received 5 bytes. Stop reading from serial port.\n");
        return 0;

        } 
        STOP=TRUE;
        }

	}
	return 1;
}

//alarmhandler
void alarmHandler(int signal)
{
    alarmEnabled = FALSE;
    alarmCount++;

    printf("Alarm #%d received\n", alarmCount);
}


////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llOpenTx(LinkLayer llParameters)
{
// ----------------------------------------------------
// This example code shows how to open the serial port and send a string.
// TODO: Adapt and extend this code according to the specifications of the project.
// ----------------------------------------------------

if (openSerialPort(llParameters.serialPort, llParameters.baudRate) < 0)
{
perror("openSerialPort");
return -1;
}

printf("Serial port %s opened\n", llParameters.serialPort);

// Create string to send
unsigned char buf[BUF_SIZE] = {0};


buf[0] = 0X7E;
buf[1] = 0X03;
buf[2] = 0X03;
buf[3]= buf[1]^buf[2];
buf[4]= 0X7E;

// In non-canonical mode, '\n' does not end the writing.
// Test this condition by placing a '\n' in the middle of the buffer.
// The whole buffer must be sent even with the '\n'.


struct sigaction act = {0};
act.sa_handler = &alarmHandler;
if (sigaction(SIGALRM, &act, NULL) == -1){
    perror("sigaction");
    exit(1);
}

printf("Alarm configured\n");


while (alarmCount<4 )
{ 
  int bytes=0;
  if (alarmEnabled == FALSE)
        {
            alarm(3); // Set alarm to be triggered in 3s
            alarmEnabled = TRUE;
            //escrevo uma vez por alarme
            bytes = writeBytesSerialPort(buf, BUF_SIZE);
            printf("%d bytes written to serial port\n", bytes);
        }  
    
    if (maquinadeestados()==0){
        alarm(0);
        break;
    }
    

}


// Wait until all bytes have been written to the serial port
sleep(1);

// Close serial port
if (closeSerialPort() < 0)
{
perror("closeSerialPort");
return -1;
}

printf("Serial port %s closed\n", llParameters.serialPort);

return 0;
}


int llOpenRx(LinkLayer llParameters)
{
    // ----------------------------------------------------
    // This example code shows how to open the serial port and receive a string.
    // TODO: Adapt and extend this code according to the specifications of the project.
    // ----------------------------------------------------

    if (openSerialPort(llParameters.serialPort, llParameters.baudRate) < 0)
    {
        perror("openSerialPort");
        return -1;
    }

    printf("Serial port %s opened\n", llParameters.serialPort);

    // Read from serial port until the 'z' char is received.

    // NOTE: This while() cycle is a simple example showing how to read from the serial port.
    // It must be changed in order to respect the specifications of the protocol indicated in the Lab guide.

    // TODO: Save the received bytes in a buffer array and print it at the end of the program.
    volatile int STOP = FALSE;
    int nBytesBuf = 0;

    while (STOP == FALSE)
{
// Read one byte from serial port.
// NOTE: You must check how many bytes were actually read by reading the return value.
// In this example, we assume that the byte is always read, which may not be true.
unsigned char byte;
int bytes = readByteSerialPort(&byte);
if (bytes==0)continue;
nBytesBuf += bytes;

printf("Byte received: 0x%02X\n", byte) ; 

if(nBytesBuf == 1){ if(byte == 0X7E){continue;}else{STOP=TRUE;}}
if(nBytesBuf == 2){ if(byte == 0X03){continue;}else{STOP=TRUE;}}
if(nBytesBuf == 3){ if(byte == 0X03){continue;}else{STOP=TRUE;}}
if(nBytesBuf == 4){ if(byte == 0X00){continue;}else{STOP=TRUE;}}
if(nBytesBuf==5)
{
if(byte == 0X7E){
    //received SET frame, Has to send UA
    printf("Received 5 bytes. Will send UA. Stop reading from serial port.\n");

    // Create string to send
    unsigned char buf[BUF_SIZE] = {0};

    buf[0] = 0X7E;
    buf[1] = 0X01;
    buf[2] = 0X07;
    buf[3]= buf[1]^buf[2];
    buf[4]= 0X7E;

    bytes = writeBytesSerialPort(buf, BUF_SIZE);
    printf("%d bytes written to serial port\n", bytes);
    STOP = TRUE;
}
}

}


    printf("Total bytes received: %d\n", nBytesBuf);

    // Close serial port
    if (closeSerialPort() < 0)
    {
        perror("closeSerialPort");
        return -1;
    }

    printf("Serial port %s closed\n", llParameters.serialPort);

    return 0;
}

////////////////////////////////////////////////
// LLSEND
////////////////////////////////////////////////
int llSend(const unsigned char *buf, int bufSize)
{
    // TODO: Implement this function

    return 0;
}

////////////////////////////////////////////////
// LLRECEIVE
////////////////////////////////////////////////
int llReceive(unsigned char *packet)
{
    // TODO: Implement this function

    return 0;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llCloseTx()
{
    // TODO: Implement this function

    return 0;
}

int llCloseRx()
{
    // TODO: Implement this function

    return 0;
}
