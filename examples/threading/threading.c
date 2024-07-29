#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg, ...)
// #define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg, ...) printf("threading ERROR: " msg "\n", ##__VA_ARGS__)

void *threadfunc(void *thread_param)
{
    struct thread_data *data_ptr = (struct thread_data *)thread_param;

    // Sleeps wait_to_obtain_ms number of milliseconds
    if (usleep(data_ptr->wait_to_obtain_ms) != 0)
    {
        return data_ptr;
    }

    // Obtains the mutex in mutex
    if (pthread_mutex_lock(data_ptr->mutex) != 0)
    {
        return data_ptr;
    }

    // Holds for wait_to_release_ms milliseconds
    if (usleep(data_ptr->wait_to_release_ms) != 0)
    {
        pthread_mutex_unlock(data_ptr->mutex);
        return data_ptr;
    }

    // Releases the mutex
    data_ptr->thread_complete_success = true;
    pthread_mutex_unlock(data_ptr->mutex);
    return data_ptr;
}

bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex, int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * Allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */
    struct thread_data *data_ptr = malloc(sizeof(struct thread_data));
    if (!data_ptr)
    {
        return false;
    }
    if (wait_to_obtain_ms < 0)
    {
        return false;
    }
    if (wait_to_release_ms < 0)
    {
        return false;
    }

    data_ptr->mutex = mutex;
    data_ptr->wait_to_obtain_ms = wait_to_obtain_ms;
    data_ptr->wait_to_release_ms = wait_to_release_ms;
    data_ptr->thread_complete_success = false;

    return pthread_create(thread, NULL, threadfunc, data_ptr) == 0;
}
