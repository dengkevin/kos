#ifndef SYSCALL_H
#define SYSCALL_H

void syscall_return(struct PCB* PCB, int returnValue);

void do_write(struct PCB* PCB);
void do_read(struct PCB* PCB);
void do_ioctl(struct PCB* PCB);
void do_fstat(struct PCB* PCB);
void do_getpagesize(struct PCB* PCB);
void do_sbrk(struct PCB* PCB);
void do_execve(struct PCB* PCB);
void do_getpid(struct PCB* PCB);
void do_fork(struct PCB* PCB);
void do_getdtablesize(struct PCB* PCB);
void do_exit(struct PCB* PCB);
void do_getppid(struct PCB* PCB);
void do_wait(struct PCB* PCB);
void do_pipe(struct PCB* PCB);
void do_close(struct PCB* PCB);
void do_dup(struct PCB* PCB);
void do_dup2(struct PCB* PCB);

#endif