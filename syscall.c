#include <stdlib.h>

#include "kos.h"
#include "simulator_lab2.h"
#include "dllist.h"
#include <errno.h>

bool execve_bool = 0;

/* --------------- HELPER FUNCTIONS --------------- */

int is_valid_addr(struct PCB* PCB, int address) {
    if (address >= 0 && address < PCB->limit) {
        return 1;
    }
    return 0;
}

int get_fd(struct PCB* PCB, int read_write) {
    for (int i = 0; i < TotalFDs; i++) {
        if (PCB->fd[i] == 0) {
            PCB->fd[i] = 1;
            PCB->fd_readwrite[i] = read_write;
            PCB->fd_isconsole[i] = 0;
            return i;
        }
    }
    return -1; // no fds remaining
}

int rm_fd(struct PCB* PCB, int fd) {
    if (PCB->fd[fd] == 0) {
        return -1;
    } else {
        PCB->fd[fd] = 0;
        PCB->fd_readwrite[fd] = 0;
        PCB->fd_isconsole[fd] = 0;
        return 0;
    }
}

void perform_close(struct PCB* PCB, int fd) {

    DEBUG('e', "    closing %d\n", fd);

    int read_write = PCB->fd_readwrite[fd];
    int is_console = PCB->fd_isconsole[fd];
    struct pipe* this_pipe = PCB->pipes[fd];

    // remove fd from arrays
    rm_fd(PCB, fd);

    // decrement the corresponding ref count
    if (read_write == 0) {
        this_pipe->read_ref -= 1;
    } else {
        this_pipe->write_ref -= 1;
    }

    DEBUG('e', "    file descriptor %d has refs: (read, %d), (write, %d)\n", fd, this_pipe->read_ref, this_pipe->write_ref);

    if (is_console == 0) {
        // if I am the last reader but there are extra writers, V nslots for every writer so that all writers can unblock (and exit with error)
        if (read_write == 0 && this_pipe->read_ref == 0 && this_pipe->write_ref > 0) {
            for (int i = 0; i < this_pipe->write_ref; i++) {
                V_kt_sem(this_pipe->nslots);
            }                
        }
        // if I am the last writer but there are extra readers, V nelem for every reader so that all readers can unblock (and exit with error), put an EOF on the head
        if (read_write == 1 && this_pipe->write_ref == 0 && this_pipe->read_ref > 0) {
            this_pipe->buffer[this_pipe->head] = -1;
            for (int i = 0; i < this_pipe->read_ref; i++) {
                V_kt_sem(this_pipe->nelem);
            }
        }

        // if this pipe has read/write ref count both = 0, free it
        if (this_pipe->read_ref == 0 && this_pipe->write_ref == 0) {
            DEBUG('e', "freeing this pipe\n");
            free(this_pipe->buffer);
            free(this_pipe);
        }
    }

    return 0;
}

/* ----------------- SYSTEM CALLS ----------------- */

void syscall_return(struct PCB* PCB, int returnValue) {

    if (execve_bool) {
        execve_bool = 0;
    } else {
        (PCB->registers)[PCReg] = (PCB->registers)[NextPCReg]; // set PCReg in saved registers to NextPCReg
    }

    (PCB->registers)[2] = returnValue; // put return value in register 2

    // add PCB to queue
	Jval payload;
	payload.v = (void*) PCB;
	dll_append(readyq, payload);

    kt_exit();
}

void do_write(struct PCB* PCB) {
    
    // validate inputs
    int fd = (PCB->registers)[5];
    int buffer = (PCB->registers)[6];
    int buffer_size = (PCB->registers)[7];

    DEBUG('e', "process %d is writing buffer of size %d to fd %d\n", PCB->pid, buffer_size, fd);
    DEBUG('e', "process %d is writing to fd %d, which has values: %d, %d, %d\n", PCB->pid, fd, PCB->fd[fd], PCB->fd_isconsole[fd], PCB->fd_readwrite[fd]);

    if (fd < 0 || fd >= TotalFDs || PCB->fd[fd] == 0 || PCB->fd_readwrite[fd] == 0) { // nonexistent fd or non-write fd
        syscall_return(PCB, -EBADF);
    }
    if ((buffer_size + buffer) > PCB->limit) { // too many chars to write
        syscall_return(PCB, -EFBIG);
    }
    if (!is_valid_addr(PCB, buffer)) { // bad address
        syscall_return(PCB, -EFAULT);
    }
    if (buffer_size < 0) { // bad argument
        syscall_return(PCB, -EINVAL);
    }

    DEBUG('e', "process %d passed checks, begin to write:\n", PCB->pid);

    int num_chars_printed = 0;

    if (PCB->fd_isconsole[fd] == 1 || PCB->fd_isconsole == 2) { // write to console (have to handle console interrupt)

        P_kt_sem(writers); // ensure that we are the only process writing to console

        for (int i = 0; i < buffer_size; i++) {
            console_write(main_memory[buffer + i + PCB->base]); // write command
            P_kt_sem(writeok); // wait for console interrupt
            num_chars_printed += 1;
        }

        V_kt_sem(writers); // now free writer sem for console

    } else { // write to pipe

        P_kt_sem(PCB->pipes[fd]->writers); // ensure that we are the only process writing to this pipe

        for (int i = 0; i < buffer_size; i++) {
            P_kt_sem(PCB->pipes[fd]->nslots);

            if (PCB->pipes[fd]->read_ref == 0) { // if at any point there are no readers exit (could find out halfway through my write)
                V_kt_sem(PCB->pipes[fd]->writers);
                syscall_return(PCB, -EPIPE);
            }

            char c = main_memory[buffer + i + PCB->base];
            PCB->pipes[fd]->buffer[PCB->pipes[fd]->tail] = c;
            PCB->pipes[fd]->tail += 1;
            PCB->pipes[fd]->tail = PCB->pipes[fd]->tail % PipeBufferSize;
            V_kt_sem(PCB->pipes[fd]->nelem);
            num_chars_printed += 1;
        }

        V_kt_sem(PCB->pipes[fd]->writers); // now free writer sem for this pipe

        // printf("wrote %d chars to pipe\n", num_chars_printed);

    }
    syscall_return(PCB, num_chars_printed);
}

void do_read(struct PCB* PCB) {

    // validate inputs
    int fd = (PCB->registers)[5];
    int buffer = (PCB->registers)[6];
    int buffer_size = (PCB->registers)[7];

    if (fd < 0 || fd >= TotalFDs || PCB->fd[fd] == 0 || PCB->fd_readwrite[fd] == 1) { // bad file number
        syscall_return(PCB, -EBADF);
    }
    if (!is_valid_addr(PCB, buffer)) { // bad address
        syscall_return(PCB, -EFAULT);
    }
    if (buffer_size < 0) { // bad argument
        syscall_return(PCB, -EINVAL);
    }
    if (!is_valid_addr(PCB, buffer + buffer_size)) { // full buffer will segfault
        syscall_return(PCB, -EFAULT);
    }

    int num_chars_read = 0;

    DEBUG('e', "process %d is reading buffer of size %d to fd %d\n", PCB->pid, buffer_size, fd);
    DEBUG('e', "process %d is reading from fd %d, which has values: %d, %d, %d\n", PCB->pid, fd, PCB->fd[fd], PCB->fd_isconsole[fd], PCB->fd_readwrite[fd]);

    if (PCB->fd_isconsole[fd] == 1) { // read from console buffer

        P_kt_sem(readers); // ensure that we are the only process reading

        for (int i = 0; i < buffer_size; i++) {
            P_kt_sem(nelem);
            if (console_read_buffer[queue_head] == -1) { // if -1 (EOF) instantly throw away
                queue_head += 1;
                queue_head = queue_head % 256;
                V_kt_sem(nslots);
                break;
            }
            // put character in use memory
            main_memory[buffer + i + PCB->base] = console_read_buffer[queue_head];
            queue_head += 1;
            queue_head = queue_head % 256;
            V_kt_sem(nslots);
            num_chars_read++;
        }

        V_kt_sem(readers); // now free reader sem

    } else {

        if (PCB->pipes[fd]->write_ref == 0 && kt_getval(PCB->pipes[fd]->nelem) == 0) { // if no writers and queue is empty
            main_memory[buffer + PCB->base] = -1; // return EOF
            syscall_return(PCB, 0); // exit with 0
        }

        P_kt_sem(PCB->pipes[fd]->readers); // ensure that we are the only process reading from this pipe

        for (int i = 0; i < buffer_size; i++) { // if no writers and some in queue, read what's left

            P_kt_sem(PCB->pipes[fd]->nelem);
            char c = (char) PCB->pipes[fd]->buffer[PCB->pipes[fd]->head];

            if (c == -1 && PCB->pipes[fd]->write_ref == 0) { // if EOF and no writers just die
                num_chars_read = 0;
                break;
            }

            main_memory[buffer + i + PCB->base] = c;
            PCB->pipes[fd]->head += 1;
            PCB->pipes[fd]->head = (PCB->pipes[fd]->head) % PipeBufferSize;
            V_kt_sem(PCB->pipes[fd]->nslots);
            num_chars_read++;

            if (kt_getval(PCB->pipes[fd]->nelem) == 0) { // no more characters in the read buffer
                // printf("no more to read from pipe\n");
                break;
            }
            
        }

        V_kt_sem(PCB->pipes[fd]->readers); // now free reader sem for this pipe

        // printf("read %d chars from pipe, buffer size was %d\n", num_chars_read, buffer_size);

    }
    syscall_return(PCB, num_chars_read);
}

void do_ioctl(struct PCB* PCB) {

    // validate inputs
    int one = (PCB->registers)[5];
    int tcgetp = (PCB->registers)[6];
    int termios_address = (PCB->registers)[7];

    if (one != 1) { // should be equal to 1
        syscall_return(PCB, -EINVAL); 
    }
    if (tcgetp != JOS_TCGETP) { // this parameter should have this val
        syscall_return(PCB, -EINVAL);
    }
    if (!is_valid_addr(PCB, termios_address)) { // out of bounds
        syscall_return(PCB, -EFAULT);
    }
    
    // TODO: make sure this is right (arg3-PCB->Base)
    ioctl_console_fill((struct JOStermios*) &(main_memory[termios_address + PCB->base]));

    syscall_return(PCB, 0);
}

void do_fstat(struct PCB* PCB) {

    // validate inputs
    int fd = (PCB->registers)[5];
    int buffer = (PCB->registers)[6];

    if (fd != 0 && fd != 1 && fd != 2) { // has to be these values
        syscall_return(PCB, -EBADF);
    }
    if (!is_valid_addr(PCB, buffer)) { // bad address
        syscall_return(PCB, -EFAULT);
    }

    // set bufSize accordingly to cookbook
    int bufSize = 1;
    if (fd == 1 || fd == 2) {
        bufSize = 256;
    }
    
    if (!is_valid_addr(PCB, bufSize + buffer)) { // no memory to hold buffer
        syscall_return(PCB, ENOMEM);
    }

    struct KOSstat* buf = (struct KOSstat*) &(main_memory[buffer + PCB->base]);
    stat_buf_fill(buf, bufSize);

    syscall_return(PCB, 0);
}

void do_getpagesize(struct PCB* PCB) {
    syscall_return(PCB, PageSize);
}

void do_sbrk(struct PCB* PCB) {
    
    // validate inputs
    int increment = (PCB->registers[5]);

    if (increment < 0 || (increment + PCB->brk_pointer) < 0 || (increment + PCB->brk_pointer) >= PCB->registers[StackReg]) { // can't bump into stack register
        syscall_return(PCB, -ENOMEM);
    }

    int temp = PCB->brk_pointer;
    PCB->brk_pointer += increment;

    syscall_return(PCB, temp);
}

void do_execve(struct PCB* PCB) {

    // validate inputs
    int path = (PCB->registers[5]); // pointer to char* for program file
    int argv_pointer = (PCB->registers[6]);
    int envp_pointer = (PCB->registers[7]);

    if (!is_valid_addr(PCB, argv_pointer) || !is_valid_addr(PCB, envp_pointer)) { // address out of bounds
        syscall_return(PCB, -EFAULT);
    }

    // get inputs
    char** argv = (char**) &main_memory[argv_pointer+PCB->base];
    int i = 0;
    while ((int) argv[i] != 0) {
        char* cur_str = (char*) &main_memory[(int) argv[i] + PCB->base];
        i++;
    }

    // count total characters in argv
    int charCount = 0;

    // copy all arguments
    char** copy = malloc((i+1) * sizeof(char*));
    argv = (char**) &main_memory[argv_pointer + PCB->base];
    for (int j = 0; j < i; j++) {
        char* cur_str = (char*) &main_memory[(int) argv[j] + PCB->base];
        charCount += strlen(cur_str) + 1;
        copy[j] = malloc((strlen(cur_str)+1) * sizeof(char));
        strcpy(copy[j], cur_str);
    }
    copy[i] = NULL;
    charCount++; // for null string at end

    int total_size = ((charCount) / 4) + (i + 1);

    if (total_size > PCB->limit) { // argv has too many bytes (ignore envp, is null)
        syscall_return(PCB, -E2BIG);
    }

    // load values
    int status = perform_execve(PCB, copy[0], copy);
    if (status < 0) {
        syscall_return(PCB, -1);
    }

    // free created argument arrays
    for (int j = 0; copy[j] != NULL; j++) {
        free(copy[j]);
    }
    free(copy);

    // specific for syscall_return
    execve_bool = 1;
    syscall_return(PCB, 0);
}

void do_getpid(struct PCB* PCB) {
    syscall_return(PCB, PCB->pid);
}

void finish_fork(struct PCB* PCB) {
    syscall_return(PCB, 0);
}

void do_fork(struct PCB* PCB) {

    // check if there is an open memory space
    int memspace = -1;
    for (int i = 0; i < 8; i++) {
        if (used_memory[i] == 0) {
            memspace = i;
            break;
        }
    }

    // identify open space and take it
    if (memspace == -1) { // no memory
        syscall_return(PCB, -EAGAIN); 
    }
    used_memory[memspace] = 1;

    // create new PCB
    struct PCB* child_PCB = malloc(sizeof(struct PCB));
	child_PCB->registers = malloc(sizeof(int) * NumTotalRegs);
    for (int i = 0; i < 40; i++) {
        (child_PCB->registers)[i] = (PCB->registers)[i];
    }
    child_PCB->base = memory_bases[memspace];
    child_PCB->limit = User_Limit;
    child_PCB->pid = get_new_pid();
    child_PCB->brk_pointer = PCB->brk_pointer;
    child_PCB->parent = PCB;
    child_PCB->waiter_sem = make_kt_sem(0);
    child_PCB->waiters = new_dllist();
    child_PCB->children = make_jrb();

    // copy file descriptors
    for (int i = 0; i < TotalFDs; i++) {
        child_PCB->fd[i] = PCB->fd[i];
        child_PCB->fd_isconsole[i] = PCB->fd_isconsole[i];
        child_PCB->fd_readwrite[i] = PCB->fd_readwrite[i];
        child_PCB->pipes[i] = PCB->pipes[i];
        // update ref count
        if (child_PCB->fd[i] == 1 && child_PCB->pipes[i] != NULL) {
            if (child_PCB->fd_readwrite[i] == 0) { // read
                child_PCB->pipes[i]->read_ref += 1;
            } else { // write
                child_PCB->pipes[i]->write_ref += 1;
            }
        }
    }

    // add child to parent's children tree
    DEBUG('e',"pid: %d --- added %d to its children tree\n", PCB->pid, child_PCB->pid);
	Jval payload;
	payload.v = (void*) child_PCB;
    jrb_insert_int(PCB->children,child_PCB->pid,payload);

    // copy all memory
    for (int i = 0; i < User_Limit; i++) {
        main_memory[(child_PCB->base) + i] = main_memory[(PCB->base) + i];
    }

    // add forked process to readyq
    kt_fork(finish_fork, child_PCB);

    syscall_return(PCB, child_PCB->pid);
}

void do_getdtablesize(struct PCB* PCB) {
    syscall_return(PCB, 64);
}

void do_exit(struct PCB* PCB) {
    
    // free space in main memory
    used_memory[PCB->base / User_Limit] = 0;

    // return address
    int exit_status = (PCB->registers[5]);
	PCB->registers[2] = exit_status;

    // close all my file descriptors
    DEBUG('e', "pid: %d --- is closing all fds\n", PCB->pid);
    for (int i = 0; i < TotalFDs; i++) {
        if (PCB->fd[i] != 0) {
            perform_close(PCB, i);
        }
    }
    DEBUG('e', "pid: %d --- closed all my fds\n", PCB->pid);

    // put all my children into the children of init
    while (!jrb_empty(PCB->children)) {

        // find child
        struct PCB* child = (struct PCB*) jrb_val(jrb_first(PCB->children)).v;
        DEBUG('e',"pid: %d --- put pid %d into init's children \n",PCB->pid, child->pid);

        // make the child's parent init
        child->parent = init;

        // put the child onto init's children tree
        Jval init_payload;
        init_payload.v = (void*) child;
        jrb_insert_int(init->children, child->pid, init_payload);

        // remove the child from this process's children tree
        jrb_delete_node(jrb_first(PCB->children));
    }

    // put all my waiters on init's waiter queue
    while (!dll_empty(PCB->waiters)) {

        // find waiter
        struct PCB* waiter = (struct PCB*) dll_val(dll_first(PCB->waiters)).v;
        DEBUG('e',"pid: %d --- put pid %d into init's waiters \n",PCB->pid, waiter->pid);

        // put the waiter onto init's waiter queue
        Jval init_payload;
        init_payload.v = (void*) waiter;
        dll_append(init->waiters, init_payload);

        // set waiter's parent to init? shouldn't matter

        // call V on init's waiter semaphore
        V_kt_sem(init->waiter_sem);

        // remove the waiter from this process's waiter queue
        dll_delete_node(dll_first(PCB->waiters));
    }

    // take me off my parent's children tree
    JRB cursor;
    cursor = jrb_find_int((PCB->parent)->children, PCB->pid);
    jrb_delete_node(cursor);
    DEBUG('f',"pid: %d --- is no longer on the tree of %d\n", PCB->pid, PCB->parent->pid);

    // add me to my parent's waiter q
	Jval payload;
	payload.v = (void*) PCB;
	dll_append((PCB->parent)->waiters, payload);
    DEBUG('f',"pid: %d --- added to waiter queue of %d\n", PCB->pid, PCB->parent->pid);

    // allow my parent's wait to finish
    V_kt_sem((PCB->parent)->waiter_sem);

    DEBUG('e',"pid: %d --- exited");

    kt_exit();
}

void do_getppid(struct PCB* PCB) {
    syscall_return(PCB, (PCB->parent)->pid);
}

void do_wait(struct PCB* PCB) {

    // validate inputs
    int return_pointer = (PCB->registers)[5]; // arg1 holds a pointer to address where the exit value of the waited process should be put

    if (jrb_empty(PCB->children) && dll_empty(PCB->waiters)) { // if I have no children in my children tree and no waiters then fail, as no one will ever hit this semaphore
        syscall_return(PCB, -ECHILD);
    }
    if (!is_valid_addr(PCB, return_pointer)) { // make sure pointer is valid
        syscall_return(PCB, -EFAULT);
    }

    P_kt_sem(PCB->waiter_sem);

    // take child off my waiter list
    struct PCB* child_PCB = (struct PCB*) (dll_val(dll_first(PCB->waiters))).v;
    int returned_pid = child_PCB->pid;

    // fill return value
    int exit_val = (child_PCB->registers[2]);

    if (return_pointer != 0) { // ensure not null
        main_memory[return_pointer + PCB->base] = exit_val;
    }

    DEBUG('e',"pid: %d --- received exit status %d from %d\n", PCB->pid, exit_val, returned_pid);

    // free the child pid
    destroy_pid(child_PCB->pid);
    DEBUG('e',"pid: %d is available again\n", child_PCB->pid);

    // free child PCB
    free(child_PCB->registers);
    free(child_PCB);

    // remove it from waiter queue
    dll_delete_node(dll_first(PCB->waiters));

    DEBUG('f',"returning %d\n",returned_pid);

    syscall_return(PCB, returned_pid);
}

void do_pipe(struct PCB* PCB) {
    
    // validate inputs
    int return_pointer = (PCB->registers[5]);

    if (pipe_count >= 1024) { // OS hit pipe limit
        syscall_return(PCB, -ENFILE);
    }
    if (!is_valid_addr(PCB, return_pointer)) { // segfault
        syscall_return(PCB, -EFAULT);
    }

    // get new fds (this function will also modify the PCB's fd arrays)
    int read = get_fd(PCB, 0);
    int write = get_fd(PCB, 1);
    if (read == -1 || write == -1) { // if one file descriptor was unable to be created, deallocate both
        rm_fd(PCB, read);
        rm_fd(PCB, write);
        syscall_return(PCB, -EMFILE);
    }

    // allocate new pipe
    struct pipe* new_pipe = malloc(sizeof(struct pipe));
    new_pipe->buffer = malloc(sizeof(char) * PipeBufferSize);
    new_pipe->head = 0;
    new_pipe->tail = 0;
    new_pipe->read_ref = 1;
    new_pipe->write_ref = 1;
    new_pipe->writers = make_kt_sem(1);
    new_pipe->readers = make_kt_sem(1);
    new_pipe->nslots = make_kt_sem(PipeBufferSize);
    new_pipe->nelem = make_kt_sem(0);

    // assign pipes to PCB
    PCB->pipes[read] = new_pipe;
    PCB->pipes[write] = new_pipe;

    // put the new fds in user memory
    int* loc = main_memory + return_pointer + PCB->base;
    loc[0] = read;
    loc[1] = write;
    // main_memory[return_pointer + PCB->base] = read;
    // main_memory[return_pointer + PCB->base + 4] = write;

    syscall_return(PCB, 0);
}

void do_close(struct PCB* PCB) {

    int fd = (PCB->registers[5]);
    DEBUG('e', "    attempting to close: %d\n", fd);
    if (PCB->fd[fd] != 1) { // fd doesn't exist
        syscall_return(PCB, -EBADF);
    }

    perform_close(PCB, fd);

    syscall_return(PCB, 0);
}

void do_dup(struct PCB* PCB) {

    // validate inputs
    int fd_to_copy = (PCB->registers[5]);
    if (PCB->fd[fd_to_copy] == 0) { // not a currently open fd
        syscall_return(PCB, -EBADF);
    }

    // get smallest available fd
    int new_fd = get_fd(PCB, PCB->fd_readwrite[fd_to_copy]);

    // match console flag
    PCB->fd_isconsole[new_fd] = PCB->fd_isconsole[fd_to_copy];

    DEBUG('p', "pid: %d --- duped fd %d to fd %d.\n            New fd has values: (readwrite = %d), (isconsole = %d)\n            Old fd has values: (readwrite = %d), (isconsole = %d)\n",
        PCB->pid, fd_to_copy, new_fd, PCB->fd_readwrite[new_fd], PCB->fd_isconsole[new_fd], PCB->fd_readwrite[fd_to_copy], PCB->fd_isconsole[fd_to_copy]
    );

    // set pipe
    PCB->pipes[new_fd] = PCB->pipes[fd_to_copy];

    // increment corresponding refcounts
    if (PCB->fd_readwrite[new_fd] == 0) {
        PCB->pipes[new_fd]->read_ref += 1;
    } else {
        PCB->pipes[new_fd]->write_ref += 1;
    }
    
    // return the new fd
    syscall_return(PCB, new_fd);
}

void do_dup2(struct PCB* PCB) {

    // validate inputs
    int fd_to_copy = (PCB->registers[5]);
    int target_fd = (PCB->registers[6]);

    if (PCB->fd[fd_to_copy] == 0) { // not a currently open fd
        syscall_return(PCB, -EBADF);
    }
    if (PCB->pipes[fd_to_copy] == PCB->pipes[target_fd]) {
        syscall_return(PCB, target_fd);
    }

    // close target_fd
    perform_close(PCB,target_fd);

    // dup fd_to_copy to target_fd
    PCB->fd[target_fd] = 1;
    PCB->fd_readwrite[target_fd] = PCB->fd_readwrite[fd_to_copy];
    PCB->fd_isconsole[target_fd] = PCB->fd_isconsole[fd_to_copy];

    // set pipe
    PCB->pipes[target_fd] = PCB->pipes[fd_to_copy];

    // increment corresponding refcounts
    if (PCB->fd_readwrite[target_fd] == 0) {
        PCB->pipes[target_fd]->read_ref += 1;
    } else {
        PCB->pipes[target_fd]->write_ref += 1;
    }

    DEBUG('e', "pid: %d --- duped fd %d to fd %d.\n            New fd has values: (readwrite = %d), (isconsole = %d)\n            Old fd has values: (readwrite = %d), (isconsole = %d)\n",
        PCB->pid, fd_to_copy, target_fd, PCB->fd_readwrite[target_fd], PCB->fd_isconsole[target_fd], PCB->fd_readwrite[fd_to_copy], PCB->fd_isconsole[fd_to_copy]
    );

    syscall_return(PCB, target_fd);
}