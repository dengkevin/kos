#include <stdlib.h>

#include "kos.h"
#include "simulator_lab2.h"
#include "dllist.h"

struct PCB* global_pcb;

void scheduler() {

    if (dll_empty(readyq)) {

        global_pcb = NULL;
        
        // if init has no children, we can now halt
        if (jrb_empty(init->children)) {
            SYSHalt();
        }

        noop();

    } else {

        // load registers from readyq
        struct PCB* PCB = (struct PCB*) (dll_val(dll_first(readyq))).v;

        // point global_pcb to this one
        global_pcb = PCB;

        // delete first node from readyq
        dll_delete_node(dll_first(readyq));

        // printf("running code from process %d\n", PCB->pid);
        User_Base = global_pcb->base;
        User_Limit = global_pcb->limit;

        // call timer
        start_timer(10);

        // run user code
        run_user_code(PCB->registers);

    }
}