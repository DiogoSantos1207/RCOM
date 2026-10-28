// RCOM 2026/2027
//
// Link layer protocol implementation

#include "link_layer.h"
#include "serial_port.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source
#define BUF_SIZE 5
int alarmEnabled = FALSE;
int alarmCount= 0;

//estatisticas do protocolo (impressas no llCloseTx/llCloseRx)
//indices: 0=I, 1=SET, 2=DISC, 3=UA, 4=RR, 5=REJ
static int stat_sent[6] = {0};
static int stat_recv[6] = {0};
static int stat_retransmissions = 0;
static int stat_timeouts = 0;
static int stat_rejs_received = 0;
static int stat_rejs_sent = 0;
static int stat_duplicates = 0;
static long stat_bytes = 0;
static struct timespec stat_start;

static const char *stat_names[6] = {"I", "SET", "DISC", "UA", "RR", "REJ"};

static void stats_reset(void) {
    for (int i = 0; i < 6; i++) { stat_sent[i] = 0; stat_recv[i] = 0; }
    stat_retransmissions = 0;
    stat_timeouts = 0;
    stat_rejs_received = 0;
    stat_rejs_sent = 0;
    stat_duplicates = 0;
    stat_bytes = 0;
    clock_gettime(CLOCK_MONOTONIC, &stat_start);
}

static void stats_print(const char *role) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double elapsed = (now.tv_sec - stat_start.tv_sec)
                   + (now.tv_nsec - stat_start.tv_nsec) / 1e9;

    printf("\n========== Protocol statistics (%s) ==========\n", role);
    printf("Elapsed time:        %.3f s\n", elapsed);
    printf("Bytes transferred:   %ld\n", stat_bytes);
    if (elapsed > 0)
        printf("Throughput:          %.2f byte/s\n", stat_bytes / elapsed);
    printf("Frames sent:         ");
    for (int i = 0; i < 6; i++) printf("%s=%d ", stat_names[i], stat_sent[i]);
    printf("\n");
    printf("Frames received:     ");
    for (int i = 0; i < 6; i++) printf("%s=%d ", stat_names[i], stat_recv[i]);
    printf("\n");
    printf("Timeouts:            %d\n", stat_timeouts);
    printf("Retransmissions:     %d\n", stat_retransmissions);
    printf("REJ received (Tx):   %d\n", stat_rejs_received);
    printf("REJ sent (Rx):       %d\n", stat_rejs_sent);
    printf("Duplicates detected: %d\n", stat_duplicates);
    printf("================================================\n\n");
}

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
    stat_timeouts++;

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

stats_reset();

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
            stat_sent[1]++; //SET
            if (alarmCount > 0) stat_retransmissions++;
            printf("%d bytes written to serial port\n", bytes);
        }

    //0x03->reply do receiver, True->estamos a usar alarme, 0x07->control byte do UA
    if (maquinadeestados(0x03, TRUE)==0x07){
        alarm(0);
        stat_recv[3]++; //UA
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

    tx_timeout = llParameters.timeout;
    tx_nRetransmissions = llParameters.nRetransmissions;

    stats_reset();

    //instalamos o handler do SIGALRM aqui tambem porque o llCloseRx usa alarme
    //quando envia o seu DISC e espera o UA final. Sem este handler, o default
    //POSIX do SIGALRM e terminar o processo ("Alarm clock") — matava o Rx.
    struct sigaction act = {0};
    act.sa_handler = &alarmHandler;
    if (sigaction(SIGALRM, &act, NULL) == -1) {
        perror("sigaction");
        exit(1);
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
    if (maquinadeestados(0x03, FALSE)==0x03) { stat_recv[1]++; break; } //SET
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
    stat_sent[3]++; //UA
    printf("%d bytes written to serial port\n", bytes);

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
    int attempts = 0;
    while (alarmCount<tx_nRetransmissions){
        int bytes=0;
        if (alarmEnabled==FALSE){
            alarm(tx_timeout);
            alarmEnabled=TRUE;
            bytes = writeBytesSerialPort(frame, frame_size);
            stat_sent[0]++; //I
            if (attempts > 0) stat_retransmissions++;
            attempts++;
            printf("%d bytes written to serial port\n", bytes);
        }

        int c = maquinadeestados(0x03, TRUE);

        if (c == rr_ok){
            alarm(0);
            stat_recv[4]++; //RR
            stat_bytes += bufSize;
            //ja recebemos a resposta certa, agora alteramos o module para o proximo i frame
            if (module_counter==1){module_counter--;}
            else {module_counter++;}
            return bufSize;
        }
        //caso especial de falha em que o information frame foi recebido mas com erro,
        //nao gastamos tentativa aqui mas convem perguntar a professora
        else if (c == rej_me){
            alarm(0);
            stat_recv[5]++; //REJ
            stat_rejs_received++;
            alarmEnabled = FALSE;
        }
    }

    //esgotamos as tentativas sem receber RR valido
    return -1;
}


////////////////////////////////////////////////
// LLRECEIVE
////////////////////////////////////////////////
static int rx_expected = 0;

int llReceive(unsigned char *packet)
{
    //R_SET_END trata o caso em que o Tx reenvia o SET porque o UA inicial se
    //perdeu. Nessa altura o Rx ja esta em llReceive e tem de reconhecer o SET
    //duplicado para voltar a enviar o UA, caso contrario o Tx desiste.
    typedef enum { R_START, R_FLAG, R_A, R_C, R_BCC1, R_DATA, R_SET_END, R_DONE } EstadoR;
    EstadoR estado = R_START;

    unsigned char byte;
    unsigned char C = 0;
    int escaped = 0;
    int size = 0;

    //scratch interno. A camada de aplicacao nao conhece nem o BCC2 nem o
    //stuffing, por isso destuffamos e guardamos aqui ate o frame estar validado.
    //So no fim e que copiamos o payload (sem BCC2) para o buffer do utilizador.
    //Isto cumpre o criterio "Layer independency" do enunciado (slide 29/32).
    //O +1 e para caber o BCC2 que fica como ultimo byte deste buffer.
    unsigned char scratch[MAX_PAYLOAD_SIZE + 1];

    //resposta esperada depende do N(s) que esperamos receber
    unsigned char rr_ok, rr_dup, rej;
    if (rx_expected == 0) { rr_ok = 0xAB; rr_dup = 0xAA; rej = 0x54; }
    else                  { rr_ok = 0xAA; rr_dup = 0xAB; rej = 0x55; }

    while (estado != R_DONE) {
        if (readByteSerialPort(&byte) <= 0) continue;

        if (estado == R_START) {
            if (byte == 0x7E) estado = R_FLAG;
        }
        else if (estado == R_FLAG) {
            if (byte == 0x03) estado = R_A;
            else if (byte != 0x7E) estado = R_START;
        }
        else if (estado == R_A) {
            //guardamos o C para saber se e I0, I1 ou SET duplicado
            C = byte;
            if (C == 0x00 || C == 0x80 || C == 0x03) estado = R_C;
            else if (byte == 0x7E) estado = R_FLAG;
            else estado = R_START;
        }
        else if (estado == R_C) {
            if (byte == (0x03 ^ C)) {
                //header ok. Se for SET (C=0x03) nao ha dados, so a flag final.
                if (C == 0x03) estado = R_SET_END;
                else {
                    estado = R_DATA;
                    size = 0;
                    escaped = 0;
                }
            }
            else if (byte == 0x7E) estado = R_FLAG;
            else estado = R_START;
        }
        else if (estado == R_SET_END) {
            //SET duplicado completo: significa que o UA do llOpenRx se perdeu.
            //Reenviamos UA (A=0x03, C=0x07) para o Tx conseguir sair do llOpenTx.
            if (byte == 0x7E) {
                unsigned char ua[5] = {0x7E, 0x03, 0x07, 0x03 ^ 0x07, 0x7E};
                writeBytesSerialPort(ua, 5);
                stat_sent[3]++; //UA
            }
            estado = R_START;
        }
        else if (estado == R_DATA) {
            if (byte == 0x7E) {
                //fim de frame, o ultimo byte guardado e o BCC2
                if (size == 0) { estado = R_START; continue; }

                unsigned char bcc2_recv = scratch[size - 1];
                int dataLen = size - 1;

                unsigned char bcc2_calc = 0x00;
                for (int i = 0; i < dataLen; i++) bcc2_calc ^= scratch[i];

                unsigned char Ns;
                if (C == 0x00) Ns = 0;
                else Ns = 1;

                //montamos a resposta inline
                unsigned char reply[5];
                reply[0] = 0x7E;
                reply[1] = 0x03;
                reply[4] = 0x7E;

                stat_recv[0]++; //I
                if (bcc2_calc != bcc2_recv) {
                    //erro no campo de dados
                    if (Ns == rx_expected) {
                        reply[2] = rej;
                        stat_sent[5]++; //REJ
                        stat_rejs_sent++;
                    }
                    else {
                        reply[2] = rr_dup;
                        stat_sent[4]++; //RR
                        stat_duplicates++;
                    }
                    reply[3] = reply[1] ^ reply[2];
                    writeBytesSerialPort(reply, 5);
                    estado = R_START;
                }
                else {
                    //dados ok
                    if (Ns == rx_expected) {
                        reply[2] = rr_ok;
                        reply[3] = reply[1] ^ reply[2];
                        writeBytesSerialPort(reply, 5);
                        stat_sent[4]++; //RR
                        stat_bytes += dataLen;
                        //alteramos o rx_expected para a proxima frame
                        if (rx_expected == 1) rx_expected--;
                        else rx_expected++;
                        //so agora copiamos o payload (ja sem BCC2) para o buffer
                        //que vem da aplicacao. Assim o utilizador so precisa de
                        //alocar MAX_PAYLOAD_SIZE e nao sabe nada sobre o BCC2.
                        for (int i = 0; i < dataLen; i++) packet[i] = scratch[i];
                        return dataLen;
                    }
                    else {
                        //duplicado sem erro, confirma mas descarta
                        reply[2] = rr_dup;
                        reply[3] = reply[1] ^ reply[2];
                        writeBytesSerialPort(reply, 5);
                        stat_sent[4]++; //RR
                        stat_duplicates++;
                        estado = R_START;
                    }
                }
            }
            else if (byte == 0x7D) {
                escaped = 1;
            }
            else {
                //bound check: nunca podemos escrever para la do scratch.
                //Se um frame com ruido mantiver R_DATA aberto para sempre,
                //descartamos em vez de corromper memoria.
                if (size >= MAX_PAYLOAD_SIZE + 1) {
                    estado = R_START;
                    escaped = 0;
                    continue;
                }
                if (escaped) {
                    if (byte == 0x5E) scratch[size] = 0x7E;
                    else if (byte == 0x5D) scratch[size] = 0x7D;
                    else scratch[size] = byte;
                    escaped = 0;
                }
                else scratch[size] = byte;
                size++;
            }
        }
    }

    return -1;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llCloseTx()
{
    //monta DISC (A=0x03, C=0x0B)
    unsigned char buf[BUF_SIZE] = {0};
    buf[0] = 0x7E;
    buf[1] = 0x03;
    buf[2] = 0x0B;
    buf[3] = buf[1] ^ buf[2];
    buf[4] = 0x7E;

    alarmEnabled = FALSE;
    alarmCount = 0;
    int attempts = 0;

    while (alarmCount < tx_nRetransmissions)
    {
        int bytes = 0;
        if (alarmEnabled == FALSE)
        {
            alarm(tx_timeout);
            alarmEnabled = TRUE;
            bytes = writeBytesSerialPort(buf, BUF_SIZE);
            stat_sent[2]++; //DISC
            if (attempts > 0) stat_retransmissions++;
            attempts++;
            printf("%d bytes written to serial port\n", bytes);
        }

        //esperamos DISC do receiver (A=0x01, C=0x0B)
        if (maquinadeestados(0x01, TRUE) == 0x0B)
        {
            alarm(0);
            stat_recv[2]++; //DISC

            //respondemos com UA (A=0x01, C=0x07)
            unsigned char ua[BUF_SIZE] = {0};
            ua[0] = 0x7E;
            ua[1] = 0x01;
            ua[2] = 0x07;
            ua[3] = ua[1] ^ ua[2];
            ua[4] = 0x7E;
            writeBytesSerialPort(ua, BUF_SIZE);
            stat_sent[3]++; //UA

            stats_print("Tx");

            if (closeSerialPort() < 0)
            {
                perror("closeSerialPort");
                return -1;
            }
            return 0;
        }
    }

    stats_print("Tx");
    return -1;
}

int llCloseRx()
{
    //esperamos DISC do transmitter (A=0x03, C=0x0B)
    while (1) {
        if (maquinadeestados(0x03, FALSE) == 0x0B) { stat_recv[2]++; break; } //DISC
    }

    //monta DISC proprio (A=0x01, C=0x0B)
    unsigned char buf[BUF_SIZE] = {0};
    buf[0] = 0x7E;
    buf[1] = 0x01;
    buf[2] = 0x0B;
    buf[3] = buf[1] ^ buf[2];
    buf[4] = 0x7E;

    alarmEnabled = FALSE;
    alarmCount = 0;
    int attempts = 0;

    while (alarmCount < tx_nRetransmissions)
    {
        int bytes = 0;
        if (alarmEnabled == FALSE)
        {
            alarm(tx_timeout);
            alarmEnabled = TRUE;
            bytes = writeBytesSerialPort(buf, BUF_SIZE);
            stat_sent[2]++; //DISC
            if (attempts > 0) stat_retransmissions++;
            attempts++;
            printf("%d bytes written to serial port\n", bytes);
        }

        //esperamos UA (A=0x01, C=0x07)
        if (maquinadeestados(0x01, TRUE) == 0x07)
        {
            alarm(0);
            stat_recv[3]++; //UA

            stats_print("Rx");

            if (closeSerialPort() < 0)
            {
                perror("closeSerialPort");
                return -1;
            }
            return 0;
        }
    }

    stats_print("Rx");
    return -1;
}
