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

//N(s)
static int module_counter=0;

//parametros guardados pelo llOpenTx para usar no llSend/llCloseTx
//o opentx recebe os llparameters como argumento, 
//para o llsend usamos estas variaveis static que sao alteradas no opentx para o que vem do main linha 12/13
static int tx_timeout = 3;
static int tx_nRetransmissions = 4;

//maquinadeestados para todos os supervision frames
typedef enum { S_START, S_FLAG, S_A, S_C, S_BCC, S_DONE } Estado;
//preciso de separar os dois casos: stop and wait para set disc e i frames e os outros que nao usam alarmes
int maquinadeestados(unsigned char A, int with_alarm) {
    Estado estado = S_START;
    unsigned char C = 0;
    unsigned char byte;

    //le-se: o alarme ainda esta ativado OU isto nem precisa de alarme
    while ( (alarmEnabled == TRUE || !with_alarm) && estado != S_DONE) {
        if (readByteSerialPort(&byte) <= 0) continue;

        printf("Byte received: 0x%02X\n", byte);

        if (estado == S_START) {
            if (byte == 0x7E) estado = S_FLAG;
        }
        else if (estado == S_FLAG) {
            // Ver se o byte de address é o que queremos
            if (byte == A) estado = S_A;
            else if (byte != 0x7E) estado = S_START;
        }
        else if (estado == S_A) {
            //byte de controlo retirado para o retornarmos caso tudo corra bem
            C = byte;
            estado = S_C;
        }
        else if (estado == S_C) {
            if (byte == (A ^ C)) estado = S_BCC;
            else if (byte == 0x7E) estado = S_FLAG;
            else estado = S_START;
        }
        else if (estado == S_BCC) {
            if (byte == 0x7E) estado = S_DONE;
            else estado = S_START;
        }
    }

    if (estado == S_DONE) return C;
    return -1;
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

//guardamos para usar no llSend/llCloseTx
tx_timeout = llParameters.timeout;
tx_nRetransmissions = llParameters.nRetransmissions;

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


while (alarmCount<tx_nRetransmissions)
{
  int bytes=0;
  if (alarmEnabled == FALSE)
        {
            alarm(tx_timeout);
            alarmEnabled = TRUE;
            //escrevo uma vez por alarme
            bytes = writeBytesSerialPort(buf, BUF_SIZE);
            printf("%d bytes written to serial port\n", bytes);
        }

    //0x03->reply do receiver, True->estamos a usar alarme, 0x07->control byte do UA
    if (maquinadeestados(0x03, TRUE)==0x07){
        alarm(0);
        return 0;
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

return -1;
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

    // Read one byte from serial port.
    // NOTE: You must check how many bytes were actually read by reading the return value.
    // In this example, we assume that the byte is always read, which may not be true.

    while (1){
    if (maquinadeestados(0x03, FALSE)==0x03) break;
    }



    //received SET frame, Has to send UA
    printf("Received 5 bytes. Will send UA. Stop reading from serial port.\n");

    // Create string to send
    unsigned char buf[BUF_SIZE] = {0};

    buf[0] = 0X7E;
    buf[1] = 0X03;
    buf[2] = 0X07;
    buf[3]= buf[1]^buf[2];
    buf[4]= 0X7E;
    int bytes;
    bytes = writeBytesSerialPort(buf, BUF_SIZE);
    printf("%d bytes written to serial port\n", bytes);


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
    //create frame with size 0, we dinamically increase it
    int frame_size=0;
    unsigned char frame[2*MAX_PAYLOAD_SIZE + 7];
    //calculate bcc2 first
    unsigned char bcc2=0x00;
    for (int i=0; i<bufSize; i++){
        bcc2^=buf[i];
    }
    //add flags to frame
    frame[0]=0x7E;
    frame[1]=0x03;
    if (module_counter==0){frame[2]=0x00;}else frame[2]=0x80;
    frame[3]=frame[1]^frame[2];
    frame_size= 4;

    //stuff data
    for (int i=0; i<bufSize; i++){
        if (buf[i]==0x7E){
            frame[frame_size]=0x7D;
            frame_size++;
            frame[frame_size]=0x5E;
            frame_size++;
        }
        else if (buf[i]==0x7D){
            frame[frame_size]=0x7D;
            frame_size++;
            frame[frame_size]=0x5D;
            frame_size++;
        }
        else{
            frame[frame_size]=buf[i];
            frame_size++;
        }
    }

    //add bcc2
    if(bcc2==0x7D){
        frame[frame_size]=0x7D;
        frame_size++;
        frame[frame_size]=0x5D;
        frame_size++;
    }
    else if (bcc2==0x7E){
        frame[frame_size]=0x7D;
        frame_size++;
        frame[frame_size]=0x5E;
        frame_size++;

    }
    else{
        frame[frame_size]= bcc2;
        frame_size++;
    }
    //flag final
    frame[frame_size]= 0x7E;
    frame_size++;
    //frame esta feito, criamos as variaveis para o alarme
    alarmEnabled = FALSE;
    alarmCount = 0;
    //resposta esperada depende do Ns que mandamos
    unsigned char rr_ok, rej_me;
    if (module_counter==0){ rr_ok=0xAB; rej_me=0x54; }
    else { rr_ok=0xAA; rej_me=0x55; }

    //vamos tentar envia-lo nRetransmissions vezes
    while (alarmCount<tx_nRetransmissions){
        int bytes=0;
        if (alarmEnabled==FALSE){
            alarm(tx_timeout);
            alarmEnabled=TRUE;
            bytes = writeBytesSerialPort(frame, frame_size);
            printf("%d bytes written to serial port\n", bytes);
        }

        int c = maquinadeestados(0x03, TRUE);

        if (c == rr_ok){
            alarm(0);
            //ja recebemos a resposta certa, agora alteramos o module para o proximo i frame
            if (module_counter==1){module_counter--;}
            else {module_counter++;}
            return bufSize;
        }
        //caso especial de falha em que o information frame foi recebido mas com erro,
        //nao gastamos tentativa aqui mas convem perguntar a professora
        else if (c == rej_me){
            alarm(0);
            alarmEnabled = FALSE;
        }
    }

    //esgotamos as tentativas sem receber RR valido
    return -1;
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
