/*
 * channel.c
 *
 *  Created on: Sep 22, 2024
 *      Author: HP
 */
#include "channel.h"
#include <kern/proc/user_environment.h>
#include <kern/cpu/sched.h>
#include <inc/string.h>
#include <inc/disk.h>

//===============================
// 1) INITIALIZE THE CHANNEL:
//===============================
// initialize its lock & queue
void init_channel(struct Channel *chan, char *name)
{
	strcpy(chan->name, name);
	init_queue(&(chan->queue));
}

//===============================
// 2) SLEEP ON A GIVEN CHANNEL:
//===============================
// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
// Ref: xv6-x86 OS code
void sleep(struct Channel *chan, struct kspinlock* lk)
{
    //TODO: [PROJECT'25.IM#5] KERNEL PROTECTION: #1 CHANNEL - sleep [Done]
    //Your code is here
    //Comment the following line
    //panic("sleep() is not implemented yet...!!");


    release_kspinlock(lk);    //release lk before blocking the current process

    //block process and add to blocked queue
    acquire_kspinlock(&ProcessQueues.qlock);
    struct Env *curr_env = get_cpu_proc();
    curr_env->channel = chan;
    curr_env->env_status = ENV_BLOCKED;
    enqueue(&(chan->queue), curr_env);

    //schedule next ready process, and release the qlock
    sched();
    curr_env->channel = NULL;
    release_kspinlock(&ProcessQueues.qlock);

    //reacquire lk on awake
    acquire_kspinlock(lk);
}

//==================================================
// 3) WAKEUP ONE BLOCKED PROCESS ON A GIVEN CHANNEL:
//==================================================
// Wake up ONE process sleeping on chan.
// The qlock must be held.
// Ref: xv6-x86 OS code
// chan MUST be of type "struct Env_Queue" to hold the blocked processes
void wakeup_one(struct Channel *chan)
{
    //TODO: [PROJECT'25.IM#5] KERNEL PROTECTION: #2 CHANNEL - wakeup_one [Done]
    //Your code is here
    //Comment the following line
    //panic("wakeup_one() is not implemented yet...!!");

    acquire_kspinlock(&ProcessQueues.qlock);

    //dequeue a process, and schedule it to ready queue
    struct Env *curr_env = dequeue(&chan->queue);
    if(curr_env != NULL)
    {
        sched_insert_ready(curr_env);
    }
    release_kspinlock(&ProcessQueues.qlock);
}

//====================================================
// 4) WAKEUP ALL BLOCKED PROCESSES ON A GIVEN CHANNEL:
//====================================================
// Wake up all processes sleeping on chan.
// The queues lock must be held.
// Ref: xv6-x86 OS code
// chan MUST be of type "struct Env_Queue" to hold the blocked processes

void wakeup_all(struct Channel *chan)
{
    //TODO: [PROJECT'25.IM#5] KERNEL PROTECTION: #3 CHANNEL - wakeup_all [Done]
    //Your code is here
    //Comment the following line
    //panic("wakeup_all() is not implemented yet...!!");

    acquire_kspinlock(&ProcessQueues.qlock);
    struct Env *p = NULL;
    while ((p = dequeue(&chan->queue)) != NULL)
    {
        p->channel = NULL;
        sched_insert_ready(p);
    }
    release_kspinlock(&ProcessQueues.qlock);
}

