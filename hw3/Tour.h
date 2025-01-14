#ifndef TOUR_H
#define TOUR_H
#include <semaphore.h>

#include <iostream>
#include <vector>
#include <unistd.h>
#include <mutex>
#include <memory>
#include <cassert>
using namespace std;

//thoughts:
//1) read-write lock can be the solution


mutex m_person_inside;
class Tour {
private:
    int group_size, people_inside, tour_guide_needed,tour_guide_present, tour_happened;
    pthread_t guide_thread; // Thread for the guide
    sem_t s_open_spot;
    sem_t s_tour_in_progress;
    sem_t s_guide_must_leave;
    sem_t s_last_message_of_guide;
public:
    Tour(int groupSize, int tour_guide_needed)
        : group_size(groupSize), people_inside(0), tour_guide_needed(tour_guide_needed), tour_guide_present(0), tour_happened(0){
        // Input validation
        if (group_size <= 0) {
            throw invalid_argument("An error occurred.");
        }
        if (tour_guide_needed != 0 && tour_guide_needed != 1) {
            throw invalid_argument("An error occurred.");
        }
        if (tour_guide_needed)
        {
            group_size++;
        }
        sem_init(&s_open_spot, 0, group_size);
        sem_init(&s_last_message_of_guide, 0, 0);
        sem_init(&s_tour_in_progress, 0, 1);
        sem_init(&s_guide_must_leave, 0, 0);
    }
    ~Tour(){
        sem_destroy(&s_open_spot);
        sem_destroy(&s_last_message_of_guide);
        sem_destroy(&s_tour_in_progress);
        sem_destroy(&s_guide_must_leave);
    }
    void start();
    int arrive(){
        //first they arrive
        printf("Thread ID: %ld | Status: Arrived at the location.\n", pthread_self());
        //check the num of people inside
        
        //open spot has group size count, until that many people come nobody will wait here
        sem_wait(&s_open_spot);
        //if a tour is in progress no new people will be able to enter, this is a binary semaphore
        sem_wait(&s_tour_in_progress);
        //this mutex is for when I need to change values of integers used as flags.
        m_person_inside.lock();
        people_inside++;
        m_person_inside.unlock(); //though since s_tour_in_progress is a binary semaphore there is no race here
        if (people_inside == group_size) //this is only true for the last guy
        {
            //if tour has enough people
            tour_happened = 1;
            if (tour_guide_needed)
            {
                guide_thread = pthread_self();
                tour_guide_present = 1;
            }

            printf("Thread ID: %ld | Status: There are enough visitors, the tour is starting.\n", pthread_self());
             
            
            return 1;
        }
        //if a tourist isn't last they come here
        printf("Thread ID: %ld | Status: Only %d visitors inside, starting solo shots.\n", pthread_self(), people_inside);
        //there were no tour happening so release that
        sem_post(&s_tour_in_progress);
        //since I can't interfere with main return is meaningless, mostly useful for finishing the func
        return 1;
    }

    int leave()
    {
        //if a tour guide exists there was a tour and a guide
        if(tour_guide_present)
        {
            //check if the thread is tour guide, if not wait. Since this sem is zero everyone will wait here
            if (!pthread_equal(guide_thread, pthread_self()))
                sem_wait(&s_guide_must_leave); //initialized as zero so no non-guide thread can pass
            else{ //guide will set the semaphore to one and we can let them in one by one
                printf("Thread ID: %ld | Status: Tour guide speaking, the tour is over.\n", pthread_self());
                m_person_inside.lock();
                people_inside--;
                m_person_inside.unlock();
                sem_post(&s_open_spot); //relinquish their open spot
                tour_guide_present = 0;
                sem_post(&s_guide_must_leave); //let others through
                sem_wait(&s_last_message_of_guide);  //output formatting, all samples make the last thread the guide
                printf("Thread ID: %ld | Status: All visitors have left, the new visitors can come.\n", pthread_self());
                tour_happened = 0;
                sem_post(&s_tour_in_progress); //let others in
                return 1; //guide thread ends
            }
            m_person_inside.lock();
            people_inside--;
            m_person_inside.unlock();
            sem_post(&s_open_spot); //non-guide threads relinquish their open spot, no one can start yet though
            assert(people_inside >= 0); //for debugging, if somehow people number go negative.
            if (people_inside > 0){
                printf("Thread ID: %ld | Status: I am a visitor and I am leaving.\n", pthread_self());
                sem_post(&s_guide_must_leave); //this is to let others waiting in this semaphore in
            }
            else{ //the last thread to call leave
                printf("Thread ID: %ld | Status: I am a visitor and I am leaving.\n", pthread_self());
                sem_post(&s_last_message_of_guide);
            }
        }
        
        else{  //if there wasn't a guide maybe tour happened without a guide or just didn't happen
            if (tour_happened == 0) {
                printf("Thread ID: %ld | Status: My camera ran out of memory while waiting, I am leaving.\n", pthread_self());
                m_person_inside.lock();
                people_inside--;
                m_person_inside.unlock();
                sem_post(&s_open_spot);  //relinquishing spot
            }
            else{//there was a tour but no guide
                m_person_inside.lock();
                people_inside--;
                m_person_inside.unlock();
                sem_post(&s_open_spot);
                assert(people_inside >= 0);
                if (people_inside > 0)
                {
                    printf("Thread ID: %ld | Status: I am a visitor and I am leaving.\n", pthread_self());
                }
                else
                {
                    printf("Thread ID: %ld | Status: I am a visitor and I am leaving.\n", pthread_self());
                    printf("Thread ID: %ld | Status: All visitors have left, the new visitors can come.\n", pthread_self());
                    tour_happened = 0;
                    sem_post(&s_tour_in_progress); //let others in
                }              
            }
        }
        return 1;
    }
};


#endif //TOUR_H