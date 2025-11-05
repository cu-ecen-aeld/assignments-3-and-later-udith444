#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{
    // Cast to obtain thread arguments from parameter
    struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    
    // Initialize success flag to false initially
    thread_func_args->thread_complete_success = false;
    
    // Wait for specified time before obtaining mutex
    usleep(thread_func_args->wait_to_obtain_ms * 1000);
    
    // Obtain the mutex
    if (pthread_mutex_lock(thread_func_args->mutex) != 0) {
        ERROR_LOG("Failed to obtain mutex");
        return thread_param;
    }
    
    // Wait for specified time while holding mutex
    usleep(thread_func_args->wait_to_release_ms * 1000);
    
    // Release the mutex
    if (pthread_mutex_unlock(thread_func_args->mutex) != 0) {
        ERROR_LOG("Failed to release mutex");
        return thread_param;
    }
    
    // If we reached here, thread completed successfully
    thread_func_args->thread_complete_success = true;
    
    return thread_param;
}

bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex, int wait_to_obtain_ms, int wait_to_release_ms)
{
    // Allocate memory for thread_data
    struct thread_data *thread_data = (struct thread_data *)malloc(sizeof(struct thread_data));
    if (thread_data == NULL) {
        ERROR_LOG("Failed to allocate memory for thread_data");
        return false;
    }
    
    // Setup the thread data structure
    thread_data->mutex = mutex;
    thread_data->wait_to_obtain_ms = wait_to_obtain_ms;
    thread_data->wait_to_release_ms = wait_to_release_ms;
    thread_data->thread_complete_success = false; // Initialize to false
    
    // Create the thread
    int result = pthread_create(thread, NULL, threadfunc, thread_data);
    if (result != 0) {
        ERROR_LOG("Failed to create thread: error code %d", result);
        free(thread_data);
        return false;
    }
    
    DEBUG_LOG("Thread started successfully");
    return true;
}

