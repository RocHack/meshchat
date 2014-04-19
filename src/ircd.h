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

struct irc_prefix {
    const char *nick;
    const char *user;
    const char *host;
};

typedef struct {
    void *obj;
    void (*fn) (void *obj, char *channel, char *data);
} callback_t;

typedef struct {
    callback_t on_msg;
    callback_t on_notice;
    callback_t on_join;
    callback_t on_part;
    callback_t on_nick;
} ircd_callbacks_t;

void callback_call(callback_t cb, char *channel, char *data);

ircd_t *ircd_new(ircd_callbacks_t *callbacks);

void ircd_free(ircd_t *ircd);

void ircd_start(ircd_t *ircd);

void ircd_set_hostname(ircd_t *ircd, const char *host);

void ircd_add_select_descriptors(ircd_t *mc, fd_set *in_set,
        fd_set *out_set, int *maxfd);

void ircd_process_select_descriptors(ircd_t *mc, fd_set *in_set,
        fd_set *out_set);

void ircd_join(ircd_t *ircd, struct irc_prefix *prefix, const char *channel);

void ircd_part(ircd_t *ircd, struct irc_prefix *prefix, const char *channel,
        const char *message);

void ircd_quit(ircd_t *ircd, struct irc_prefix *prefix, const char *message);

void ircd_privmsg(ircd_t *ircd, struct irc_prefix *prefix, const char *target,
        const char *msg);

void ircd_notice(ircd_t *ircd, struct irc_prefix *prefix, const char *target,
        const char *msg);

void ircd_nick(ircd_t *ircd, struct irc_prefix *prefix, const char *nick);

size_t ircd_get_channels(ircd_t *ircd, char *buffer, size_t buf_len);

#endif /* IRCD_H */
