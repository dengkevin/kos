/*
 * exception.c -- stub to handle user mode exceptions, including system calls
 * 
 * Everything else core dumps.
 * 
 */
#include <stdlib.h>

#include "scheduler.h"
#include "kos.h"
#include "simulator_lab2.h"
#include "dllist.h"
#include "syscall.h"

void printConsoleReadBuffer() {
	for (int i = 0; i != kt_getval(nelem); i++) {
		int index = (i + queue_head) % 256;
		printf("%c ",console_read_buffer[index]);
	}
	printf("\n");
	printf("nelem sema value: 	 %d\n", kt_getval(nelem));
	printf("nslots sema value:	 %d\n", kt_getval(nslots));
	printf("head of queue is at: %d\n", queue_head);
	printf("tail of queue is at: %d\n", queue_tail);
}

void
exceptionHandler(ExceptionType which)
{
	
	examine_registers(global_pcb->registers);
	int type = global_pcb->registers[4];

	/*
	 * for system calls type is in r4, arg1 is in r5, arg2 is in r6, and
	 * arg3 is in r7 put result in r2 and don't forget to increment the
	 * pc before returning!
	 */

	DEBUG('e', "pid: %d --- ", global_pcb->pid);

	switch (which) {
	case SyscallException:
		/* the numbers for system calls is in <sys/syscall.h> */
		switch (type) {
		case 0:
			/* 0 is our halt system call number */
			DEBUG('e', "Halt initiated by user program\n");
			free(global_pcb->registers);
			free(global_pcb);
			SYSHalt();
		case SYS_exit:
			DEBUG('e', "exit system call\n");
			// printf("Program %d exited with value %d.\n", global_pcb->pid, r5);
			kt_fork(do_exit, global_pcb);
			// printf("process status: ");
			// for (int i = 0; i < 8; i++) {
			// 	printf("%d ",used_memory[i]);
			// }
			break;
		case SYS_write:
			DEBUG('e', "write system call from id %d\n", global_pcb->pid);
			kt_fork(do_write, global_pcb);
			break;
		case SYS_read:
			DEBUG('e', "read system call\n");
			kt_fork(do_read, global_pcb);
			break;
		case SYS_ioctl:
			DEBUG('e', "ioctl system call\n");
			kt_fork(do_ioctl, global_pcb);
			break;
		case SYS_fstat:
			DEBUG('e', "fstat system call\n");
			kt_fork(do_fstat, global_pcb);
			break;
		case SYS_getpagesize:
			DEBUG('e', "getpagesize system call\n");
			kt_fork(do_getpagesize, global_pcb);
			break;
		case SYS_sbrk:
			DEBUG('e', "sbrk system call\n");
			kt_fork(do_sbrk, global_pcb);
			break;
		case SYS_execve:
			DEBUG('e', "execve system call\n");
			kt_fork(do_execve, global_pcb);
			break;
		case SYS_getpid:
			DEBUG('e', "getpid system call\n");
			kt_fork(do_getpid, global_pcb);
			break;
		case SYS_fork:
			DEBUG('e', "fork system call\n");
			kt_fork(do_fork, global_pcb);
			break;
		case SYS_getdtablesize:
			DEBUG('e', "getdtablesize system call\n");
			kt_fork(do_getdtablesize, global_pcb);
			break;
		case SYS_close:
			DEBUG('e', "close system call\n");
			kt_fork(do_close, global_pcb);
			break;
		case SYS_wait:
			DEBUG('f', "pid: %d --- wait system call\n", global_pcb->pid);
			kt_fork(do_wait, global_pcb);
			break;
		case SYS_getppid:
			DEBUG('e', "getppid system call\n");
			kt_fork(do_getppid, global_pcb);
			break;
		case SYS_pipe:
			DEBUG('e', "pipe system call\n");
			kt_fork(do_pipe, global_pcb);
			break;
		case SYS_dup:
			DEBUG('e', "dup system call\n");
			kt_fork(do_dup, global_pcb);
			break;
		case SYS_dup2:
			DEBUG('e', "dup2 system call\n");
			kt_fork(do_dup2, global_pcb);
			break;
		default:
			DEBUG('e', "Unknown system call was %d\n",type);
			SYSHalt();
			break;
		}
		break;
	case PageFaultException:
		DEBUG('e', "Exception PageFaultException\n");
		break;
	case BusErrorException:
		DEBUG('e', "Exception BusErrorException\n");
		break;
	case AddressErrorException:
		DEBUG('e', "Exception AddressErrorException\n");
		break;
	case OverflowException:
		DEBUG('e', "Exception OverflowException\n");
		break;
	case IllegalInstrException:
		DEBUG('e', "Exception IllegalInstrException\n");
		break;
	default:
		printf("Unexpected user mode exception %d %d\n", which, type);
		exit(1);
	}
	
	kt_joinall();
	scheduler();
}

void
interruptHandler(IntType which)
{

	if (global_pcb != NULL) {
		// only add to queue if we are NOT in noop
		examine_registers(global_pcb->registers);
		Jval payload;
		payload.v = (void*) global_pcb;
		dll_append(readyq, payload);
	}
	
	switch (which) {
	case ConsoleReadInt:
		// DEBUG('e', "ConsoleReadInt interrupt\n");
		V_kt_sem(console_wait);
		break;
	case ConsoleWriteInt:
		// DEBUG('e', "ConsoleWriteInt interrupt\n");
		V_kt_sem(writeok);
		break;
	case TimerInt:
		break;
	default:
		DEBUG('e', "Unknown interrupt\n");
		break;
	}

	kt_joinall();
	scheduler();
}