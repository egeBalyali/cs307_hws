#include <stdio.h>
#include <stdlib.h> 
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
#include "constants.h"
#include "wbq.h"

extern int stop_threads;
extern int finished_jobs[NUM_CORES];
int flag_array[NUM_CORES]; //I guess it has the flags of overburdened threaeds
extern WorkBalancerQueue** processor_queues; //this will have every threads stuff
enum {NORMAL, HEAVY_BURDEN};
// Thread function for each core simulator thread
void* processJobs(void* arg) {
    // initalize local variables
    ThreadArguments* my_arg = (ThreadArguments*) arg; //id and a workbalancer
    WorkBalancerQueue* my_queue = my_arg -> q;
    int my_id = my_arg -> id;
    //printf("Situation of queue %d is:", my_id);
    //print_queue(my_queue);
    // Main loop, each iteration simulates the processor getting The stop_threads flag
    // is set by the main thread when it concludes all jobs are finished. The bookkeeping 
    // of finished jobs is done in executeJob's example implementation. a task from 
    // its or another processor's queue. After getting a task to execute the thread 
    // should call executeJob to simulate its execution. It is okay if the thread
    //  does busy waiting when its queue is empty and no other job is available
    // outside.
    while (!stop_threads) {
        Task* task;
        int task_set = 0;
        //printf("my task count is: %d\n", my_queue->task_count);
        if (my_queue->task_count > 20)
        {
            flag_array[my_id] = HEAVY_BURDEN;
        }
        else
        {
            flag_array[my_id] = NORMAL;
        }
        if (my_queue->task_count < 10)
        {
            //look for someone who needs help
            for(int i = 0; i < NUM_CORES; i++)
            {
                if (flag_array[i] == HEAVY_BURDEN && processor_queues[i]->head != NULL) {
                    //printf("fetching from other\n");
                    flag_array[i] == NORMAL;
                    task = fetchTaskFromOthers(processor_queues[i]);
                    my_queue->task_count += ceil(task->task_duration / CYCLE);
                    task_set = 1;
                    break;
                }
            }
        }
        if (!task_set)
        {
            task = fetchTask(processor_queues[my_id]);
        }
        if (task == NULL)
        {
            usleep(2000);
            continue;
        }
        else 
        {
            executeJob(task, my_queue, my_id);
            if (task->task_duration != 0)
                submitTask(my_queue,task);
        }
    }
    //printf("Processor %d: Exiting\n", my_id);
    free(arg); // Clean up thread arguments
    pthread_exit(NULL);
}

// Do any initialization of your shared variables here.
// For example initialization of your queues, any data structures 
// you will use for synchronization etc.
void initSharedVariables() {
    for (int i = 0; i < NUM_CORES; i++)
    {
        WorkBalancerQueue* q = processor_queues[i];
        
        Queue_Init(q);
        flag_array[i] = NORMAL;
    }

}