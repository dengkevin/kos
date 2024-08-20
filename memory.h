#ifndef MEMORY_H
#define MEMORY_H

#include "dllist.h"
#include "jrb.h"
#include "kt.h"
#include "sys/types.h"

// which memory spaces are free
extern int used_memory[8]; // free when value is 0, occupied when value is 1
extern int memory_bases[8];

// rb tree for process ids
extern JRB pidtree;
extern pid_t curpid;

pid_t get_new_pid();
void destroy_pid(int pid);
void print_tree();

#endif