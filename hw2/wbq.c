#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include <assert.h>
#include "constants.h"
#include "wbq.h"

// Do your WorkBalancerQueue implementation here. 
// Implement the 3 given methods here. You can add 
// more methods as you see necessary.

// Thismethod adds a new job to the head end of the queue and can be only called by the owner thread.


void submitTask(WorkBalancerQueue* q, Task* _task) {
    // Allocate a new node
    Queue_Enqueue(q, _task); //added to the queue
    q->task_count += ceil(_task->task_duration / CYCLE); //updated the queue task count
    //printf("Submitting task with id %s", _task->task_id);
}

// This method removes
// the next available job from the tail end of the queue and can be only
// called by the owner thread. When the WBQ is empty it returns NULL.
Task* fetchTask(WorkBalancerQueue* q) {
	// TODO: Implement fetchTask
    // .
    // .
    Task* tmp_task = malloc(sizeof(Task));
    if (Queue_Dequeue(q, tmp_task) == -1)
    {
        //queue was empty
        free(tmp_task);
        return NULL;
    }
    
    q->task_count -= ceil(tmp_task->task_duration / CYCLE);
    
    return tmp_task;
}

Task* fetchTaskFromOthers(WorkBalancerQueue* q) {
    // Lock the mutex before accessing the queue
    Task* tmp_task = malloc(sizeof(Task));
    if (Queue_Steal_From_Back(q, tmp_task) == -1)
    {
        //queue was empty
        free(tmp_task);
        return NULL;
    }
    q->task_count -= ceil(tmp_task->task_duration / CYCLE);
    return tmp_task;
}
void print_queue(WorkBalancerQueue* q)
{
    QueueNode *tmp = q->head->next;
    while(tmp != NULL)
    {
        printf("The task is %s and it has %d time remaining -> ", tmp->task->task_id, tmp->task->task_duration);
        tmp = tmp->next;
    }
    printf("\n");

}
//creatte the dummy node and initialize locks
void Queue_Init(WorkBalancerQueue* q)
{
    QueueNode *tmp = malloc(sizeof(QueueNode));
    assert(tmp != NULL);
    tmp->next = NULL;
    tmp->prev = NULL;
    q->head = q->tail = tmp;
    pthread_mutex_init(&q->head_lock, NULL);
    pthread_mutex_init(&q->tail_lock, NULL);
}
void Queue_Enqueue(WorkBalancerQueue* q, Task* task)
{
    QueueNode *tmp = malloc(sizeof(QueueNode));
    assert(tmp != NULL);
    tmp->task = task;
    tmp->next = NULL;
    
    pthread_mutex_lock(&q->tail_lock);
    tmp->prev = q->tail;
    q->tail->next = tmp;
    q->tail = tmp;
    pthread_mutex_unlock(&q->tail_lock);
}
int Queue_Dequeue(WorkBalancerQueue* q, Task* task)
{
    pthread_mutex_lock(&q->head_lock);
    QueueNode *tmp = q->head;
    QueueNode *new_head = tmp->next; //since first head is dummy acctual value is head->hnext
    if (new_head == NULL)
    {
        pthread_mutex_unlock(&q->head_lock);
        return -1;//empty queue
    }
    *task = *(new_head->task);
    q->head = new_head;
    new_head->prev = NULL;
    pthread_mutex_unlock(&q->head_lock);
    //printf("fetcing task with id:%s \n", task->task_id);
    free(tmp);
    return 0;
}
int Queue_Steal_From_Back(WorkBalancerQueue* q, Task* task)
{
    pthread_mutex_lock(&q->tail_lock);
    QueueNode* tmp = q->tail;
    QueueNode* new_tail = tmp->prev;
    new_tail->next = NULL;
    if (new_tail == NULL)
    {
        pthread_mutex_unlock(&q->tail_lock);
        return -1;//empty queue
    }
    *task = *(tmp->task);
    q->tail = new_tail;
    pthread_mutex_unlock(&q->tail_lock);
    free(tmp);
    return 0;
}