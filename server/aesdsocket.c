#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <syslog.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>
#include <sys/queue.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include "aesd_ioctl.h"

#define SEEKTO_PREFIX "AESDCHAR_IOCSEEKTO:"

#define USE_AESD_CHAR_DEVICE 1

#if USE_AESD_CHAR_DEVICE
#define AESD_FILEPATH "/dev/aesdchar"
#else
#define AESD_FILEPATH "/var/tmp/aesdsocketdata"
#endif

pthread_mutex_t FileLock = PTHREAD_MUTEX_INITIALIZER;
volatile sig_atomic_t exitFlag = 0;

typedef struct
{
    int clientSocket;
    struct sockaddr_in clientAddr;
} client_t;

void signal_handler(int signal)
{
    (void)signal;
    exitFlag = 1;
}

void *client_thread(void *arg)
{
    client_t *client = (client_t *)arg;
    char recvbuf[2048];
    char *packet = NULL;
    size_t packet_size = 0;
    ssize_t rcv;
    int fd;

    fd = open(AESD_FILEPATH, O_RDWR);
    if (fd < 0) {
        syslog(LOG_ERR, "Failed to open device");
        goto cleanup;
    }

    /* Receive until newline */
    while ((rcv = recv(client->clientSocket, recvbuf,
                       sizeof(recvbuf), 0)) > 0)
    {
        char *tmp = realloc(packet, packet_size + rcv);
        if (!tmp) {
            syslog(LOG_ERR, "realloc failed");
            goto cleanup;
        }
        packet = tmp;
        memcpy(packet + packet_size, recvbuf, rcv);
        packet_size += rcv;

        if (memchr(recvbuf, '\n', rcv))
            break;
    }

    if (!packet)
        goto cleanup;

    pthread_mutex_lock(&FileLock);

    /* IOCSEEKTO handling */
    if (!strncmp(packet, SEEKTO_PREFIX, strlen(SEEKTO_PREFIX))) {

        uint32_t cmd, offset;
        if (sscanf(packet + strlen(SEEKTO_PREFIX),
                   "%u,%u", &cmd, &offset) == 2) {

            struct aesd_seekto seekto = {
                .write_cmd = cmd,
                .write_cmd_offset = offset
            };

            if (ioctl(fd, AESDCHAR_IOCSEEKTO, &seekto) == 0) {
                ssize_t rd;
                while ((rd = read(fd, recvbuf, sizeof(recvbuf))) > 0) {
                    send(client->clientSocket, recvbuf, rd, 0);
                }
            }
        }
    }
    else {
        /* Normal write */
        write(fd, packet, packet_size);

        /* Return entire contents */
        lseek(fd, 0, SEEK_SET);
        ssize_t rd;
        while ((rd = read(fd, recvbuf, sizeof(recvbuf))) > 0) {
            send(client->clientSocket, recvbuf, rd, 0);
        }
    }

    pthread_mutex_unlock(&FileLock);

cleanup:
    if (fd >= 0)
        close(fd);
    close(client->clientSocket);
    free(packet);
    free(client);
    return NULL;
}

int main(int argc, char *argv[])
{
    int server_fd;
    struct addrinfo hints = {0}, *res;
    struct sigaction sa = {0};

    openlog("aesdsocket", 0, LOG_USER);

    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, "9000", &hints, &res) != 0)
        exit(EXIT_FAILURE);

    server_fd = socket(res->ai_family, res->ai_socktype, 0);
    if (server_fd < 0)
        exit(EXIT_FAILURE);

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(server_fd, res->ai_addr, res->ai_addrlen) != 0)
        exit(EXIT_FAILURE);

    freeaddrinfo(res);

    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        if (fork() > 0)
            exit(EXIT_SUCCESS);
        setsid();
    }

    listen(server_fd, 10);

    while (!exitFlag) {
        client_t *client = malloc(sizeof(client_t));
        socklen_t len = sizeof(client->clientAddr);

        client->clientSocket =
            accept(server_fd,
                   (struct sockaddr *)&client->clientAddr, &len);

        if (client->clientSocket < 0) {
            free(client);
            continue;
        }

        pthread_t tid;
        pthread_create(&tid, NULL, client_thread, client);
        pthread_detach(tid);
    }

    close(server_fd);
    closelog();
    return 0;
}
