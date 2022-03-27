/* This file will contain your solution. Modify it as you wish. */
#include <types.h>
#include <lib.h>
#include <synch.h>
#include <kern/errno.h>
#include "client_server.h"

/*
 * Declare any variables you need here to implement and
 *  synchronise your queues and/or requests.
 */

struct queue {
        request_t *item;
        struct queue *next;
};

struct queue *start, *end;
int queue_num;

struct semaphore *full, *mutex;

/* work_queue_enqueue():
 *
 * req: A pointer to a request to be processed. You can assume it is
 * a valid pointer or NULL. You can't assume anything about what it
 * points to, i.e. the internals of the request type.
 *
 * This function is expected to add requests to a single queue for
 * processing. The queue is a queue (FIFO). The function then returns
 * to the caller. It can be called concurrently by multiple threads.
 *
 * Note: The above is a high-level description of behaviour, not
 * detailed psuedo code. Depending on your implementation, more or
 * less code may be required. 
 */

   

void work_queue_enqueue(request_t *req)
{
        (void) req; /* Avoid compiler error */
        P(mutex);

        struct queue *new = kmalloc(sizeof(struct queue));
        if (new == NULL){
                panic("queue memory allocation failed");
        }
        new->item = req;
        new->next = NULL;

        if (queue_num == 0){
                start = end = new;
        }
        else{
                end->next = new;
                end = end->next;
        }
        queue_num++;
        
        V(mutex);
        V(full);
}

/* 
 * work_queue_get_next():
 *
 * This function is expected to block on a synchronisation primitive
 * until there are one or more requests in the queue for processing.
 *
 * A pointer to the request is removed from the queue and returned to
 * the server.
 * 
 * Note: The above is a high-level description of behaviour, not
 * detailed psuedo code. Depending on your implementation, more or
 * less code may be required.
 */


request_t *work_queue_get_next(void)
{
        P(full);
        P(mutex);

        request_t *out = start->item;

        if (queue_num==1){
                kfree(start);
                start = end = NULL;
        }
        else{
                struct queue *next = start->next;
                kfree(start);
                start = next;
        }
        queue_num--;

        V(mutex);
        return out;
}




/*
 * work_queue_setup():
 * 
 * This function is called before the client and server threads are started. It is
 * intended for you to initialise any globals or synchronisation
 * primitives that are needed by your solution.
 *
 * In returns zero on success, or non-zero on failure.
 *
 * You can assume it is not called concurrently.
 */

int work_queue_setup(void)
{
        full = sem_create("full", 0);
        mutex = sem_create("mutex", 1);
        if (full == NULL || mutex == NULL) 
                return ENOMEM;
        start = NULL, end = NULL;
        queue_num = 0;
        return 0;
}


/* 
 * work_queue_shutdown():
 * 
 * This function is called after the participating threads have
 * exited. Use it to de-allocate or "destroy" anything allocated or created
 * on setup.
 *
 * You can assume it is not called concurrently.
 */

void work_queue_shutdown(void)
{
        sem_destroy(full);
        sem_destroy(mutex);
        KASSERT(queue_num == 0);
        KASSERT(start == NULL && end == NULL);
}
