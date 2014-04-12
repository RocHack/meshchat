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
