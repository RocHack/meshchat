/* vim: set expandtab ts=4 sw=4: */
/*
 * ircd.c
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "ircd.h"
#include "meshchat.h"
#include "util.h"

struct irc_session {
    // fd socket (TCP recv())
    int fd;
    char inbuf[IRCD_BUFFER_LEN];
    size_t inbuf_used;
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
    ircd_callbacks_t callbacks;
};

ircd_t *ircd_new(ircd_callbacks_t *callbacks) {
    ircd_t *ircd = calloc(1, sizeof(ircd_t));
    if (!ircd) {
        perror("calloc");
        return NULL;
    }

    ircd->fd = -1;
    ircd->session_list = NULL;
    ircd->channel_list = NULL;

    memcpy(&ircd->callbacks, callbacks, sizeof(ircd_callbacks_t));

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
ircd_send(ircd_t *ircd, struct irc_session *session, const char *format, ...) {
    char buffer[MESHCHAT_MESSAGE_LEN]; // 512
    va_list ap;
    size_t prefixlen = 11;
    size_t suffixlen = 2;
    int len = 0;
    int sv = 0;

    strcpy(buffer, ":localhost ");

    va_start(ap, format);
    len = vsnprintf(buffer + prefixlen, MESHCHAT_MESSAGE_LEN - prefixlen - suffixlen, format, ap);
    va_end(ap);

    len += prefixlen;

    if (len > MESHCHAT_MESSAGE_LEN - suffixlen) {
        len = MESHCHAT_MESSAGE_LEN;
    }
    strcpy(buffer + len, "\r\n");

    len += suffixlen;
    while (sv < len) {
        sv += send(session->fd, buffer + sv, len - sv, 0);
    }
}

void
ircd_handle_message(ircd_t *ircd, struct irc_session *session,
        char *lineptr) {
    if (strncmp(lineptr, "NICK ", 5) == 0) {
        // NICK username
        strwncpy(ircd->nick, lineptr + 5, MESHCHAT_NAME_LEN);
        ircd->callbacks.on_nick.fn(ircd->callbacks.on_nick.obj, NULL, ircd->nick);
        //printf("NICK %s\n", ircd->nick);
    } else if (strncmp(lineptr, "USER ", 5) == 0) {
        // NICK username
        strwncpy(ircd->username, lineptr + 5, MESHCHAT_FULLNAME_LEN);
        ircd_send(ircd, session, "001 %s :Welcome to this MeshChat Relay (I'm not really an IRC server!)", ircd->username);
        ircd_send(ircd, session, "002 %s :IRC MeshChat v1", ircd->username);
        ircd_send(ircd, session, "003 %s :Created 0", ircd->username);
        ircd_send(ircd, session, "004 %s localhost ircd-meshchat-0.0.1 DOQRSZaghilopswz CFILMPQSbcefgijklmnopqrstvz bkloveqjfI", ircd->username);
        //printf("NICK %s\n", ircd->nick);
    } else if (strncmp(lineptr, "JOIN ", 5) == 0) {
        // NICK username
        ircd->callbacks.on_join.fn(ircd->callbacks.on_join.obj, lineptr + 5, NULL);
        printf("CLIENT WANTS TO JOIN %s\n", lineptr + 5);
        //strwncpy(ircd->nick, lineptr + 5, MESHCHAT_CHANNEL_LEN);
        //printf("NICK %s\n", ircd->nick);
    } else if (strncmp(lineptr, "PRIVMSG ", 8) == 0) {
        // NICK username
        char channel[MESHCHAT_CHANNEL_LEN];
        char message[MESHCHAT_MESSAGE_LEN];
        int clen = strwncpy(channel, lineptr + 8, MESHCHAT_CHANNEL_LEN);
        strncpy(message, lineptr + 9 + clen, MESHCHAT_MESSAGE_LEN);
        ircd->callbacks.on_msg.fn(ircd->callbacks.on_msg.obj, channel, message);
        //strwncpy(ircd->nick, lineptr + 5, MESHCHAT_CHANNEL_LEN);
        //printf("NICK %s\n", ircd->nick);
    }
}

void
ircd_free_session(ircd_t *ircd, struct irc_session *session) {
    session->fd = -1;
    if (ircd->session_list == session) {
        ircd->session_list = session->next;
        free(session);
    } else {
        struct irc_session *other_s = ircd->session_list;
        while (other_s != NULL) {
            if (other_s->next == session) {
                other_s->next = session->next;
                free(session);
                return;
            }
            other_s = other_s->next;
        }
    }
}

int
ircd_handle_buffer(ircd_t *ircd, struct irc_session *session) {
    size_t buf_remain = IRCD_BUFFER_LEN - session->inbuf_used;
    if (buf_remain == 0) {
        fprintf(stderr, "Line exceeded buffer length!\n");
        return 1;
    }

    ssize_t rv = recv(session->fd, session->inbuf + session->inbuf_used, buf_remain, MSG_DONTWAIT);
    if (rv == 0) {
        fprintf(stderr, "Connection closed.\n");
        close(session->fd);
        return 0;
    }
    if (rv < 0 && errno == EAGAIN) {
        /* no data for now, call back when the socket is readable */
        return 1;
    }
    if (rv < 0) {
        perror("recv");
        close(session->fd);
        return 0;
    }
    session->inbuf_used += rv;

    /* Scan for newlines in the line buffer; we're careful here to deal with embedded \0s
     * an evil client may send, as well as only processing lines that are complete.
     */
    char *line_start = session->inbuf;
    char *line_end;
    size_t max_len = session->inbuf_used - (line_start - session->inbuf);
    if (max_len > MESHCHAT_MESSAGE_LEN) {
        max_len = MESHCHAT_MESSAGE_LEN; // MUST be <= 512 chars per line
    }
    while ( (line_end = (char*)memchr((void*)line_start, '\n', session->inbuf_used - (line_start - session->inbuf))))
    {
        *line_end = 0;
        ircd_handle_message(ircd, session, line_start);
        line_start = line_end + 1;
    }
    /* Shift buffer down so the unprocessed data is at the start */
    // printf("%d used %d read %d left\n", session->inbuf_used, rv, session->inbuf - line_start);
    session->inbuf_used += (session->inbuf - line_start);
    if (session->inbuf_used > 0) {
        memmove(session->inbuf, line_start, session->inbuf_used);
    }

    return 1;
}

void
ircd_process_select_descriptors(ircd_t *ircd, fd_set *in_set,
        fd_set *out_set) {
    struct sockaddr_storage addr;
    socklen_t addrlen = sizeof(struct sockaddr_in6);
    if (FD_ISSET(ircd->fd, in_set)) {
        int new_fd = -1;
        // available accept
        if ((new_fd = accept(ircd->fd, (struct sockaddr *)&addr, &addrlen)) < 0) {
            perror("accept");
        } else {
            printf("accepted connection from %s\n", sprint_addrport((struct sockaddr *)&addr));

            struct irc_session *new_session = (struct irc_session *)malloc(sizeof(struct irc_session));
            new_session->fd = new_fd;
            new_session->inbuf_used = 0;
            new_session->next = ircd->session_list;
            ircd->session_list = new_session;
        }
    }

    // wait for recv()s from clients
    struct irc_session *session = ircd->session_list;
    while (session != NULL) {
        int keep_session = 1;
        struct irc_session *to_remove = NULL;
        if (FD_ISSET(session->fd, in_set)) {
            keep_session = ircd_handle_buffer(ircd, session);
        }

        if (!keep_session) {
            to_remove = session;
        }

        session = session->next;

        // ew, ugly!
        if (!keep_session) {
            ircd_free_session(ircd, to_remove);
        }
    }
}
