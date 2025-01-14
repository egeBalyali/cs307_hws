#ifndef WBQ_H
#define WBQ_H
#include <math.h>
// Structs and methods for WorkBalancerQueue, you can use additional structs 
// and data structures ON TOP OF the ones provided here.

// **********************************************************
extern int flag_array[];
typedef struct WorkBalancerQueue WorkBalancerQueue;
typedef struct QueueNode QueueNode;
typedef struct ThreadArguments {
    WorkBalancerQueue* q;
    int id;
} ThreadArguments;

typedef struct Task {
    char* task_id;
    int task_duration;
	double cache_warmed_up;
	WorkBalancerQueue* owner; //TODO change this in fetchother
} Task;
//a node has the next and the task
struct QueueNode{
    QueueNode* next;
    QueueNode* prev;
    Task* task;
};
// TODO: You can modify this struct and add any 
// fields you may need
struct WorkBalancerQueue {
    QueueNode* head;
    QueueNode* tail;
    pthread_mutex_t head_lock,tail_lock;
    int task_count;
};



// **********************************************************

// WorkBalancerQueue API **********************************************************
void submitTask(WorkBalancerQueue* q, Task* _task);
Task* fetchTask(WorkBalancerQueue* q);

Task* fetchTaskFromOthers(WorkBalancerQueue* q);
// You can add more methods to Queue API
// .
// . 
// **********************************************************
void Queue_Init(WorkBalancerQueue* q);
void Queue_Enqueue(WorkBalancerQueue* q, Task* task);
int Queue_Dequeue(WorkBalancerQueue* q, Task* task);

// Your simulator threads should call this function to simulate execution. 
// Don't change the function signature, you can use the provided implementation of 
// this function. We will use potentially different implementations while testing.
void executeJob(Task* task, WorkBalancerQueue* my_queue, int my_id );
int Queue_Steal_From_Back(WorkBalancerQueue* q, Task* task);
void* processJobs(void* arg);
void initSharedVariables();
void print_queue(WorkBalancerQueue* q);
#endif