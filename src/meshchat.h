/*
 * meshchat.h
 */

#ifndef MESHCHAT_H
#define MESHCHAT_H

#include <sys/select.h>

typedef struct meshchat meshchat_t;

meshchat_t *meshchat_new();

void meshchat_free(meshchat_t *mc);

void meshchat_start(meshchat_t *mc);

void meshchat_add_select_descriptors(meshchat_t *mc, fd_set *in_set,
		fd_set *out_set, int *maxfd);

void meshchat_process_select_descriptors(meshchat_t *mc, fd_set *in_set,
		fd_set *out_set);

#endif /* MESHCHAT_H */
