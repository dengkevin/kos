#ifndef KOS_H
#define KOS_H

#include "dllist.h"
#include "jrb.h"
#include "kt.h"
#include "sys/types.h"
#include "memory.h"

#define TotalFDs 128
#define PipeBufferSize 8192

// PCB Struct contains int* that points to array of registers with size NumTotalRegs
struct PCB {
    int* registers;
    int brk_pointer; // represents the index in main_memory so is just an int
    int base;
    int limit;
    pid_t pid;
    struct PCB* parent;
    kt_sem waiter_sem;
    Dllist waiters;
    JRB children;
    int fd[TotalFDs];
    int fd_readwrite[TotalFDs]; // 0 for read, 1 for write
    int fd_isconsole[TotalFDs];
    struct pipe* pipes[TotalFDs];
};

// global readyq
extern Dllist readyq;

// global pointer to currently running PCB
extern struct PCB* global_pcb;

// global pointer to "Init" PCB
extern struct PCB* init;

// semaphores
extern kt_sem writers;      // ensures only one process can write at a time
extern kt_sem writeok;      // ensures every write interrupt is handled before next write

extern kt_sem readers;      // ensures only one process can read at a time
extern kt_sem nelem;
extern kt_sem nslots;
extern kt_sem console_wait;

extern int* console_read_buffer;
extern int queue_head;
extern int queue_tail;

int perform_execve(struct PCB* job, char* fn, char** argv);

// structs for pipe lab
struct pipe {
    char* buffer;
    int head;
    int tail;
    int read_ref;
    int write_ref;
    kt_sem writers;
    kt_sem readers;
    kt_sem nelem;
    kt_sem nslots;
};

extern int pipe_count;

#endif