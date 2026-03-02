#include <sched.h>
#include <stdio.h>
#include <syslog.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

// defines
#define NSEC_PER_SEC 1000000000
#define NSEC_PER_100MSEC 100000000
#define UPDATE_PRIORITY 1
#define READ_PRIORITY 2
#define MAIN_PRIORITY 99

// structs
typedef struct
{
    double position;
    double velocity;
    double acceleration;
    double direction;
    struct timespec time;
} dataStructure;

dataStructure sharedData;

struct sched_param Main_param, Read_param, Update_param;

sem_t Read_Data_Sem, Update_Data_Sem;
pthread_mutex_t Shared_Mutex;

// globals
int runPrograms = 1;
pthread_t Read_thread, Update_thread;
pthread_attr_t Main_attr, Read_attr, Update_attr;

static inline double getDelta(struct timespec *start, struct timespec *stop)
{
    return stop->tv_sec - start->tv_sec +
           (double)(stop->tv_nsec - start->tv_nsec) / NSEC_PER_SEC;
}

void *safe_read(void *thread_id)
{
    sem_wait(&Read_Data_Sem);

    while (runPrograms)
    {
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += 10;

        int ret = pthread_mutex_timedlock(&Shared_Mutex, &timeout);

        if (ret == ETIMEDOUT)
        {
            struct timespec now;
            clock_gettime(CLOCK_REALTIME, &now);

            double timestamp =
                now.tv_sec + (double)now.tv_nsec / NSEC_PER_SEC;

            printf("No new data available at %.9lf seconds\n",
                   timestamp);
        }
        else if (ret == 0)
        {
            printf("Read data: position=%.2f, velocity=%.2f, acceleration=%.2f, direction=%.2f\n",
                   sharedData.position,
                   sharedData.velocity,
                   sharedData.acceleration,
                   sharedData.direction);

            pthread_mutex_unlock(&Shared_Mutex);
        }
        else
        {
            perror("pthread_mutex_timedlock error");
        }

        sem_wait(&Read_Data_Sem);
    }
    return NULL;
}

void *safe_update(void *thread_id)
{
    sem_wait(&Update_Data_Sem);

    while (runPrograms)
    {
        struct timespec startTime, stopTime;
        clock_gettime(CLOCK_REALTIME, &startTime);

        pthread_mutex_lock(&Shared_Mutex);

        sharedData.time = startTime;
        sharedData.acceleration++;

        if (sharedData.acceleration > 4)
            sharedData.acceleration = -2;

        sharedData.velocity += sharedData.acceleration;
        sharedData.position += sharedData.velocity;
        sharedData.direction += 30;

        if (sharedData.direction >= 360)
            sharedData.direction -= 360;

        // Hold mutex long enough to trigger timeout
        sleep(12);

        clock_gettime(CLOCK_REALTIME, &stopTime);
        double delta = getDelta(&startTime, &stopTime);

        printf("Update finished at %.9lf in %.9lf seconds\n",
               stopTime.tv_sec +
                   (double)stopTime.tv_nsec / NSEC_PER_SEC,
               delta);

        pthread_mutex_unlock(&Shared_Mutex);

        sem_wait(&Update_Data_Sem);
    }
    return NULL;
}

int main()
{
    openlog("LOG_Exercise2_Part2", LOG_PID, LOG_USER);

    struct timespec frequency = {0, NSEC_PER_100MSEC};

    sem_init(&Read_Data_Sem, 0, 0);
    sem_init(&Update_Data_Sem, 0, 0);

    pthread_mutex_init(&Shared_Mutex, NULL);

    pthread_attr_init(&Main_attr);
    pthread_attr_init(&Read_attr);
    pthread_attr_init(&Update_attr);

    pthread_attr_setinheritsched(&Main_attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setinheritsched(&Read_attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setinheritsched(&Update_attr, PTHREAD_EXPLICIT_SCHED);

    pthread_attr_setschedpolicy(&Main_attr, SCHED_FIFO);
    pthread_attr_setschedpolicy(&Read_attr, SCHED_FIFO);
    pthread_attr_setschedpolicy(&Update_attr, SCHED_FIFO);

    Main_param.sched_priority = MAIN_PRIORITY;
    Read_param.sched_priority = READ_PRIORITY;
    Update_param.sched_priority = UPDATE_PRIORITY;

    if (sched_setscheduler(getpid(), SCHED_FIFO, &Main_param))
    {
        perror("sched_setscheduler, try running with sudo");
        return -1;
    }

    pthread_attr_setschedparam(&Read_attr, &Read_param);
    pthread_attr_setschedparam(&Update_attr, &Update_param);

    pthread_create(&Update_thread, &Update_attr, safe_update, NULL);
    pthread_create(&Read_thread, &Read_attr, safe_read, NULL);

    for (int i = 0; i < 5; i++)
    {
        sem_post(&Update_Data_Sem);
        sem_post(&Read_Data_Sem);
        nanosleep(&frequency, NULL);
    }

    runPrograms = 0;

    sem_post(&Update_Data_Sem);
    sem_post(&Read_Data_Sem);

    pthread_join(Update_thread, NULL);
    pthread_join(Read_thread, NULL);

    pthread_mutex_destroy(&Shared_Mutex);
    sem_destroy(&Read_Data_Sem);
    sem_destroy(&Update_Data_Sem);

    printf("Completed main, cleaning...\n");
    return 0;
}