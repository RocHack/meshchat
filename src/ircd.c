/* vim: set expandtab ts=4 sw=4: */
/*
 * ircd.c
 */

#include <stdlib.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include "ircd.h"
#include "meshchat.h"
#include "util.h"

struct irc_session {
    // fd socket (TCP recv())
    int fd;
    // link
    struct irc_session *next;
};

struct irc_user {
    char name[MESHCHAT_NAME_LEN]; // 9
    char username[MESHCHAT_FULLNAME_LEN]; // 32
    char realname[MESHCHAT_FULLNAME_LEN]; // 32
    char host[MESHCHAT_HOST_LEN]; // 63
    struct irc_user *next;
};

struct irc_channel {
    char name[MESHCHAT_CHANNEL_LEN]; // 50
    char topic[MESHCHAT_MESSAGE_LEN]; // 512
    struct irc_user *user_list;
};

struct ircd {
    // fd socket (TCP accept())
    int fd;
    char nick[MESHCHAT_NAME_LEN]; // 9
    char username[MESHCHAT_FULLNAME_LEN]; // 32
    char realname[MESHCHAT_FULLNAME_LEN]; // 32
    // connected sessions (LL)
    struct irc_session *session_list;
    struct irc_channel *channel_list;
};

ircd_t *ircd_new() {
    ircd_t *ircd = calloc(1, sizeof(ircd_t));
    if (!ircd) {
        perror("calloc");
        return NULL;
    }

    ircd->fd = -1;
    ircd->session_list = NULL;
    ircd->channel_list = NULL;

    return ircd;
}

void
ircd_free(ircd_t *ircd) {
}

void
ircd_start(ircd_t *ircd) {
    struct addrinfo hints;
    struct addrinfo *result;

    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0;
    // rest 0

    int status = getaddrinfo(NULL, "ircd", &hints, &result);
    if (status) {
        printf("getaddrinfo: ");
        puts(gai_strerror(status));
        return;
    }

    ircd->fd = socket(result->ai_family, result->ai_socktype, 0);
    if (ircd->fd < 0) {
        perror("socket");
        freeaddrinfo(result);
        return;
    }

    if (bind(ircd->fd, result->ai_addr, result->ai_addrlen) < 0) {
        perror("bind");
        freeaddrinfo(result);
        ircd->fd = -1;
        return;
    }

    if (listen(ircd->fd, IRCD_BACKLOG) > 0) {
        perror("listen");
        ircd->fd = -1;
        return;
    }

    printf("ircd listening on %s\n", sprint_addrport(result->ai_addr));

    freeaddrinfo(result);
}

void
ircd_add_select_descriptors(ircd_t *ircd, fd_set *in_set,
        fd_set *out_set, int *maxfd) {
    if (ircd->fd > 0) {
        // wait for accept()s
        FD_SET(ircd->fd, in_set);
        if (ircd->fd > *maxfd) {
            *maxfd = ircd->fd + 1;
        }
    }
    // wait for recv()s from clients
    struct irc_session *session = ircd->session_list;
    while (session != NULL) {
        FD_SET(session->fd, in_set);
        if (session->fd > *maxfd) {
            *maxfd = session->fd + 1;
        }
        session = session->next;
    }
}

void
ircd_process_select_descriptors(ircd_t *ircd, fd_set *in_set,
        fd_set *out_set) {
    struct sockaddr_storage addr;
    socklen_t addrlen = sizeof(struct sockaddr_in6);
    if (FD_ISSET(ircd->fd, in_set)) {
        // available accept
        if (accept(ircd->fd, (struct sockaddr *)&addr, &addrlen) < 0) {
            perror("accept");
            return;
        }

        printf("accepted connection from %s\n", sprint_addrport((struct sockaddr *)&addr));
    }
}

