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

int maquinadeestados()
{
    enum State
    {
        START,
        FLAG_RCV,
        A_RCV,
        C_RCV,
        BCC_RCV,
        STOP
    };

    enum State state = START;

    unsigned char byte;

    while (state != STOP && alarmEnabled == TRUE)
    {
        int bytes = readByteSerialPort(&byte);

        if (bytes == 0)
            continue;

        printf("Byte received: 0x%02X\n", byte);

        switch (state)
        {
            case START:

                if (byte == 0x7E)
                {
                    state = FLAG_RCV;
                }

                break;


            case FLAG_RCV:

                if (byte == 0x03)
                {
                    state = A_RCV;
                }
                else if (byte == 0x7E)
                {
                    // Another FLAG, remain here
                    state = FLAG_RCV;
                }
                else
                {
                    state = START;
                }

                break;


            case A_RCV:

                if (byte == 0x03)
                {
                    state = C_RCV;
                }
                else if (byte == 0x7E)
                {
                    state = FLAG_RCV;
                }
                else
                {
                    state = START;
                }

                break;


            case C_RCV:

                if (byte == (0x03 ^ 0x03))
                {
                    state = BCC_RCV;
                }
                else if (byte == 0x7E)
                {
                    state = FLAG_RCV;
                }
                else
                {
                    state = START;
                }

                break;

            case BCC_RCV:

                if (byte == 0x7E)
                { 
                    state = STOP;
                }
                else
                {
                    state = START;
                }

                break;



            case STOP:
                break;
        }
    }

    if (state == STOP)
    {
        printf("SET received correctly.\n");
        return 0;
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
int receiveUA()
{
    enum State
    {
        START,
        FLAG_RCV,
        A_RCV,
        C_RCV,
        BCC_RCV,
        STOP
    };

    enum State state = START;

    unsigned char byte;

    while (state != STOP && alarmEnabled == TRUE)
    {
        int bytes = readByteSerialPort(&byte);

        if (bytes < 0)
        {
            perror("readByteSerialPort");
            return -1;
        }

        if (bytes == 0)
            continue;

        printf("Byte received: 0x%02X\n", byte);

        switch (state)
        {
        case START:
            if (byte == 0x7E)
                state = FLAG_RCV;
            break;

        case FLAG_RCV:
            if (byte == 0x03)
                state = A_RCV;
            else if (byte == 0x7E)
                state = FLAG_RCV;
            else
                state = START;
            break;

        case A_RCV:
            if (byte == 0x07)
                state = C_RCV;
            else if (byte == 0x7E)
                state = FLAG_RCV;
            else
                state = START;
            break;

        case C_RCV:
            if (byte == (0x03 ^ 0x07))
                state = BCC_RCV;
            else if (byte == 0x7E)
                state = FLAG_RCV;
            else
                state = START;
            break;

        case BCC_RCV:
            if (byte == 0x7E)
                state = STOP;
            else
                state = START;
            break;

        case STOP:
            break;
        }
    }

    if (state == STOP)
    {
        printf("UA received correctly.\n");
        return 0;
    }

    return 1;
}


////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llOpenTx(LinkLayer llParameters)
{
    if (openSerialPort(llParameters.serialPort, llParameters.baudRate) < 0)
    {
        perror("openSerialPort");
        return -1;
    }

    printf("Serial port %s opened\n", llParameters.serialPort);

    // SET frame
    unsigned char buf[5];

    buf[0] = 0x7E;
    buf[1] = 0x03;
    buf[2] = 0x03;
    buf[3] = buf[1] ^ buf[2];
    buf[4] = 0x7E;

    // Configure alarm handler
    struct sigaction act = {0};
    act.sa_handler = &alarmHandler;

    if (sigaction(SIGALRM, &act, NULL) == -1)
    {
        perror("sigaction");
        closeSerialPort();
        return -1;
    }

    alarmEnabled = FALSE;
    alarmCount = 0;

    int tries = 0;
    int success = FALSE;

    while (tries < llParameters.nRetransmissions)
    {
        printf("Sending SET (attempt %d/%d)\n",
               tries + 1,
               llParameters.nRetransmissions);

        // Send SET
        int bytes = writeBytesSerialPort(buf, 5);

        if (bytes != 5)
        {
            perror("writeBytesSerialPort");
            closeSerialPort();
            return -1;
        }

        tries++;

        // Start timeout
        alarmEnabled = TRUE;
        alarm(llParameters.timeout);

        // Wait for UA
        if (receiveUA() == 0)
        {
            // UA received -> cancel alarm
            alarm(0);
            alarmEnabled = FALSE;

            success = TRUE;
            break;
        }

        // Timeout -> prepare retransmission
        alarm(0);
        alarmEnabled = FALSE;
    }

    if (success)
    {
        printf("Connection established successfully.\n");
    }
    else
    {
        printf("Failed to establish connection.\n");
    }

    if (closeSerialPort() < 0)
    {
        perror("closeSerialPort");
        return -1;
    }

    printf("Serial port %s closed\n", llParameters.serialPort);

    return success ? 0 : -1;
}

int llOpenRx(LinkLayer llParameters)
{
    if (openSerialPort(llParameters.serialPort, llParameters.baudRate) < 0)
    {
        perror("openSerialPort");
        return -1;
    }

    printf("Serial port %s opened\n", llParameters.serialPort);

    enum State
    {
        START,
        FLAG_RCV,
        A_RCV,
        C_RCV,
        BCC_RCV,
        STOP
    };

    enum State state = START;

    unsigned char byte;

    while (state != STOP)
    {
        int bytes = readByteSerialPort(&byte);

        if (bytes < 0)
        {
            perror("readByteSerialPort");
            closeSerialPort();
            return -1;
        }

        if (bytes == 0)
            continue;

        printf("Byte received: 0x%02X\n", byte);

        switch (state)
        {
        case START:

            if (byte == 0x7E)
            {
                state = FLAG_RCV;
            }

            break;

        case FLAG_RCV:

            if (byte == 0x03)
            {
                state = A_RCV;
            }
            else if (byte == 0x7E)
            {
                state = FLAG_RCV;
            }
            else
            {
                state = START;
            }

            break;

        case A_RCV:

            if (byte == 0x03)
            {
                state = C_RCV;
            }
            else if (byte == 0x7E)
            {
                state = FLAG_RCV;
            }
            else
            {
                state = START;
            }

            break;

        case C_RCV:

            if (byte == (0x03 ^ 0x03))
            {
                state = BCC_RCV;
            }
            else if (byte == 0x7E)
            {
                state = FLAG_RCV;
            }
            else
            {
                state = START;
            }

            break;

        case BCC_RCV:

            if (byte == 0x7E)
            {
                state = STOP;
            }
            else
            {
                state = START;
            }

            break;

        case STOP:
            break;
        }
    }

    printf("SET received correctly.\n");

    // UA frame
    unsigned char ua[5];

    ua[0] = 0x7E;
    ua[1] = 0x03;
    ua[2] = 0x07;
    ua[3] = ua[1] ^ ua[2];
    ua[4] = 0x7E;

    // Send UA
    int bytes = writeBytesSerialPort(ua, 5);

    if (bytes != 5)
    {
        perror("writeBytesSerialPort");
        closeSerialPort();
        return -1;
    }

    printf("UA sent correctly.\n");

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
