/*
 * kos.c -- starting point for student's os.
 * 
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "simulator_lab2.h"
#include "scheduler.h"
#include "kos.h"

Dllist readyq;

JRB pidtree;
pid_t curpid;

kt_sem writeok;
kt_sem writers;
kt_sem readers;
kt_sem nelem;
kt_sem nslots;
kt_sem console_wait;
int* console_read_buffer;
int queue_head;
int queue_tail;
int used_memory[8];
int memory_bases[8];
int pipe_count;

struct PCB* init;

void init_cull() {
	while (1) {
		P_kt_sem(init->waiter_sem);
		struct PCB* childPCB = (struct PCB*) (dll_val(dll_first(init->waiters))).v;
		DEBUG('e', "init is freeing pid %d\n", childPCB->pid);
		destroy_pid(childPCB->pid);
		free(childPCB->registers);
		free(childPCB);
		dll_delete_node(dll_first(init->waiters));
	}
}

void initialize_user_process(void *args) {

	// zero out memory
	bzero(main_memory, MemorySize);

	// load file
	char** filename = ((char**) args)[0];

	// initialize PCB
	struct PCB* PCB = malloc(sizeof(struct PCB));
	PCB->registers = malloc(sizeof(int) * NumTotalRegs);
	PCB->base = User_Base;
	PCB->limit = User_Limit;
	PCB->pid = get_new_pid();
	PCB->parent = init;
	PCB->waiter_sem = make_kt_sem(0);
	PCB->waiters = new_dllist();
	PCB->children = make_jrb();

	// set file descriptors
	PCB->fd[0] = 1; // stdin
	PCB->fd_isconsole[0] = 1;
	PCB->fd_readwrite[0] = 0;
	struct pipe* pipe1 = malloc(sizeof(struct pipe));
	PCB->pipes[0] = pipe1;
	pipe1->read_ref = 1;
	pipe1->write_ref = 0;

	PCB->fd[1] = 1; // stdout
	PCB->fd_isconsole[1] = 1;
	PCB->fd_readwrite[1] = 1;
	struct pipe* pipe2 = malloc(sizeof(struct pipe));
	PCB->pipes[1] = pipe2;
	pipe2->read_ref = 0;
	pipe2->write_ref = 1;

	PCB->fd[2] = 1; // stderr
	PCB->fd_isconsole[2] = 1;
	PCB->fd_readwrite[2] = 1;
	struct pipe* pipe3 = malloc(sizeof(struct pipe));
	PCB->pipes[2] = pipe3;
	pipe3->read_ref = 0;
	pipe3->write_ref = 1;

	// put PCB on init's children tree
	Jval initPayload;
    initPayload.v = (void*) PCB;
    jrb_insert_int(init->children, PCB->pid, initPayload);

	// hold local args
	char *local_argv[256];

	// copy args into local args
	int i = 0;
	while (kos_argv[i] != NULL) {
		local_argv[i] = kos_argv[i];
		i++;
	}

	// execv call
	int exec_result = perform_execve(PCB, filename, local_argv);
	if (exec_result < 0) {
		DEBUG('e', "ERROR!\n");
		exit(exec_result);
	}

	// memory no longer available
	used_memory[0] = 1;

	// add PCB to queue
	Jval payload;
	payload.v = (void*) PCB;
	dll_append(readyq, payload);

	kt_exit();
}

int perform_execve(struct PCB* job, char* fn, char** argv) {

	// validate inputs
	if (!fn || !argv || !job) {
		return -1;
	}

	int break_pointer = load_user_program(fn);

	if (break_pointer < 0) {
		DEBUG('j', stderr,"%d can't load program.\n");
		return -1;
	}

	// zero out all my registers
    for (int i = 0; i < 40; i++) {
        (job->registers)[i] = 0; 
    }

    // reset specific register values
    (job->registers)[PCReg] = 0;
    (job->registers)[NextPCReg] = 4;
    (job->registers)[StackReg] = job->limit - 12;

	job->brk_pointer = break_pointer;

	int* user_args = MoveArgsToStack(job->registers, argv, job->base);
	InitCRuntime(user_args, job->registers, argv, job->base);
	
	return 0;
}

void KOS()
{

	User_Base = 0;
	User_Limit = MemorySize / 8;
	
	// program start constants
	for (int i = 0; i < 8; ++i) {
        used_memory[i] = 0;
    }
	for (int i = 0; i < 8; ++i) {
        memory_bases[i] = i * (MemorySize / 8);
    }

	// create ready queue
	readyq = new_dllist();

	// create pid tree and curpid
	pidtree = make_jrb();
	curpid = 0;

	// create write semaphores
	writers = make_kt_sem(1);
	writeok = make_kt_sem(0);

	// create read semaphores
	readers = make_kt_sem(1);
	console_wait = make_kt_sem(0);
	nslots = make_kt_sem(256);
	nelem = make_kt_sem(0);

	// create console reader
	console_read_buffer = malloc(sizeof(int) * 256);
	queue_head = 0;
	queue_tail = 0;

	// create fake "sentinel" PCB, this is init
	init = malloc(sizeof(struct PCB));
	init->pid = 0;
	init->waiter_sem = make_kt_sem(0);
	init->waiters = new_dllist();
	init->children = make_jrb();
	
	// start
	kt_fork(initialize_user_process, kos_argv);
	kt_fork(read_buffer, NULL);
	kt_fork(init_cull, NULL);

	kt_joinall();
	scheduler();
}