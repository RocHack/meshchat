/*
 * ircd.c
 */

#include <stdlib.h>
#include <stdio.h>
#include "ircd.h"

struct ircd {
};

ircd_t *ircd_new() {
	ircd_t *ircd = calloc(1, sizeof(ircd_t));
	if (!ircd) {
		perror("calloc");
		return NULL;
	}

	return ircd;
}

void
ircd_free(ircd_t *ircd) {
}

void
ircd_start(ircd_t *ircd) {
}

void
ircd_add_select_descriptors(ircd_t *mc, fd_set *in_set,
		fd_set *out_set, int *maxfd) {
}

void
ircd_process_select_descriptors(ircd_t *mc, fd_set *in_set,
		fd_set *out_set) {
}
