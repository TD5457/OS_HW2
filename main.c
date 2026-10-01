
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>

#define NUM_ACQUIRES 10000  // Number of lock attempts
#define NUM_TRIALS   5     // Number of trials
#define CS_WORK 100         // Work inside the lock
#define OUTSIDE_WORK 200    // Work outside the lock

// more threads = more contention
int thread_counts[] = {1, 2,4, 8,16};
#define NUM_COUNTS 5

// adds 1 and returns the old value in one atomic step, same as the book
int FetchAndAdd(atomic_int *ptr) {
    return atomic_fetch_add(ptr,1);
}

// if *ptr equals expected set it to new, returns the old value
int CompareAndSwap(atomic_int *ptr, int expected, int new) {
    atomic_compare_exchange_strong(ptr, &expected,new);
    return expected;
}

// ticket = next number to hand out, turn = whose turn it is now
typedef struct {
    atomic_int ticket;
atomic_int turn;
}ticket_lock_t;

void ticket_init(ticket_lock_t *lock) {
    lock->ticket = 0;
    lock->turn=0;
}

// Take a ticket and wait until it is this thread's turn
void ticket_lock(ticket_lock_t *lock) {
    int myturn=FetchAndAdd(&lock->ticket);

    while (lock->turn != myturn)
        ;
}

// let the next ticket in
void ticket_unlock(ticket_lock_t *lock) {
 FetchAndAdd(&lock->turn);
}

// flag 0 = free, 1 = taken
typedef struct {
    atomic_int flag;
} cas_lock_t;

void cas_init(cas_lock_t *lock) {
    lock->flag = 0;
}

// Keep trying until the lock is free
void cas_lock(cas_lock_t *lock) {
    while (CompareAndSwap(&lock->flag,0,1) == 1);
}

void cas_unlock(cas_lock_t *lock) {
    lock->flag=0;
}

// shared stuff, counter is what the lock protects
ticket_lock_t tlock;
cas_lock_t clock_lock;
int use_ticket;
int num_threads;
atomic_int ready;
int counter;
long *waits;

// current time in nanoseconds
long now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec*1000000000L + ts.tv_nsec;
}

// fake work so threads act like a real program
void work(int n) {
    volatile int sum=0;

    for (int i = 0; i<n; i++)
        sum += i;
}

// each thread runs this
void *worker(void *arg) {
    long id = (long)arg;

    // Wait for all threads to start
    while (ready==0)
        ;

    for (int i=0; i<NUM_ACQUIRES; i++) {
        long t0 = now_ns();

        // Get the lock
        if (use_ticket)
            ticket_lock(&tlock);
        else
            cas_lock(&clock_lock);

        // Save how long the lock took
        waits[id * NUM_ACQUIRES+i] = now_ns()-t0;

        // critical section
        counter++;
        work(CS_WORK);

        if (use_ticket)
            ticket_unlock(&tlock);
        else
            cas_unlock(&clock_lock);

        work(OUTSIDE_WORK);
    }
    return NULL;
}

// one trial: make the threads, start them, wait for them
void run_trial() {
    pthread_t threads[32];
    ticket_init(&tlock);
    cas_init(&clock_lock);
    counter=0;
    ready=0;

    // Start the threads
    for (long i=0; i<num_threads; i++)
        pthread_create(&threads[i],NULL,worker,(void *)i);

    ready = 1;

    for (int i=0; i<num_threads;i++)
        pthread_join(threads[i],NULL);

    // lock works if no increments got lost
    if (counter != num_threads*NUM_ACQUIRES) {
        printf("ERROR: lock failed, counter = %d\n",counter);
        exit(1);
    }
}

// for qsort, smallest to largest
int compare(const void *a,const void *b) {
    long x=*(long *)a;
    long y = *(long *)b;
    return (x > y)-(x < y);
}

int main() {
    printf("lock\tthreads\tavg\tmedian\tp99\tmax\n");

    for (int c=0;c<NUM_COUNTS;c++) {
        num_threads=thread_counts[c];
        int total=num_threads*NUM_ACQUIRES;
        waits=malloc(total*sizeof(long));

        // Run both lock types
        for (use_ticket=1;use_ticket>=0;use_ticket--) {
            double avg=0,median=0,p99=0,max=0;

            for (int trial=0;trial<NUM_TRIALS;trial++) {
                run_trial();
                // sort so we can get median, p99 and max
                qsort(waits,total,sizeof(long),compare);

                long sum=0;
                for (int i=0;i<total;i++)
                    sum += waits[i];

                avg += (double)sum/total;
                median += waits[total/2];
                p99 += waits[total*99/100];
                max += waits[total-1];
            }

            printf("%s\t%d\t%.0f\t%.0f\t%.0f\t%.0f\n",
                   use_ticket ? "ticket":"cas", num_threads,
                   avg/NUM_TRIALS, median/NUM_TRIALS,
                   p99/NUM_TRIALS, max/NUM_TRIALS);
        }

        printf("\n");
        free(waits);
    }
    return 0;
}