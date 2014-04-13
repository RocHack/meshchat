/* vim: set expandtab ts=4 sw=4: */
/*
 * ircd.h
 */

#ifndef IRCD_H
#define IRCD_H

#include <sys/select.h>

#define IRCD_BACKLOG 10
#define IRCD_BUFFER_LEN 1024

typedef struct ircd ircd_t;

typedef struct {
    void *obj;
    void (*fn) (void *obj, char *channel, char *data);
} callback_t;

typedef struct {
    callback_t on_msg;
    callback_t on_privmsg;
    callback_t on_action;
    callback_t on_notice;
    callback_t on_join;
    callback_t on_part;
    callback_t on_nick;
} ircd_callbacks_t;

ircd_t *ircd_new(ircd_callbacks_t *callbacks);

void ircd_free(ircd_t *ircd);

void ircd_start(ircd_t *ircd);

void ircd_add_select_descriptors(ircd_t *mc, fd_set *in_set,
        fd_set *out_set, int *maxfd);

void ircd_process_select_descriptors(ircd_t *mc, fd_set *in_set,
        fd_set *out_set);

#endif /* IRCD_H */
