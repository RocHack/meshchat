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
    char ip[INET6_ADDRSTRLEN];
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

static const struct irc_prefix localhost = {.host = "localhost"};

void ircd_free_session(ircd_t *ircd, struct irc_session *session);

inline void
callback_call(callback_t cb, char *channel, char *data) {
    if (cb.obj) {
        cb.fn(cb.obj, channel, data);
    }
}

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
    struct irc_session *session = ircd->session_list, *next;
    while (session) {
        next = session->next;
        free(session);
        session = next;
    }
    free(ircd);
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

    int opt = 1;
    if (setsockopt(ircd->fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt) < 0) {
        perror("setsockopt");
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

int sprint_prefix(char *buffer, struct irc_prefix *prefix) {
    if (prefix->nick) {
        if (prefix->host) {
            if (prefix->user) {
                return sprintf(buffer, ":%s!~%s@%s ", prefix->nick, prefix->user,
                        prefix->host);
            } else {
                return sprintf(buffer, ":%s@%s ", prefix->nick, prefix->host);
            }
        } else {
            return sprintf(buffer, ":%s ", prefix->nick);
        }
    } else if (prefix->host) {
        return sprintf(buffer, ":%s ", prefix->host);
    } else {
        return 0;
    }
}

void
ircd_send(ircd_t *ircd, struct irc_session *session, struct irc_prefix *prefix,
        const char *format, ...) {
    char buffer[MESHCHAT_MESSAGE_LEN]; // 512
    va_list ap;
    size_t prefixlen;
    size_t suffixlen = 2;
    int len = 0;
    int sv = 0;

    prefixlen = prefix ? sprint_prefix(buffer, prefix) : 0;

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
        char oldnick[MESHCHAT_NAME_LEN];
        struct irc_prefix prefix = {
            .nick = oldnick,
            .user = ircd->username,
            .host = session->ip
        };
        strncpy(oldnick, ircd->nick, MESHCHAT_NAME_LEN);
        strwncpy(ircd->nick, lineptr + 5, MESHCHAT_NAME_LEN);
        callback_call(ircd->callbacks.on_nick, NULL, ircd->nick);
        if (oldnick[0]) {
            // acknowledge nick change
            ircd_send(ircd, session, &prefix, "NICK :%s", ircd->nick);
        }

    } else if (strncmp(lineptr, "USER ", 5) == 0) {
        // NICK username
        strwncpy(ircd->username, lineptr + 5, MESHCHAT_FULLNAME_LEN);
        ircd_send(ircd, session, NULL, "001 %s :Welcome to this MeshChat Relay (I'm not really an IRC server!)", ircd->username);
        ircd_send(ircd, session, NULL, "002 %s :IRC MeshChat v1", ircd->username);
        ircd_send(ircd, session, NULL, "003 %s :Created 0", ircd->username);
        ircd_send(ircd, session, NULL, "004 %s localhost ircd-meshchat-0.0.1 DOQRSZaghilopswz CFILMPQSbcefgijklmnopqrstvz bkloveqjfI", ircd->username);

    } else if (strncmp(lineptr, "JOIN ", 5) == 0) {
        char *channel = lineptr + 5;
        callback_call(ircd->callbacks.on_join, channel, ircd->nick);
        ircd_join(ircd, &prefix, channel);

    } else if (strncmp(lineptr, "PART ", 5) == 0) {
        char *channel = lineptr + 5;
        callback_call(ircd->callbacks.on_part, channel, ircd->nick);
        ircd_part(ircd, &prefix, channel);

    } else if (strncmp(lineptr, "PRIVMSG ", 8) == 0) {
        char channel[MESHCHAT_CHANNEL_LEN];
        char message[MESHCHAT_MESSAGE_LEN];
        int clen = strwncpy(channel, lineptr + 8, MESHCHAT_CHANNEL_LEN);
        if (lineptr[clen + 9] == ':') {
            // skip colon/prefix
            clen++;
        }
        strncpy(message, lineptr + 9 + clen, MESHCHAT_MESSAGE_LEN);
        // check for CTCP message (surrounded with 0x01)
        if (message[0] == 0x01) {
            message[strlen(message)-1] = '\0';
            if (strncmp(message+1, "ACTION ", 7) == 0) {
                printf("message: \"%s\"\n", message+8);
                callback_call(ircd->callbacks.on_action, channel, message+8);
            } else {
                printf("message: (%zu) \"%s\"\n", strlen(message+1), message+1);
            }
        } else {
            callback_call(ircd->callbacks.on_msg, channel, message);
        }

    } else if (strncmp(lineptr, "NOTICE ", 7) == 0) {
        // check for CTCP message (surrounded with 0x01)
        if (lineptr[7] == 0x01) {
            //message[strlen(message)-1] = '\0';
            printf("notice! \"%s\"\n", lineptr+8);
        } else {
            char channel[MESHCHAT_CHANNEL_LEN];
            char message[MESHCHAT_MESSAGE_LEN];
            int clen = strwncpy(channel, lineptr + 7, MESHCHAT_CHANNEL_LEN);
            if (lineptr[clen + 8] == ':') {
                // skip colon/prefix
                clen++;
            }
            strncpy(message, lineptr + 8 + clen, MESHCHAT_MESSAGE_LEN);
            printf("notice in %s: \"%s\"\n", channel, message);
            callback_call(ircd->callbacks.on_notice, channel, message);
        }

    } else if (strncmp(lineptr, "PING ", 5) == 0) {
        ircd_send(ircd, session, NULL, "PONG localhost", ircd->username);

    } else if (strncmp(lineptr, "MODE ", 5) == 0) {
        return;

    } else if (strncmp(lineptr, "WHOIS ", 6) == 0) {
        // TODO
        return;

    } else if (strncmp(lineptr, "QUIT ", 5) == 0) {
        close(session->fd);
        session->fd = -1;
        //ircd_free_session(ircd, session);

    } else if (strncmp(lineptr, "PASS ", 5) == 0) {
        // TODO
        return;

    } else {
        printf("Unhandled message: %s\n", lineptr);
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
    // TODO: handle \r or \n here
    while ( (line_end = (char*)memchr((void*)line_start, '\r', session->inbuf_used - (line_start - session->inbuf))))
    {
        *line_end = 0;
        ircd_handle_message(ircd, session, line_start);
        line_start = line_end + 2;
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
            if (!inet_ntop(AF_INET6, &((struct sockaddr_in6 *)&addr)->sin6_addr, new_session->ip, INET6_ADDRSTRLEN)) {
                perror("inet_ntop");
            }
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

void
ircd_join(ircd_t *ircd, struct irc_prefix *prefix, const char *channel) {
    // send to all sessions
    for (struct irc_session *sess = ircd->session_list; sess; sess = sess->next) {
        ircd_send(ircd, sess, prefix, "JOIN :%s", channel);
    }
}

void
ircd_part(ircd_t *ircd, struct irc_prefix *prefix, const char *channel) {
    for (struct irc_session *sess = ircd->session_list; sess; sess = sess->next) {
        ircd_send(ircd, sess, prefix, "PART %s", channel);
    }
}

void
ircd_privmsg(ircd_t *ircd, struct irc_prefix *prefix, const char *target,
        const char *msg) {
    for (struct irc_session *sess = ircd->session_list; sess; sess = sess->next) {
        ircd_send(ircd, sess, prefix, "PRIVMSG %s :%s", target, msg);
    }
}

void
ircd_nick(ircd_t *ircd, struct irc_prefix *prefix, const char *nick) {
    for (struct irc_session *sess = ircd->session_list; sess; sess = sess->next) {
        ircd_send(ircd, sess, prefix, "NICK :%s", nick);
    }
}
