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
