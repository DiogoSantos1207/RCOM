// RCOM 2026/2027
//
// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

//C para control packets
#define C_START 1
#define C_DATA  2
#define C_END   3

//T para TLV
#define T_SIZE 0
#define T_NAME 1

//tamanho maximo do fragmento do ficheiro dentro de um data packet
//MAX_PAYLOAD_SIZE menos os 3 bytes do header do data packet (C, L2, L1)
//(pode ser alterado ao compilar, ex.: -DMAX_CHUNK=200, para o estudo da eficiencia)
#ifndef MAX_CHUNK
#define MAX_CHUNK (MAX_PAYLOAD_SIZE - 3)
#endif
#if MAX_CHUNK < 1 || MAX_CHUNK > (MAX_PAYLOAD_SIZE - 3)
#error "MAX_CHUNK tem de estar entre 1 e MAX_PAYLOAD_SIZE - 3"
#endif

////////////////////////////////////////////////
// TX
////////////////////////////////////////////////
static int sendControlPacket(unsigned char c, long fileSize, const char *filename)
{
    //montamos o packet dinamicamente
    unsigned char packet[MAX_PAYLOAD_SIZE];
    int packet_size = 0;

    //primeiro byte e o control
    packet[0] = c;
    packet_size = 1;

    //TLV do tamanho do ficheiro
    //calculamos quantos bytes sao necessarios para representar fileSize
    unsigned char sizeBytes[8];
    int nSizeBytes = 0;
    long tmp = fileSize;
    //caso especial: ficheiro vazio ainda precisa de 1 byte
    if (tmp == 0) {
        sizeBytes[0] = 0x00;
        nSizeBytes = 1;
    }
    else {
        //guardamos em little endian para depois invertermos
        while (tmp > 0) {
            sizeBytes[nSizeBytes] = (unsigned char)(tmp & 0xFF);
            nSizeBytes++;
            tmp >>= 8;
        }
    }

    packet[packet_size] = T_SIZE;
    packet_size++;
    packet[packet_size] = (unsigned char)nSizeBytes;
    packet_size++;
    //invertemos para ficar em big endian (byte mais significativo primeiro)
    for (int i = nSizeBytes - 1; i >= 0; i--) {
        packet[packet_size] = sizeBytes[i];
        packet_size++;
    }

    //TLV do nome do ficheiro
    //enviamos so o nome base (sem diretorias) e no maximo 255 bytes
    const char *base = strrchr(filename, '/');
    base = base ? base + 1 : filename;
    int nameLen = (int)strlen(base);
    if (nameLen > 255) nameLen = 255;
    packet[packet_size] = T_NAME;
    packet_size++;
    packet[packet_size] = (unsigned char)nameLen;
    packet_size++;
    for (int i = 0; i < nameLen; i++) {
        packet[packet_size] = (unsigned char)base[i];
        packet_size++;
    }

    //enviamos atraves do link layer
    if (llSend(packet, packet_size) < 0) return -1;
    return 0;
}

static int sendDataPacket(const unsigned char *data, int dataSize)
{
    //montamos o data packet: C | L2 | L1 | data
    unsigned char packet[MAX_PAYLOAD_SIZE];
    packet[0] = C_DATA;
    //K = 256 * L2 + L1
    packet[1] = (unsigned char)((dataSize >> 8) & 0xFF);
    packet[2] = (unsigned char)(dataSize & 0xFF);
    for (int i = 0; i < dataSize; i++) {
        packet[3 + i] = data[i];
    }

    if (llSend(packet, dataSize + 3) < 0) return -1;
    return 0;
}

static int runTx(const char *filename, int baudRate)
{
    //abrimos o ficheiro em modo binario
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        perror("fopen");
        return -1;
    }

    //descobrimos o tamanho do ficheiro
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    printf("File %s has %ld bytes\n", filename, fileSize);

    //medimos o tempo de transferencia (START ate END)
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    //enviamos o start packet
    if (sendControlPacket(C_START, fileSize, filename) < 0) {
        printf("Error sending START\n");
        fclose(file);
        return -1;
    }
    printf("START sent\n");

    //loop para ler fragmentos e enviar data packets
    unsigned char chunk[MAX_CHUNK];
    long bytesSent = 0;
    while (bytesSent < fileSize) {
        int toRead = MAX_CHUNK;
        if (fileSize - bytesSent < MAX_CHUNK) toRead = (int)(fileSize - bytesSent);

        int read = (int)fread(chunk, 1, toRead, file);
        if (read <= 0) {
            printf("Error reading file\n");
            fclose(file);
            return -1;
        }

        if (sendDataPacket(chunk, read) < 0) {
            printf("Error sending DATA\n");
            fclose(file);
            return -1;
        }
        bytesSent += read;
        printf("DATA sent (%d bytes, total %ld/%ld)\n", read, bytesSent, fileSize);
    }

    //enviamos o end packet (mesma info do start)
    if (sendControlPacket(C_END, fileSize, filename) < 0) {
        printf("Error sending END\n");
        fclose(file);
        return -1;
    }
    printf("END sent\n");
    clock_gettime(CLOCK_MONOTONIC, &t1);

    //R = debito obtido (bit/s), S = R / C (eficiencia)
    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    if (elapsed > 0) {
        double R = (fileSize * 8.0) / elapsed;
        printf("Transfer time: %.3f s | R = %.1f bit/s | S = R/C = %.4f (C = %d, fragment = %d bytes)\n",
               elapsed, R, R / baudRate, baudRate, MAX_CHUNK);
    }

    fclose(file);
    return 0;
}

////////////////////////////////////////////////
// RX
////////////////////////////////////////////////
static int parseControlPacket(const unsigned char *packet, int size, long *fileSize, char *filename)
{
    //primeiro byte e o control, saltamos
    int i = 1;
    while (i < size) {
        unsigned char t = packet[i];
        i++;
        unsigned char l = packet[i];
        i++;

        if (t == T_SIZE) {
            //montamos o inteiro byte a byte (big endian)
            long value = 0;
            for (int j = 0; j < l; j++) {
                value = (value << 8) | packet[i + j];
            }
            *fileSize = value;
        }
        else if (t == T_NAME) {
            for (int j = 0; j < l; j++) {
                filename[j] = (char)packet[i + j];
            }
            filename[l] = '\0';
        }
        //se aparecer um T desconhecido ignoramos mas avancamos na mesma
        i += l;
    }
    return 0;
}

static int runRx(const char *filename)
{
    //o llReceive copia apenas o payload para este buffer (sem BCC2),
    //portanto MAX_PAYLOAD_SIZE chega
    unsigned char packet[MAX_PAYLOAD_SIZE];

    //esperamos o START
    int size = llReceive(packet);
    if (size < 0 || packet[0] != C_START) {
        printf("Error: did not receive START\n");
        return -1;
    }

    long fileSize = 0;
    char receivedName[256] = {0};
    parseControlPacket(packet, size, &fileSize, receivedName);
    printf("START received: file %s with %ld bytes\n", receivedName, fileSize);

    //abrimos o ficheiro de destino com o nome passado ao receiver
    FILE *file = fopen(filename, "wb");
    if (file == NULL) {
        perror("fopen");
        return -1;
    }

    //loop ate ao END
    long bytesReceived = 0;
    while (1) {
        size = llReceive(packet);
        if (size < 0) {
            printf("Error in llReceive\n");
            fclose(file);
            return -1;
        }

        if (packet[0] == C_DATA) {
            //K = 256 * L2 + L1
            int k = packet[1] * 256 + packet[2];
            //validacao de integridade: o K declarado tem de bater certo com o
            //tamanho real do packet. O enunciado (slide 27) diz que este campo
            //existe precisamente "to allow for additional checks regarding data
            //integrity". Sem este check, um k maior leria lixo do buffer.
            if (k != size - 3) {
                printf("Error: DATA packet size mismatch (declared k=%d, got %d)\n", k, size - 3);
                fclose(file);
                return -1;
            }
            fwrite(packet + 3, 1, k, file);
            bytesReceived += k;
            printf("DATA received (%d bytes, total %ld/%ld)\n", k, bytesReceived, fileSize);
        }
        else if (packet[0] == C_END) {
            printf("END received\n");
            //validamos integridade: o END repete os TLV do START e nessa altura
            //ja devemos ter recebido exactamente fileSize bytes. Qualquer
            //discrepancia significa que a transferencia esta errada.
            //"File integrity" e um criterio de avaliacao (slide 32).
            long endSize = 0;
            char endName[256] = {0};
            parseControlPacket(packet, size, &endSize, endName);
            if (endSize != fileSize) {
                printf("Error: END size (%ld) != START size (%ld)\n", endSize, fileSize);
                fclose(file);
                return -1;
            }
            if (bytesReceived != fileSize) {
                printf("Error: received %ld bytes, expected %ld\n", bytesReceived, fileSize);
                fclose(file);
                return -1;
            }
            break;
        }
        //packets desconhecidos sao ignorados
    }

    fclose(file);
    return 0;
}

////////////////////////////////////////////////
// MAIN
////////////////////////////////////////////////
void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    LinkLayer llParameters = {
        .baudRate = baudRate,
        .nRetransmissions = nTries,
        .timeout = timeout,
    };
    strcpy(llParameters.serialPort, serialPort);

    if (strcmp(role, "tx") == 0)
    {
        if (llOpenTx(llParameters) < 0) {
            printf("llOpenTx failed\n");
            return;
        }
        printf("Connection established (Tx)\n");

        if (runTx(filename, baudRate) < 0) {
            printf("Transfer failed\n");
        }

        if (llCloseTx() < 0) {
            printf("llCloseTx failed\n");
            return;
        }
        printf("Connection closed (Tx)\n");
    }
    else if (strcmp(role, "rx") == 0)
    {
        if (llOpenRx(llParameters) < 0) {
            printf("llOpenRx failed\n");
            return;
        }
        printf("Connection established (Rx)\n");

        if (runRx(filename) < 0) {
            printf("Reception failed\n");
        }

        if (llCloseRx() < 0) {
            printf("llCloseRx failed\n");
            return;
        }
        printf("Connection closed (Rx)\n");
    }
    else
    {
        printf("Invalid role: %s. Must be 'tx' or 'rx'.\n", role);
        return;
    }
}
