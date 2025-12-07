/*
** aesdsocket.c -- a stream socket server (nv)
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdarg.h>
#include <syslog.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/queue.h>
#include <pthread.h>
#include <time.h>



#define PORT "9000"  // the port users will be connecting to

#define BACKLOG 10   // how many pending connections queue will hold

#define BUFFER_SIZE 1024  

#define TIME_DELAY  10     // 10 seconds

#define false 0
#define true !false

const char* server_name = "aesdsocket";

const char* savefile_name = "/var/tmp/aesdsocketdata";



int server_fd;
pthread_t timer_thread_id;


// start as daemon
int start_as_daemon = false;


// Running Flag
volatile sig_atomic_t running = 1;

// threads-management with SLIST.
typedef struct thread_data_s thread_data_t;
struct thread_data_s {
    int fd;
    pthread_t tid;
    char   ip_adr[INET_ADDRSTRLEN];
    int    finish;
    SLIST_ENTRY(thread_data_s) next;
};

// List of threads
SLIST_HEAD(slist_head, thread_data_s);
struct slist_head threads_head = SLIST_HEAD_INITIALIZER(threads_head);

// Mutex file sync
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

// Mutes SLIST
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

// Mutex syslog
pthread_mutex_t syslog_mutex = PTHREAD_MUTEX_INITIALIZER;


// returncode on termination
int retcode = 0;


/*
 * handle_sigtimer  -  interrupt timer with SIGUSR1
 */
void handle_timer(int sig) 
{
    (void)sig;
}

/*
** Set Signal with sigaction
*/
int set_signal(int sig, void (*handler)(int), int flags) 
{

    // set sigaction 
    struct sigaction sa;
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = flags;
    if (sigaction(sig, &sa, NULL) == -1 )
    {
        return( -1 );
    }
    
    return 0;
}

/*
 *  Timer to write timestamp to file
 */
void *timer_thread(void* arg) 
{
    (void)arg;
    
    struct timespec next;  
    
    set_signal(SIGUSR1, handle_timer, 0);         // if error thread can carry on
            
    clock_gettime(CLOCK_REALTIME, &next);	  // get time
    //clock_gettime(CLOCK_MONOTONIC, &next);
    
    while( running ) {
        
        next.tv_sec += TIME_DELAY;
                 
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char date_buf[128];
        
        // print time format
        strftime(date_buf, sizeof(date_buf), "timestamp:%a %d %b %H:%M:%S %Y", tm_info);
        //strftime(date_buf, sizeof(date_buf), "timestamp:%Y-%d-%b %H:%M:%S.  %z", tm_info);

        // write to file with locking        
        pthread_mutex_lock(&file_mutex);
        FILE *wp = fopen(savefile_name,"a");
        if ( wp ) {
   
            if ( fprintf(wp,"%s\n", date_buf) <  0 ) {
                pthread_mutex_unlock(&file_mutex);
                
                pthread_mutex_lock(&syslog_mutex);
                syslog(LOG_DEBUG, "-> error timer_thread - fprintf(wp) < 0" );
                pthread_mutex_unlock(&syslog_mutex);
                
                fclose(wp);
                retcode = -1;
                
                // Terminate Application
                kill(getpid(), SIGTERM);
                break;
            }

            // close fp
            if ( fclose(wp) == -1 ) {
                pthread_mutex_unlock(&file_mutex);
                
                pthread_mutex_lock(&syslog_mutex);
                syslog(LOG_DEBUG, "-> error timer_thread fclose(wp) == -1" );
                pthread_mutex_unlock(&syslog_mutex);
                
                retcode = -1;
                // Terminate Application
                kill(getpid(), SIGTERM);
                break;
            }
            
        } 
        else {
          
            // wp == NULL  
            pthread_mutex_unlock(&file_mutex);
          
               
            pthread_mutex_lock(&syslog_mutex);
            syslog(LOG_DEBUG, "-> error timer_thread fclose(wp) " );
            pthread_mutex_unlock(&syslog_mutex);
           
            retcode = -1;
            // Terminate Application
            kill(getpid(), SIGTERM);
            break;
        }
        
        pthread_mutex_unlock(&file_mutex);
    
#ifdef use_MONOLITIC       

        struct timespec cur, diff;

        //clock_gettime(CLOCK_REALTIME, &cur);
        clock_gettime(CLOCK_MONOTONIC, &cur);
        
        diff.tv_sec = next.tv_sec - cur.tv_sec;
        diff.tv_nsec = next.tv_nsec - cur.tv_nsec;
        
        if (diff.tv_nsec < 0) {
            diff.tv_sec--;
            diff.tv_nsec += 1000000000L;
        }
        
        if (diff.tv_sec < 1 )
            diff.tv_sec = 1;       // at least one second


        //int rn = clock_nanosleep(CLOCK_MONOTONIC, 0, &diff, NULL);
        //int rn = nanosleep(&diff, &cur);
        
        pthread_mutex_lock(&syslog_mutex);
        syslog(LOG_DEBUG, " timer =%ld . %ld  rn = %d", diff.tv_sec, diff.tv_nsec, rn );
        pthread_mutex_unlock(&syslog_mutex);

#endif        
        
        int rn = clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &next, NULL);
    
        if (rn < 0 ) {   // timer interrupted
        
            if (errno == EINTR) {    // not active with SA_RESTART
                pthread_mutex_lock(&syslog_mutex);
                syslog(LOG_DEBUG, "--> nanosleep  rn=%d interrupted errno = %d \n", rn, errno );
                pthread_mutex_unlock(&syslog_mutex);
                break;
            }
 
            // break;
        }
    
    }

    return NULL;
}



/*
 * Handler for SIGINT and SIGTERM: shutdown
 */ 
void handle_shutdown(int signo)
{

    (void)signo;

    // terminate main loop
    running = 0;

    if (! start_as_daemon) {
        const char sigmsg[] = "Socket shutdown, exiting \n";
        write(STDOUT_FILENO, sigmsg, sizeof(sigmsg) - 1); 
    }
    
    // close server connection -> exit while accept() loop   
    shutdown(server_fd, SHUT_RDWR);

    // send Signal to timer_thread to interrupt timer
    pthread_kill(timer_thread_id, SIGUSR1);

    pthread_mutex_lock(&syslog_mutex);
    syslog(LOG_DEBUG, "-> shutdown %d ", signo);
    pthread_mutex_unlock(&syslog_mutex);
 
    // look for open connections
    // socket threads (connections) are terminated in main()
    // ...   
}


/*
 *  respond to pair
 */
int respond(int client_fd)
{
    // Answer on received string
 
    char readfile_buffer[BUFFER_SIZE+1]; // buffer could be reused
    int  sentbytes = 0;
    size_t total_sent;
    
    // lock file access  
    pthread_mutex_lock(&file_mutex);
    
    // open file  -
    FILE* fp = fopen(savefile_name, "r");
    if (fp == NULL) {
        pthread_mutex_unlock(&file_mutex);

        pthread_mutex_lock(&syslog_mutex);
        syslog(LOG_DEBUG, "-> respond fopen(fp) == NULL" );
        pthread_mutex_unlock(&syslog_mutex);
        return -2;
    }

    // read file line by line
    while(fgets(readfile_buffer, BUFFER_SIZE , fp) != NULL) {
        
        total_sent = 0;
        
        // send if length of line has characters
        if (strlen(readfile_buffer) > 0 ) {
            
            // send whole buffer
            while ( total_sent < strlen(readfile_buffer) ) {
    
                sentbytes = send(client_fd, &readfile_buffer[total_sent], strlen(readfile_buffer) - total_sent, 0);
                
                if (sentbytes < 0) {
                    // error on send
                    pthread_mutex_unlock(&file_mutex);
                    fclose(fp);
                    
                    pthread_mutex_lock(&syslog_mutex);
                    syslog(LOG_DEBUG, "-> respond send < 0" );
                    pthread_mutex_unlock(&syslog_mutex);

                    return -3;
                }
                // send(): return of 0 seems to be allowed 
                //if (sentbytes == 0 ) {
                //    //  
                //    pthread_mutex_unlock(&file_mutex);
                //    fclose(fp);
                   
                //    pthread_mutex_lock(&syslog_mutex);
                //    syslog(LOG_DEBUG, "-> respond send == 0" );
                //    pthread_mutex_unlock(&syslog_mutex);
                //    return false;
                //}
                
                total_sent += sentbytes;
                
            }
        }
    }

    // close file
    if (fclose(fp) == -1)
    {
        pthread_mutex_unlock(&file_mutex);

        pthread_mutex_lock(&syslog_mutex);
        syslog(LOG_DEBUG, " -> respond send - fclose(fp)" );
        pthread_mutex_unlock(&syslog_mutex);
        return -2;        
    }

    // strings sent
    pthread_mutex_unlock(&file_mutex);
    
    return 0;
	
}

/*
 * add SLIST node to list
 */
void add_client_nodes( struct thread_data_s *node)
{

    pthread_mutex_lock(&clients_mutex);
    SLIST_INSERT_HEAD(&threads_head, node, next);
    pthread_mutex_unlock(&clients_mutex);

}

/*
 * remove SLIST node from list
 */
void remove_client_nodes(struct thread_data_s * node)
{
               
    pthread_mutex_lock(&clients_mutex);
    SLIST_REMOVE(&threads_head, node, thread_data_s, next);
    pthread_mutex_unlock(&clients_mutex);

}


/*
 * Socket Client thread
 */
void *client_thread(void* arg)
{

    struct thread_data_s *node = (struct thread_data_s*)arg;
 
    // fd in node   
    int client_fd = node->fd;
    
    char buffer[BUFFER_SIZE];
    char* recv_buffer = NULL;
    ssize_t n = 0;
    size_t total = 0;
    int res;
    
    // assign thread_id 
    pthread_mutex_lock(&clients_mutex);
    node->tid = pthread_self();
    pthread_mutex_unlock(&clients_mutex);
    
    // syslog report accepted connection
    pthread_mutex_lock(&syslog_mutex);
    syslog(LOG_DEBUG, "Accepted connection from %s", node->ip_adr);
    pthread_mutex_unlock(&syslog_mutex);
            

    // while chars are received   
    while ((n = recv(client_fd, buffer, BUFFER_SIZE, 0)) > 0 ) {

        // terminate buffer    
        buffer[n] = '\0';
        
        // assemble reception string
        char* newBuffer = realloc(recv_buffer, total + n + 1);  // + 1: '\0'
                        
        // Ignore if realloc()/malloc() fails
        if (newBuffer) {
            recv_buffer = newBuffer;
            // append characters from last recv()
            memcpy(recv_buffer + total, buffer, n);
            total += n;
            recv_buffer[total] = '\0';
        }
        
        // LF received?
        if (recv_buffer != NULL && strchr(recv_buffer, '\n') != NULL) {
        
            // recv_buffer should not be NULL if variable total is > 0
            if  (total > 0 && recv_buffer != NULL) {

                pthread_mutex_lock(&file_mutex);                 /// lock file_mutex
                
                // open save-file in append mode
                FILE* wp = fopen(savefile_name, "a");
                if (wp == NULL) {
                    // error open file
                    pthread_mutex_unlock(&file_mutex);

                    pthread_mutex_lock(&syslog_mutex);
                    syslog(LOG_DEBUG, "-> client_thread fopen(wp) == NULL" );
                    pthread_mutex_unlock(&syslog_mutex);
                    n = -2;  // show error condition
                    break;
                }

                // write to save-file
                if (fprintf(wp,"%s", recv_buffer) < 0) {
                    // error write string
                    fclose(wp);
                    pthread_mutex_unlock(&file_mutex);

                    pthread_mutex_lock(&syslog_mutex);
                    syslog(LOG_DEBUG, "-> client_thread fprintf(wp) < 0" );
                    pthread_mutex_unlock(&syslog_mutex);
                    n = -2;   // show error condition
                    break;
                }

                // close save-file
                if (fclose(wp) == -1) {
                    // error on fclose()
                    pthread_mutex_unlock(&file_mutex);

                    pthread_mutex_lock(&syslog_mutex);
                    syslog(LOG_DEBUG, "-> client_thread fclose(wp)" );
                    pthread_mutex_unlock(&syslog_mutex);
                    n = -2;   // show error condition
                    break;
                }

                pthread_mutex_unlock(&file_mutex);                   // unlock file_mutex

                // recv_buffer content no longer necessary
                free(recv_buffer);
                recv_buffer = NULL;
                total = 0;
                    
       
                // Respond to client
       	        if ((res = respond(client_fd)) != 0 ) {
       	            n = res;
       	            break;
       	        }
       	   } 
        }
        
    }   

    // connection closed
    if (n == 0) {
        
        pthread_mutex_lock(&syslog_mutex);
        syslog(LOG_DEBUG, "Closed connection from %s", node->ip_adr);
        pthread_mutex_unlock(&syslog_mutex);
    } 

    // free remaining assigned memory
    if (recv_buffer != NULL) {
        free(recv_buffer);
        recv_buffer = NULL;
    }
          
    close(client_fd);
      
    // error condition on recv() or n==-2
    if ( n < 0 ) {    
         pthread_mutex_lock(&syslog_mutex);
         syslog(LOG_DEBUG, "  client_thread break = %ld \n", n);
         pthread_mutex_unlock(&syslog_mutex);

        if ( n==-2 ) {
            // terminate
            retcode = -1;
            kill(getpid(), SIGTERM);
        }
    }
  
    // node no longer used
    node->finish = 1;
  
    return NULL;
  
}


/*
 * Terminate Threads 
 */
void *termini_thread(void* arg)
{
    (void)arg;
    
    struct thread_data_s *item, *nxt;
  
    // make sure all list nodes are terminated   
    int cleared = 0;
    
    while ( ! cleared )  {
     
        //pthread_mutex_lock(&clients_mutex);
        
        item = SLIST_FIRST(&threads_head);
    
        while (item != NULL ) {
    
    
           if (item->finish == 1) {
        
    
                // shutdown client
                shutdown( item->fd, SHUT_RDWR);
        
                // wait until thread has terminated
    	        pthread_join(item->tid, NULL);
     
                pthread_mutex_lock(&clients_mutex);          // lock clients_mutex
  
                // get next node
                nxt = SLIST_NEXT(item, next);         
          
                // remove node
                SLIST_REMOVE(&threads_head, item, thread_data_s, next);
          
                pthread_mutex_unlock(&clients_mutex);        // unlock client_mutex
  
                // free SLIST memory 
                free(item);
        
                // point item to next node
                item = nxt;
            }
            else {
                // search with next node
                item = SLIST_NEXT(item, next);         
            }    
        }

          
        // stopped running  
        if ( ! running ) {
            // get first node of list
            item = SLIST_FIRST(&threads_head);

            if ( item == NULL ) {
                // list empty    
                cleared = 1;
            }
        }

        if (!cleared)
            sleep(1);   // 1 sec
    }

    return NULL;
}


/*
 * Request all threads in SLIST to terminate
 */
void CleanThreads()
{
   
     struct thread_data_s *item = SLIST_FIRST(&threads_head);
     
    pthread_mutex_lock(&clients_mutex);                             // lock clients_mutex
    while (item != NULL ) {
        
         
        // request termination
        item->finish = 1;
                  
        item = SLIST_NEXT(item, next );
    }
    pthread_mutex_unlock(&clients_mutex);                           // unlock clients_mutex
    
}

/*
** get sockaddr, IPv4 or IPv6: 
*/
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}


/* 
 * main(): aesdscoket
 *
 */
int main(int argc, char** argv)
{

    // addrinfos 
    struct addrinfo hints, *servinfo;
    int yes = 1;
  
    // Initialize SLIST elements
    SLIST_HEAD(threadhead, thread_data_s) threads_head;
    SLIST_INIT(&threads_head);

    // search for '-d' argument
    for ( int i = 1; i < argc; i++ ) {
        if ( strcmp(argv[i], "-d" ) == 0 ) {
            
            // set variable for aesdsocket to be started as daemon
            start_as_daemon = true;
        }
    }
   
    // Openlog again - for child
    openlog("aesdsocket", LOG_PID | LOG_CONS, LOG_USER);  
    
    
    
    // set signal_handler: SIGINT / SIGTERM
    if ( (set_signal(SIGTERM, handle_shutdown, 0) == -1) || 
         (set_signal(SIGINT, handle_shutdown, 0) == -1) ) {
       
        syslog(LOG_DEBUG,  "error: set_signal()");
        closelog();            
         
        exit(-1);    
    }
    
               
    // init TCP Parameters: here only AF_INET been tested
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;        // alternatives AF_UNSPECIFIC or AF_INET6
    hints.ai_socktype = SOCK_STREAM;  // TCP
    hints.ai_flags = AI_PASSIVE;      // Server

    int status;
    if (( status = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0 ) {
    
        syslog(LOG_DEBUG, "error: main getaddrinfo()");

        closelog();
        exit(-1);
    }
 
    // open (server-)socket
    server_fd = socket( servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if ( server_fd == -1 ) {
       freeaddrinfo(servinfo);

       syslog(LOG_DEBUG, "error: main socket()");
       closelog();
       exit(-1);
    }

    // set socketopt
    if ( setsockopt( server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1 ) {
       freeaddrinfo(servinfo);
      
       syslog(LOG_DEBUG, "error: main setsockopt()");
       close(server_fd);
       closelog();
       exit(-1);
    }
    
    // bind to AF_INET
    if ( bind( server_fd, servinfo->ai_addr, servinfo->ai_addrlen) == -1 ) {
       freeaddrinfo(servinfo);
       
       syslog(LOG_DEBUG, "error: main bind()");
       close(server_fd);
       closelog();
       exit(-1);
    }

    // no longer in use
    freeaddrinfo(servinfo);

    // start listen
    if ( listen(server_fd, BACKLOG) == -1 ) {
        syslog(LOG_DEBUG, "error: main listen()");
        close(server_fd);
        closelog();
        exit(-1);
    }


    // in case of daemon
    if ( start_as_daemon ) {

        // pid for forks
        pid_t pid;

        // 1st Fork
        pid = fork();
        if ( pid < 0 ) {
            // Error on fork()
            syslog(LOG_DEBUG, "error: daemonize 1st fork (pid < 0)");    
   
            close( server_fd );
            closelog();
            exit(-1);
        }
        if ( pid > 0 ) {
            // Parent  - terminate
            close( server_fd);
            closelog();
            exit(0);
        }

        // child keeps on running
    
        // set sid
        if ( setsid() < 0 ) {
            close( server_fd);

            syslog(LOG_DEBUG, "error: daemonize setsid()");
            closelog();
            exit(-1);
        }

        // 2nd fork for daemon
        pid = fork();
        if ( pid < 0 ) {
            syslog(LOG_DEBUG, "error: daemonize 2nd fork (pid < 0)");

            close( server_fd);
            closelog();
            exit(-1);
        }
    
        if ( pid > 0 ) {
            // Parent
            close( server_fd);
            closelog();
            exit(0);
         }

         // set up environment
         umask( 0 );
         // set path
         chdir("/");

         // Close open files: stdin, stdout, stderr   
         close( STDIN_FILENO );
         close( STDOUT_FILENO );
         close( STDERR_FILENO );
     
         // link stdin, stdout, stderr to /dev/null
         int fd = open("/dev/null", O_RDWR);
         dup2(fd, STDIN_FILENO);
         dup2(fd, STDOUT_FILENO);
         dup2(fd, STDERR_FILENO);

     } // start_as_daemon
     
     
    
    //  Starte 10 Sec Timer    
    if (pthread_create(&timer_thread_id, NULL, timer_thread, NULL ) != 0 ) {
    
        syslog(LOG_DEBUG, "error: pthread_create()");
        closelog();
        exit(-1);    
    }
    
    // starte thread for closing of clients
    pthread_t termini_id;
    
    if (pthread_create(&termini_id, NULL, termini_thread, NULL ) != 0 ) {
    
        pthread_mutex_lock(&syslog_mutex);
        syslog(LOG_DEBUG, "error: main pthread_termini()");
        pthread_mutex_unlock(&syslog_mutex);

        close(server_fd);
      
        running = 0;
    
        // send Signal to timer_thread to interrupt timer
        pthread_kill(timer_thread_id, SIGUSR1);
        
        // wait timer thread to terminate
        pthread_join(timer_thread_id, NULL);
        
        closelog();
        exit(-1);    
    }


    // accept handler
    int client_fd;
    struct sockaddr_storage client_addr;
    socklen_t client_len;

    // loop for new connections    
    while( running ) {

        if(!start_as_daemon)
            printf("server: waiting for connections...\n");


        // accept connection
        client_len = sizeof client_addr;
        client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0 ) {
        
            if (!running) {
                break;         // terminate
            }
            if (errno == EINTR) {    // not active with SA_RESTART
                continue;
            }
            
            // error accept -> stop
            pthread_mutex_lock(&syslog_mutex);
            syslog(LOG_DEBUG, "error: accept()");
	    pthread_mutex_unlock(&syslog_mutex);

            close(server_fd);
            
            CleanThreads();
            running = 0;

            pthread_kill(timer_thread_id, SIGUSR1);
            pthread_join(timer_thread_id, NULL);
    
            pthread_join(termini_id, NULL);

            closelog();
            exit (-1);
        }
        
        // client_fd valid
        struct thread_data_s *node = malloc(sizeof(*node));
	        
        if (!node) {
            
            pthread_mutex_lock(&syslog_mutex);
            syslog(LOG_DEBUG, "error: node==NULL");
            pthread_mutex_unlock(&syslog_mutex);

            close(server_fd);
            
            // Close clients
            CleanThreads();

            // terminate timer_thread and termini_thread
            running = 0;
            pthread_kill(timer_thread_id, SIGUSR1);
            pthread_join(timer_thread_id, NULL);

            pthread_join(termini_id, NULL);
            
            closelog();

            exit (-1);
        } 
        
        // assign values
        node->fd = client_fd;
        node->tid = 0;
        node->finish = 0;

        // get IP address
        inet_ntop(client_addr.ss_family, get_in_addr((struct sockaddr *)&client_addr), node->ip_adr, sizeof (node->ip_adr));

        // add to list of threads        
        add_client_nodes(node);
        
        // Start thread
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
        
        int rc = pthread_create(&node->tid, &attr, client_thread, node);
        
        pthread_attr_destroy(&attr);
        
        if (rc != 0) {
            pthread_mutex_lock(&syslog_mutex);
            syslog(LOG_DEBUG, "error: main pthread_create()");
            pthread_mutex_unlock(&syslog_mutex);
           
            close(server_fd);
             
            CleanThreads();

            // terminate timer_thread and termini_thread
            running = 0;
            pthread_kill(timer_thread_id, SIGUSR1);
            pthread_join(timer_thread_id, NULL);

            pthread_join(termini_id, NULL);

            closelog();
            exit(-1);            
        }

    } // while running
    
    // force all threads to terminate      
    CleanThreads();

    // wait termini_thread to terminate -> all threads should be closed
    pthread_join(termini_id, NULL);

  
    // remove savefile on close    
    int rc;
    pthread_mutex_lock(&file_mutex);                           // lock file_mutex
        
    if ( (rc = unlink(savefile_name)) != 0 ) {
        pthread_mutex_unlock(&file_mutex);
         
         
        pthread_mutex_lock(&syslog_mutex);
        syslog(LOG_DEBUG, "error: unlink() rc = %d errno = %d ", rc, errno);
        pthread_mutex_unlock(&syslog_mutex);
        // notify error condition
        closelog();
        exit(-1);
     }
     
     pthread_mutex_unlock(&file_mutex);                        // unlock file_mutex

     // wait timer_thread to terminate
     pthread_join(timer_thread_id, NULL);
         
     closelog(); 

     return retcode;

}



