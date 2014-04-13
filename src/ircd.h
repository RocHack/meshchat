/*
 * ircd.h
 */

#ifndef IRCD_H
#define IRCD_H

#include <sys/select.h>

#define IRCD_BACKLOG 10

typedef struct ircd ircd_t;

ircd_t *ircd_new();

void ircd_free(ircd_t *ircd);

void ircd_start(ircd_t *ircd);

void ircd_add_select_descriptors(ircd_t *mc, fd_set *in_set,
		fd_set *out_set, int *maxfd);

void ircd_process_select_descriptors(ircd_t *mc, fd_set *in_set,
		fd_set *out_set);

#endif /* IRCD_H */
