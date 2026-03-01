#include <sched.h>
#include <stdio.h>
#include <syslog.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <unistd.h>

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
    double direction; // in degrees
    struct timespec time;
} dataStructure;
dataStructure sharedData;
struct sched_param Main_param, Read_param, Update_param;
sem_t Read_Data_Sem, Update_Data_Sem, Shared_Sem;

// globals
int runPrograms = 1;
pthread_t Read_thread, Update_thread;
pthread_attr_t Main_attr, Read_attr, Update_attr;
static int errorHandler_main, errorHandler_read, errorHandler_update;

//
// Functions
//

static inline double getDelta(struct timespec *start, struct timespec *stop)
{
    return stop->tv_sec - start->tv_sec + (double)(stop->tv_nsec - start->tv_nsec) / NSEC_PER_SEC;
}

void *safe_read(void *thread_id)
{
    sem_wait(&Read_Data_Sem); // until we want to run read again
    while (runPrograms)
    {
        // get the time
        static struct timespec startTime = {0, 0};
        static struct timespec stopTime = {0, 0};
        clock_gettime(CLOCK_MONOTONIC, &startTime);

        // wait to read until we are allowed by the write
        sem_wait(&Shared_Sem);
        // read data
        printf("Read data: position=%.2f, velocity=%.2f, acceleration=%.2f, direction=%.2f\n", sharedData.position, sharedData.velocity, sharedData.acceleration, sharedData.direction);
        // let write do it's thing
        sem_post(&Shared_Sem);

        // give the time elapsed
        clock_gettime(CLOCK_MONOTONIC, &stopTime);
        double delta = getDelta(&startTime, &stopTime);
        printf("Read finished at: %.9lf in: %.9lf seconds\n", stopTime.tv_sec + (double)stopTime.tv_nsec / NSEC_PER_SEC, delta);
        sem_wait(&Read_Data_Sem); // until we want to run read again
    }
}

void *safe_update(void *thread_id)
{
    sem_wait(&Update_Data_Sem); // until we want to run update again
    while (runPrograms)
    {
        // get the time
        static struct timespec startTime = {0, 0};
        static struct timespec stopTime = {0, 0};
        clock_gettime(CLOCK_MONOTONIC, &startTime);

        // wait to write until we are allowed by the read
        sem_wait(&Shared_Sem);
        // update data
        sharedData.time = startTime;
        sharedData.acceleration++;
        if (sharedData.acceleration > 4)
        {
            sharedData.acceleration = -2;
        }
        sharedData.velocity += sharedData.acceleration;
        sharedData.position += sharedData.velocity;
        sharedData.direction += 30;
        if(sharedData.direction >= 360)
        {
            sharedData.direction -= 360;
        }

        // give time elapsed
        clock_gettime(CLOCK_MONOTONIC, &stopTime);
        double delta = getDelta(&startTime, &stopTime);
        printf("Update finished at: %.9lf in: %.9lf seconds\n", stopTime.tv_sec + (double)stopTime.tv_nsec / NSEC_PER_SEC, delta);
        
        //let read do it's thing
        sem_post(&Shared_Sem);
        sem_wait(&Update_Data_Sem); // until we want to run update again
    }
}

int main()
{
    // log initialization

    openlog("LOG_Exercise2_Part2", LOG_PID, LOG_USER);

    // frequnecy timespec
    struct timespec frequency = {0, NSEC_PER_100MSEC};

    // initialize semaphore

    sem_init(&Shared_Sem, 0, 1);
    sem_init(&Read_Data_Sem, 0, 0);
    sem_init(&Update_Data_Sem, 0, 0);

    // thread initialization

    pthread_attr_init(&Main_attr); // initializes the thread to default values, stored in the passed variable
    pthread_attr_init(&Read_attr);
    pthread_attr_init(&Update_attr);
    pthread_attr_setinheritsched(&Main_attr, PTHREAD_EXPLICIT_SCHED); // sets the new thread to use the schedule explicitly defined in the attribute object
    pthread_attr_setinheritsched(&Read_attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setinheritsched(&Update_attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&Main_attr, SCHED_FIFO); // Sets the priority to FIFO
    pthread_attr_setschedpolicy(&Read_attr, SCHED_FIFO);
    pthread_attr_setschedpolicy(&Update_attr, SCHED_FIFO);

    Main_param.sched_priority = MAIN_PRIORITY; // sets main to max priority
    Read_param.sched_priority = READ_PRIORITY;
    Update_param.sched_priority = UPDATE_PRIORITY;
    errorHandler_main = sched_setscheduler(getpid(), SCHED_FIFO, &Main_param); // updates the scheduler with max priority for this thread (main)
    errorHandler_read = sched_setscheduler(getpid(), SCHED_FIFO, &Read_param);
    errorHandler_update = sched_setscheduler(getpid(), SCHED_FIFO, &Update_param);

    if (errorHandler_main || errorHandler_read || errorHandler_update) // returns 0 on success, so on an error it will throw the errors & print below.
    {
        perror("sched_setschduler, try running with sudo");
        return(-1);
    }

    pthread_attr_setschedparam(&Main_attr, &Main_param); // Sets the schedule parameters
    pthread_attr_setschedparam(&Read_attr, &Read_param);
    pthread_attr_setschedparam(&Update_attr, &Update_param);

    if (errorHandler_read || errorHandler_update)
    {
        perror("pthread_create, failed to create thread");
        return(-1);
    }

    errorHandler_update = pthread_create(&Update_thread, &Update_attr, safe_update, NULL); // creates a thread with the parameters defined above
    errorHandler_read = pthread_create(&Read_thread, &Read_attr, safe_read, NULL);

    // running threads

    for (int i = 0; i < 18; i++)
    {
        sem_post(&Update_Data_Sem);
        sem_post(&Read_Data_Sem); // 1
        nanosleep(&frequency, NULL);
        sem_post(&Read_Data_Sem); // 2
        nanosleep(&frequency, NULL);
        sem_post(&Read_Data_Sem); // 3
        nanosleep(&frequency, NULL);
        sem_post(&Read_Data_Sem); // 4
        nanosleep(&frequency, NULL);
        sem_post(&Read_Data_Sem); // 5
        nanosleep(&frequency, NULL);
        sem_post(&Read_Data_Sem); // 6
        nanosleep(&frequency, NULL);
        sem_post(&Read_Data_Sem); // 7
        nanosleep(&frequency, NULL);
        sem_post(&Read_Data_Sem); // 8
        nanosleep(&frequency, NULL);
        sem_post(&Read_Data_Sem); // 9
        nanosleep(&frequency, NULL);
        sem_post(&Read_Data_Sem); // 10
        nanosleep(&frequency, NULL);
    }

    // let the threads terminate

    runPrograms = 0;
    sem_post(&Update_Data_Sem);
    sem_post(&Read_Data_Sem);

    // clean up

    printf("Completed main, cleaning...\n");
    pthread_join(Update_thread, NULL);
    pthread_join(Read_thread, NULL);
    sem_destroy(&Shared_Sem);
    sem_destroy(&Read_Data_Sem);
    sem_destroy(&Update_Data_Sem);
    return 0;
}
