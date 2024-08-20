#include "kos.h"
#include "simulator_lab2.h"
#include "dllist.h"

void read_buffer() {
	while (1) {
		P_kt_sem(console_wait);				// I have a character waiting to be on buffer
		P_kt_sem(nslots);					// I have space to add to buffer

		char c = console_read();			// read the character

		console_read_buffer[queue_tail] = c;	// add to buffer
		queue_tail++;
		queue_tail = queue_tail % 256;

		V_kt_sem(nelem);
	}
}