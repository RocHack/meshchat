/*
 * meshchat.c
 */

#include <stdlib.h>
#include <stdio.h>
#include "ircd.h"

#include "meshchat.h"

struct meshchat {
};

meshchat_t *meshchat_new() {
	meshchat_t *mc = calloc(1, sizeof(meshchat_t));
	if (!mc) {
		perror("calloc");
		return NULL;
	}

	return mc;
}

void
meshchat_free(meshchat_t *mc) {
}

void
meshchat_start(meshchat_t *mc) {
}

void
meshchat_add_select_descriptors(meshchat_t *mc, fd_set *in_set,
		fd_set *out_set, int *maxfd) {
}

void
meshchat_process_select_descriptors(meshchat_t *mc, fd_set *in_set,
		fd_set *out_set) {
}
