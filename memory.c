#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "memory.h"

pid_t get_new_pid() {
	curpid = 1;
	Jval payload;
	JRB search = jrb_find_int(pidtree, curpid);
	while (search != NULL) {
		curpid++;
		search = jrb_find_int(pidtree, curpid);
	}
	payload.i = curpid;
	jrb_insert_int(pidtree,curpid,payload);
	return curpid;
}

void destroy_pid(int pid) {
	Jval payload;
	payload.i = (pid_t) pid;
	JRB cursor = jrb_find_int(pidtree, pid);
	if (cursor != NULL) {
		jrb_delete_node(cursor);
	} else {
		DEBUG('e', "failed to destroy pid %d!\n",pid);
		return;
	}
}

void print_tree() {
	DEBUG('e', "printing pid tree:\n");
	JRB cursor;
	int i;
	jrb_traverse(cursor,pidtree) {
		i = jval_i(jrb_val(cursor));
		DEBUG('e', "%d ",i);
	}
	DEBUG('e', "\n");
}