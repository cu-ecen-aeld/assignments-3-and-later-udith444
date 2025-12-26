#include <fcntl.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <assert.h>
#include <stdint.h>

#include "safefile.h"
#include "list.h"

#ifdef USE_AESD_CHAR_DEVICE
#include "aesd_ioctl.h"
#endif

    // open stream socket on port 9000 | Done
    // return 1 if something is wrong | Done
    // write messages to the system log "Accepted connection from X.X.X.X" | Done
    // read data and append it to the /var/tmp/aesdsocketdata. Create file if it does not exist | Done
    // use new line as delimiter between recieved packets | PACKED RECIEVED WHEN THE NEW LINE SYMBOLL IS FOUND | Done
    // return the full content of the  aesdsocketdata file to the client | Done
    // log message to the system log "Closed connection from X.X.X.X" | Done
    // accept connection until SIGINT and SIGTERM | Done
    // done all sending operations and REMOVE file | Done
    // log message "Caugth signal, exiting" in the system logs | Done
    // Modify your program to support a -d argument which runs the aesdsocket application as a daemon. | Done
    // When in daemon mode the program should fork after ensuring it can bind to port 9000.  | Done

#define EXIT_SUCCESS 0
#define GENERAL_ERROR 1
#define SIGNAL_EXIT_ERROR  2
#define ALLOCATION_MEMORY_ERROR 3
#define CLOCK_ERROR 4
#define CANNOT_LOCK_SAFE_FILE 5
#define CANNOT_CREATE_TIMER 6
#define CANNOT_SERIALIZE_TIME 7 

#define FILE_HANDLE -1

#ifdef USE_AESD_CHAR_DEVICE
const char* logFilePath = "/dev/aesdchar";
#else
const char* logFilePath = "/var/tmp/aesdsocketdata";
#endif

typedef enum client_status_enum
{
    CLIENT_UNKNOWN_STATUS = 0,     
    CLIENT_SUCCESS_STATUS = 1,
    CLIENT_SOCKET_ERROR_STATUS = 2,
    CLIENT_WRITE_ERROR_STATUS = 3,
    CLIENT_READ_FILE_ERROR_STATUS = 4,
    CLIENT_SEND_ERROR_STATUS = 5,
    CLIENT_IN_PROGRESS = 6,
    COUNT_STATUS = 7

}client_status_t;

bool is_client_processed(client_status_t status)
{
    switch (status)
    {
    case CLIENT_UNKNOWN_STATUS:
    case CLIENT_IN_PROGRESS:
        return false;

    case CLIENT_SUCCESS_STATUS:
    case CLIENT_SOCKET_ERROR_STATUS:        
    case CLIENT_WRITE_ERROR_STATUS:
    case CLIENT_SEND_ERROR_STATUS:
    case CLIENT_READ_FILE_ERROR_STATUS:
        return true;

    default:
        syslog(LOG_ERR, "Client has unknown status. Exit from the app");
        exit(EXIT_FAILURE);
    }
}

//Global program state

#define CLIENT_BUFFER_SIZE 256
#define TIMESTAMP_BUFFER_SIZE 64

list_t* pClientsList = NULL;
safe_file* g_loggingFile = NULL;

sigset_t g_SignalsMask;

int g_SignalNum = 0;
int g_BaseSocket = -1;

pid_t g_ParentId = 0;

timer_t g_TimerID = 0;
char g_TimeStamp_Buffer[TIMESTAMP_BUFFER_SIZE];

int join_client_thread(list_item_t* pitem, void* data);
void* handle_client(void* arg);

typedef struct 
{
    char buff[CLIENT_BUFFER_SIZE];
    pthread_t thread;
    int socket; 
    safe_file* file;
    atomic_int client_status;
    struct sockaddr client_ip_addr;
    char client_ip_addr_string[INET_ADDRSTRLEN];
}
client_data_t;

typedef struct
{
    int socket;
    struct sockaddr client_ip_addr;
    
}client_init_params_t;


int init_client(void* mem, size_t size, void* arg)
{
    if(mem == NULL || arg == NULL)
    {
        return LIST_INVALID_POINTER;
    }

    client_data_t* client = (client_data_t*)mem;

    client_init_params_t* init_params = (client_init_params_t*)arg;

    client->socket = init_params->socket;
    client->client_ip_addr = init_params->client_ip_addr;
    
    struct sockaddr_in* client_addr_in = (struct sockaddr_in*)(&client->client_ip_addr);
    inet_ntop(AF_INET, &(client_addr_in->sin_addr), client->client_ip_addr_string, INET_ADDRSTRLEN);

    client->file = g_loggingFile;

    atomic_store_explicit(&client->client_status, CLIENT_UNKNOWN_STATUS, memory_order_release);

    return pthread_create(&client->thread, NULL, handle_client, (void*)client);
}

int find_processed_client(list_item_t* pitem, void* data, bool* result)
{
    client_data_t* client = NULL;

    int error = list_item_get_custom_struct(pitem, (void*)&client);

    if( error != LIST_SUCCESS)
    {
        *result = false;
        return error;
    }

    client_status_t status = atomic_load_explicit(&client->client_status, memory_order_acquire);

    *result = is_client_processed(status);

    return LIST_SUCCESS;
}

void send_exit_signal_to_parent(int status)
{
    syslog(LOG_DEBUG, "Try to send exit signal with exit code %d to the parent", status);

    if(g_ParentId && getpid() != g_ParentId)
    {
        union sigval sigdata;
        sigdata.sival_int = status;
        if (sigqueue(g_ParentId, SIGQUIT, sigdata) != 0 )
        {
            syslog(LOG_ERR, "Can not send exit signal to the parent with exit status. Error:%d %s", errno, strerror(errno));
            if (kill(g_ParentId, SIGKILL) != 0 )
            {
                syslog(LOG_ERR, "Can not send kill signal to the parent process. Error:%d %s", errno, strerror(errno));
            }
        }
    }
    else
    {
        syslog(LOG_DEBUG, "Can not send exit status to the parent. Parent process doesn't exist or it was try from the single process mode");
    }
}

void timestamp_callback(union sigval data)
{
    int result = 0;
    struct timespec realtime;
    
    if ((result = clock_gettime(CLOCK_REALTIME, &realtime)) != 0)
    {
        syslog(LOG_ERR, "Can not get clock state. Error:%d %s", result, strerror(result));
        send_exit_signal_to_parent(CLOCK_ERROR);
        exit(CLOCK_ERROR);        
    }

    struct tm* currenttime = localtime(&realtime.tv_sec);

    if(!safe_file_lock(g_loggingFile))
    {
        const char* errstring = NULL;    
        result = safe_file_get_error(g_loggingFile, &errstring);
        syslog(LOG_ERR, "Can not lock safe file. Error:%d %s", result, errstring);
        send_exit_signal_to_parent(CANNOT_LOCK_SAFE_FILE);
        exit(CANNOT_LOCK_SAFE_FILE);        
    }

    result = strftime(g_TimeStamp_Buffer, TIMESTAMP_BUFFER_SIZE, "timestamp:%a, %d %b %Y %T %z\n", currenttime);

    if(result <= 0)
    {
        syslog(LOG_ERR, "Can not serialize time stamp to needed format. Error:%d %s", errno, strerror(errno));
        safe_file_unlock(g_loggingFile);
        send_exit_signal_to_parent(CANNOT_SERIALIZE_TIME);
        exit(CANNOT_SERIALIZE_TIME);
    }

    result = safe_file_write(g_loggingFile, g_TimeStamp_Buffer, result);

    if(!safe_file_unlock(g_loggingFile))
    {
        const char* errstring = NULL;    
        result = safe_file_get_error(g_loggingFile, &errstring);
        syslog(LOG_ERR, "Can not unlock safe file. Error:%d %s", result, errstring);
        send_exit_signal_to_parent(CANNOT_LOCK_SAFE_FILE);
        exit(CANNOT_LOCK_SAFE_FILE);        
    }
}

void handle_signal(const int signalNum)
{
    g_SignalNum = signalNum;
}


bool check_signal()
{
    if(g_SignalNum != 0)
    {
        printf("Got a signal %d %s. Exit from the app\n", g_SignalNum, strsignal(g_SignalNum));
        syslog(LOG_INFO, "Caugth signal, exiting");
        return true;
    }

    return false;
}

ssize_t packet_length(const char* const buff, const ssize_t buffsize, bool* isLast)
{
    if( buffsize == 0 )
    {
        *isLast = true;
        return 0;
    }

    for(ssize_t i = 0; i < buffsize; i++)
    {
        if( buff[i] == 0 || buff[i] == '\n')
        {
            *isLast = true;
            return i + 1;
        }
    }

    return buffsize;
}

bool sendcallback(const ssize_t readed, const char* const buff, const size_t buffsize, read_params_t const* params)
{
    if(params == NULL)
    {
        syslog(LOG_ERR, "Can not send data to a client. Call back parameter is NULL");
        return false;
    }

    ssize_t sended = 0;

    do 
    {
        const int result = send(params->num, buff, readed, 0);

        if( result < 0 )
        {
            syslog(LOG_ERR, "Can not send data to a client. Error:%d %s\n", errno, strerror(errno));
            return false;
        }

        sended += result;

    }while(sended < readed);

    return true;
}

void* handle_client(void* arg)
{
    client_data_t* client = (client_data_t*)arg;

    syslog(LOG_INFO, "Accepted connection from %s", client->client_ip_addr_string);

    atomic_store_explicit(&client->client_status, CLIENT_IN_PROGRESS, memory_order_release);

    ssize_t len = -1;
    int error = 0;
    bool isLast = false;

    while(1)
    {
        len = recv(client->socket, client->buff, CLIENT_BUFFER_SIZE, 0);

        syslog(LOG_INFO, "Readed packet with length:%ld", len);

        if( len < 0 )
        {
            error = errno;
            syslog(LOG_ERR, "Can not read from a client socket. Error:%d %s\n", error, strerror(error));
            shutdown(client->socket, SHUT_RDWR);
            close(client->socket);
            syslog(LOG_INFO, "Closed connection from %s", client->client_ip_addr_string);
            atomic_store_explicit(&client->client_status, CLIENT_SOCKET_ERROR_STATUS, memory_order_release);
            return NULL;
        }

        read_range_t range = {
            .start = {
                .whence = SEEK_SET,
                .offset = 0
            },

            .end = {
                .whence = SEEK_END,
                .offset = 0
            }
        };

#ifdef USE_AESD_CHAR_DEVICE        
        struct aesd_seekto seekto = {0,0};
        int matched = sscanf(client->buff, "AESDCHAR_IOCSEEKTO:%d,%d\n", &seekto.write_cmd, &seekto.write_cmd_offset);
        syslog(LOG_INFO, "Scanned seek command, matched items:%d", matched);
        if(matched == 2)
        {
            const int file_fd = safe_file_get_fd(client->file);
            if ( 0 != ioctl(file_fd, AESDCHAR_IOCSEEKTO, (unsigned long)&seekto))
            {
                syslog(LOG_ERR, "Can not process seek command from a client. Error:%d %s\n", errno, strerror(errno));
                errno = 0;
            }
            syslog(LOG_INFO, "Processed seek command: write_cmd=%d, write_cmd_offset=%d", seekto.write_cmd, seekto.write_cmd_offset);
            range.start.whence = SEEK_CUR;
            range.start.offset = 0;
        }
        else
#endif

        {
            const ssize_t packetLen = packet_length(client->buff, len, &isLast);

            if(packetLen != 0)
            {
                syslog(LOG_INFO, "Estimated packet length:%ld", packetLen);

                error = safe_file_seek_and_write(client->file, SEEK_END, 0, client->buff, len);

                if( error != 0)
                {
                    syslog(LOG_ERR, "Can not write to the file. Error:%d %s\n", error, safe_file_get_error_string(error));
                    shutdown(client->socket, SHUT_RDWR);
                    close(client->socket);
                    syslog(LOG_INFO, "Closed connection from %s", client->client_ip_addr_string);
                    atomic_store_explicit(&client->client_status, CLIENT_WRITE_ERROR_STATUS, memory_order_release);
                    return NULL;
                }

                syslog(LOG_INFO, "Length of the data had been written to the file:%ld", packetLen);
            }

            if( isLast )
            {
                error = safe_file_sync(client->file);

                if(error != 0)
                {
                    syslog(LOG_ERR, "Can not flush and sync file for writing. Error:%d %s\n", error, strerror(error));
                    error = 0;
                    errno = 0;
                }
            }
        }

        read_params_t param;
        param.num = client->socket;

        error = safe_file_read_range(client->file, &range, client->buff, CLIENT_BUFFER_SIZE, &param, &sendcallback );

        shutdown(client->socket, SHUT_RDWR);
        close(client->socket);
        syslog(LOG_INFO, "Closed connection from %s", client->client_ip_addr_string);

        if( error != 0)
        {
            syslog(LOG_ERR, "Can not read from the file and send to a client. Error:%d %s\n", error, safe_file_get_error_string(error));
                atomic_store_explicit(&client->client_status, CLIENT_READ_FILE_ERROR_STATUS, memory_order_release);
                return NULL;
        }
            
        atomic_store_explicit(&client->client_status, CLIENT_SUCCESS_STATUS, memory_order_release);
        return NULL;
    }

    atomic_store_explicit(&client->client_status, CLIENT_SUCCESS_STATUS, memory_order_release);
    return NULL;
}

void server_exit()
{
    int result = 0;

    if(g_BaseSocket != -1)
    {
        if( (result = close(g_BaseSocket)) != 0)
        {
            syslog(LOG_ERR, "Can not close server socket. Error:%d %s", 
                errno, 
                strerror(errno));
        }
    }

    if(g_loggingFile != NULL)
    {
        if( (result = safe_file_close(g_loggingFile)) != SF_SUCCESS)
        {
            syslog(LOG_ERR, "Can not close safe file. Error:%d %s", 
                result,
                safe_file_get_error_string(result));
        }

        if( (result = safe_file_destroy(g_loggingFile)) != SF_SUCCESS)
        {
            syslog(LOG_ERR, "Can not destroy safe file. Error:%d %s", 
                result, 
                safe_file_get_error_string(result));
        }
    }

#ifndef USE_AESD_CHAR_DEVICE    
    if( (result = remove(logFilePath)) != 0)
    {
        syslog(LOG_ERR, "Can not remove logging file. Error:%d %s",
            errno, 
            strerror(errno));
    }
#endif //USE_AESD_CHAR_DEVICE

    if(pClientsList != NULL)
    {
        result = list_for_each(pClientsList, join_client_thread, NULL);

        if((result = list_clear(pClientsList)) != LIST_SUCCESS)
        {
            syslog(LOG_ERR, "Can not clean list. Error:%d %s", 
                result,
                list_get_error_string(result));
        }

        if( (result = list_delete(&pClientsList)) != LIST_SUCCESS)
        {
            syslog(LOG_ERR, "Can not delete safelist. Error:%d %s", 
                result,
                list_get_error_string(result));
        }            
    }    
}

void run_server(int baseSocket)
{
    int error = 0;
#ifndef USE_AESD_CHAR_DEVICE 

    struct sigevent timestampevent = {
        .sigev_notify = SIGEV_THREAD,
        .sigev_notify_function = timestamp_callback        
    };

    struct itimerspec timer_duration = {
        .it_value.tv_sec = 10,
        .it_interval.tv_sec = 10
    };

    if((error = timer_create(CLOCK_MONOTONIC, &timestampevent, &g_TimerID)) != 0)
    {
        syslog(LOG_ERR, "Can not create timer. Error:%d %s", error, strerror(errno));
        send_exit_signal_to_parent(CANNOT_CREATE_TIMER);
        server_exit();
        exit(CANNOT_CREATE_TIMER);
    }

    if( (error = timer_settime(g_TimerID, 0, &timer_duration, NULL)) != 0 )
    {
        syslog(LOG_ERR, "Can not set timer value. Error:%d %s", errno, strerror(errno));
        send_exit_signal_to_parent(CANNOT_CREATE_TIMER);
        server_exit();
        exit(CANNOT_CREATE_TIMER);
    }

#endif //USE_AESD_CHAR_DEVICE

    if( (error = list_create(&pClientsList)) != LIST_SUCCESS )
    {
        syslog(LOG_ERR, "Can not create clients list. Error:%d", error);
        send_exit_signal_to_parent(GENERAL_ERROR);
        server_exit();
        exit(GENERAL_ERROR);
    }

    send_exit_signal_to_parent(EXIT_SUCCESS);

    while(1)
    {    
        if(check_signal())
        {
            server_exit();
            exit(CLIENT_SUCCESS_STATUS);
        }

        if ( listen(baseSocket, SOMAXCONN - 1) != 0)
        {
            if(check_signal())
            {
                server_exit();
                exit(EXIT_SUCCESS);
            }
            
            error = errno;
            syslog(LOG_ERR, "Can not listen base socket. Error: %d %s\n", error, strerror(error));
            server_exit();
            exit(GENERAL_ERROR);
        }

        struct sockaddr clientAddr;
        socklen_t clientAddrLen = sizeof(struct sockaddr);

        const int clientSocket = accept(baseSocket, &clientAddr, &clientAddrLen);

        if( clientSocket == -1)
        {
            if(check_signal())
            {
                server_exit();
                exit(EXIT_SUCCESS);
            }

            error = errno;
            syslog(LOG_ERR, "Can not accept client. Error:%d %s\n", error, strerror(error));
            //return GENERAL_ERROR;
            continue;
        }

        if (g_loggingFile == NULL)
        {
            int error = safe_file_init(logFilePath, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH, &g_loggingFile );

            if(error != SF_SUCCESS)
            {
                const char* errorstring = NULL;
                int error = safe_file_get_error(g_loggingFile, &errorstring);
                syslog(LOG_ERR, "Can not open file. Error:%d %s", error, errorstring == NULL? "NULL" : errorstring);
                send_exit_signal_to_parent(GENERAL_ERROR);
                server_exit();
                exit(GENERAL_ERROR);
            }
        }

        client_init_params_t client_init_params = {
            .socket = clientSocket,
            .client_ip_addr = clientAddr
        };

        list_item_t* processed_client_struct = NULL;
        client_data_t* client = NULL;

        error = list_find_if(pClientsList, find_processed_client, NULL, &processed_client_struct);

        if(error != LIST_SUCCESS)
        {
            syslog(LOG_ERR, "Can not search in the client list. Error:%d %s", 
                error, 
                list_get_error_string(error));                
            server_exit();
            exit(EXIT_FAILURE);
        }

        if(processed_client_struct != NULL)
        {            
            error = list_item_get_custom_struct(processed_client_struct, (void*)&client);

            if(error != LIST_SUCCESS)
            {
                syslog(LOG_ERR, "Can not get existed struct from the clients list. Error:%d %s", 
                    error, 
                    list_get_error_string(error));                                        
                server_exit();
                exit(EXIT_FAILURE);
            }

            error = pthread_join(client->thread, NULL);

            if( error != 0)
            {
                syslog(LOG_ERR, "Can not join thread of processed client. Error:%d %s",
                    error,
                    list_get_error_string(error));                
                server_exit();
                exit(EXIT_FAILURE);
            }

            error = init_client((void*)client, sizeof(client_data_t), (void*)&client_init_params);            
        }
        else
        {
            error = list_emplace_back((list_t*)pClientsList,
                    sizeof(client_data_t),
                    init_client,
                    (void*)(&client_init_params));
        }

        if(error != 0)
        {
            syslog(LOG_ERR,
                "Can not init a new client in the clients list. Error:%d %s",
                error,
                list_get_error_string(error));
            server_exit();
            exit(GENERAL_ERROR);
        }        
    }
}

int init_base_socket()
{
    int error = 0;

    struct addrinfo hints;

    struct addrinfo* res = NULL;

    const int baseSocket = socket(AF_INET, SOCK_STREAM, 0);

    if( baseSocket <= 0 )
    {
        error = errno;
        syslog(LOG_ERR, "Can not open socket. Error:%d %s\n", error, strerror(error));
        exit(GENERAL_ERROR);
    }

    //Set socket options
    const int enable = 1;
    if ( setsockopt(baseSocket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0 ||
         setsockopt(baseSocket, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int)) < 0 )
    {
        error = errno;
        syslog(LOG_ERR, "Can not set socket options. Error:%d %s\n", error, strerror(error));
        exit(GENERAL_ERROR);
    }

    memset(&hints, 0x00, sizeof(struct addrinfo));

    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if( (error = getaddrinfo(NULL, "9000" , &hints, &res)) != 0 )
    {
        syslog(LOG_ERR, "Can not get address. Error:%d %s\n", error, gai_strerror(error));
        exit(GENERAL_ERROR);
    }

    if ( bind(baseSocket, res->ai_addr, sizeof(struct sockaddr)) != 0 )
    {
        error = errno;
        syslog(LOG_ERR, "Can not bind socket. Error:%d %s\n", error, strerror(error));
        freeaddrinfo(res);
        exit(GENERAL_ERROR);
    }

    freeaddrinfo(res);

    g_BaseSocket = baseSocket;

    return baseSocket;
}

void init_signals_logic()
{
    int error = 0;

    sigemptyset(&g_SignalsMask);
    sigaddset(&g_SignalsMask, SIGINT);
    sigaddset(&g_SignalsMask, SIGTERM);

    struct sigaction actionINT;
    struct sigaction actionTERM;

    sigemptyset(&actionINT.sa_mask);
    actionINT.sa_handler = handle_signal;
    actionINT.sa_flags = 0;

    sigemptyset(&actionTERM.sa_mask);
    actionTERM.sa_handler = handle_signal;
    actionTERM.sa_flags = 0;

    if( sigaction(SIGINT, &actionINT, NULL) != 0 ||
        sigaction(SIGTERM, &actionTERM, NULL) != 0 )
    {
        error = errno;
        syslog(LOG_ERR, "Can not registry signal handlers. Error:%d %s\n", error, strerror(error));
        send_exit_signal_to_parent(GENERAL_ERROR);
        exit(GENERAL_ERROR);
    }
}

// Use for each with this func instead list_for_each()
int join_client_thread(list_item_t* pitem, void* data)
{
    client_data_t* pclient = NULL;

    int result = list_item_get_custom_struct(pitem, (void*)&pclient);

    if(result != LIST_SUCCESS)
    {
        return result;
    }

    return pthread_join(pclient->thread, NULL);
}

void init_demon(const int baseSocket)
{
    int error = 0;
    int pid = fork();

    if( pid < 0 )
    {
        error = errno;
        syslog(LOG_ERR, "Can not for fork. Error%d %s\n", error, strerror(error));
        exit(GENERAL_ERROR);
    }

    if( pid > 0 )
    {
        return;
    }

    if( setsid() < 0 )
    {
        error = errno;
        syslog(LOG_ERR, "Can not set session id. Error:%d %s\n", error, strerror(error));
        send_exit_signal_to_parent(GENERAL_ERROR);
        exit(GENERAL_ERROR);
    }

    if( chdir("/") != 0 )
    {
        error = errno;
        syslog(LOG_ERR, "Can not change dir to the root directory. Error:%d %s\n", error, strerror(error));
        send_exit_signal_to_parent(GENERAL_ERROR);
        exit(GENERAL_ERROR);
    }

    int x;
    for (x = sysconf(_SC_OPEN_MAX); x>=0; x--)
    {
        if(x != baseSocket)
        {
            close (x);
        }
    }

    int null = open("/dev/null", O_WRONLY);

    if( null < 0 )
    {
        error = errno;
        syslog(LOG_ERR, "Can not open null device. Errro:%d %s\n", error, strerror(error));
        send_exit_signal_to_parent(GENERAL_ERROR);
        exit(GENERAL_ERROR);
    }

    if( dup2(null, 0 ) < 0 ||
        dup2(null, 1) < 0 ||
        dup2(null, 2) < 0 )
    {
        error = errno;
        syslog(LOG_ERR, "Can not redirect output to the null device. Error:%d %s", error, strerror(error));
        send_exit_signal_to_parent(GENERAL_ERROR);
        exit(GENERAL_ERROR);
    }
}

int main(int argc, char** argv)
{
    int opt = getopt(argc, argv, "d");

    if( opt == 'd')
    {
        openlog("aesdsocket", LOG_PID, LOG_DAEMON);
        g_ParentId = getpid();

    }
    else
    {
        openlog("aesdsocket", LOG_PERROR, LOG_USER);
    }

    const int baseSocket = init_base_socket();

    if( opt == 'd') //demon mode
    {
        init_demon(baseSocket);
        if(g_ParentId == getpid())
        {
            //Parent thread must wait signal with return code from the child
            sigset_t mask;
            siginfo_t siginfo;
            int signum;

            sigemptyset(&mask);
            sigaddset(&mask, SIGQUIT);

            syslog(LOG_INFO, "Start wait QUIT signal from the child process");
            signum = sigwaitinfo(&mask, &siginfo);

            if(signum < 0)
            {
                syslog(LOG_ERR, "The parent process got an error while wait quit signal. Can not wait signal. Error:%d %s", errno, strerror(errno));
                return SIGNAL_EXIT_ERROR;
            }

            syslog(LOG_INFO, "Recieved signal:%d Exit from the parent process with status code:%d", signum,  siginfo.si_int );
            exit(siginfo.si_int);
        }
    }

    init_signals_logic();

    run_server(baseSocket);

    return EXIT_SUCCESS;
}
