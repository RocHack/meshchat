/* vim: set expandtab ts=4 sw=4: */
/*
 * meshchat.c
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <time.h>
#include "ircd.h"
#include "hash/hash.h"
#include "meshchat.h"
#include "cjdnsadmin.h"
#include "util.h"

#define MESHCHAT_PORT 14627
#define MESHCHAT_PACKETLEN 1400
#define MESHCHAT_PEERFETCH_INTERVAL 60
#define MESHCHAT_PEERSERVICE_INTERVAL 5
#define MESHCHAT_TIMEOUT 60
#define MESHCHAT_PING_INTERVAL 20
#define MESHCHAT_RETRY_INTERVAL 900

struct meshchat {
    ircd_t *ircd;
    cjdnsadmin_t *cjdnsadmin;
    //const char *host;
    int port;
    int listener;
    char ip[INET6_ADDRSTRLEN];
    time_t last_peerfetch;
    time_t last_peerservice;
    hash_t *peers;
    char nick[MESHCHAT_NAME_LEN]; // our node's nick
    struct peer *me;
};

struct peer {
    char ip[40];
    struct sockaddr_in6 addr;
    enum peer_status status;
    time_t last_message;    // they sent to us
    time_t last_greeted;    // we sent to them
    char *nick;
};

enum event_type {
    EVENT_GREETING = 1,
    EVENT_MSG,
    EVENT_PRIVMSG,
    EVENT_ACTION,
    EVENT_NOTICE,
    EVENT_JOIN,
    EVENT_PART,
    EVENT_NICK,
};

const char *event_names[] = {
    NULL,
    "greeting",
    "msg",
    "privmsg",
    "action",
    "notice",
    "join",
    "part",
    "nick"
};

void handle_datagram(meshchat_t *mc, struct sockaddr *addr, char *buffer,
        size_t len);
peer_t *get_peer(meshchat_t *mc, const char *ip);
void found_ip(void *obj, const char *ip);
void service_peers(meshchat_t *mc);
peer_t *peer_new(const char *ip);
void peer_send(meshchat_t *mc, peer_t *peer, char *msg, size_t len);
void greet_peer(meshchat_t *mc, peer_t *peer);

void on_irc_msg(void *obj, char *channel, char *data);
void on_irc_privmsg(void *obj, char *channel, char *data);
void on_irc_action(void *obj, char *channel, char *data);
void on_irc_notice(void *obj, char *channel, char *data);
void on_irc_nick(void *obj, char *channel, char *data);
void on_irc_join(void *obj, char *channel, char *data);
void on_irc_part(void *obj, char *channel, char *data);

meshchat_t *meshchat_new() {
    meshchat_t *mc = calloc(1, sizeof(meshchat_t));
    if (!mc) {
        perror("calloc");
        return NULL;
    }

    mc->peers = hash_new();
    if (!mc->peers) {
        free(mc);
        return NULL;
    }

    mc->cjdnsadmin = cjdnsadmin_new();
    if (!mc->cjdnsadmin) {
        free(mc->peers);
        free(mc);
        fprintf(stderr, "fail\n");
        return NULL;
    }

    // add callback for peer discovery through cjdns
    cjdnsadmin_on_found_ip(mc->cjdnsadmin, found_ip, (void *)mc);

    ircd_callbacks_t callbacks = {
        .on_msg     = {mc, on_irc_msg},
        .on_privmsg = {mc, on_irc_privmsg},
        .on_part    = {mc, on_irc_part},
        .on_join    = {mc, on_irc_join},
        .on_nick    = {mc, on_irc_nick},
        .on_action  = {mc, on_irc_action},
        .on_notice  = {mc, on_irc_notice},
    };

    mc->ircd = ircd_new(&callbacks);
    if (!mc->ircd) {
        fprintf(stderr, "fail\n");
        exit(1);
    }

    // todo: allow custom port/hostname
    mc->port = MESHCHAT_PORT;
    //mc->host = "::"; // wildcard

    return mc;
}

void
meshchat_free(meshchat_t *mc) {
    cjdnsadmin_free(mc->cjdnsadmin);
    ircd_free(mc->ircd);
    hash_free(mc->peers);
    free(mc);
}

void
meshchat_start(meshchat_t *mc) {
    // start the local IRC server
    ircd_start(mc->ircd);

    // connect to cjdns admin
    cjdnsadmin_start(mc->cjdnsadmin);

    struct sockaddr *addr = NULL;
    struct sockaddr_in6 *addr6 = NULL;
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) < 0) {
        perror("getifaddrs");
    }

    // find a cjdns-friendly IPv6 address to bind to
    // todo: allow configuring to use a specfic address
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        addr = ifa->ifa_addr;
        if (addr && addr->sa_family == AF_INET6) {
            addr6 = (struct sockaddr_in6 *)addr;
            if (addr6->sin6_addr.s6_addr[0] == 0xfc) {
                break;
            }
        }
    }

    if (!ifa) {
        fprintf(stderr, "Unable to find a cjdns ip.\n");
        exit(1);
    }

    addr6->sin6_port = htons(mc->port);
    //inet_pton(AF_INET6, mc->host, &addr->sin6_addr);

    int fd = socket(AF_INET6, SOCK_DGRAM, 0);
    if (fd==-1) {
        perror("socket");
    }
    mc->listener = fd;

    const char *addrport = sprint_addrport(addr);
    //strcpy(mc->addrport, sprint_addrport(addr));
    if (!inet_ntop(AF_INET6, &addr6->sin6_addr, mc->ip, INET6_ADDRSTRLEN)) {
        perror("inet_ntop");
    } else {
        mc->me = get_peer(mc, mc->ip);
        ircd_set_hostname(mc->ircd, mc->ip);
    }

    if (bind(fd, addr, sizeof(*addr6)) < 0) {
        perror("bind");
        printf("meshchat failed to bind to %s\n", addrport);
    } else {
        printf("meshchat bound to %s\n", addrport);
    }

    freeifaddrs(ifaddr);
}

void
meshchat_add_select_descriptors(meshchat_t *mc, fd_set *in_set,
        fd_set *out_set, int *maxfd) {

    ircd_add_select_descriptors(mc->ircd, in_set, out_set, maxfd);
    cjdnsadmin_add_select_descriptors(mc->cjdnsadmin, in_set, out_set, maxfd);

    int fd = mc->listener;

    FD_SET(fd, in_set);
    if (fd > *maxfd) {
        *maxfd = fd;
    }
}

void
meshchat_process_select_descriptors(meshchat_t *mc, fd_set *in_set,
        fd_set *out_set) {
    static char buffer[MESHCHAT_PACKETLEN];

    ircd_process_select_descriptors(mc->ircd, in_set, out_set);
    cjdnsadmin_process_select_descriptors(mc->cjdnsadmin, in_set, out_set);

    // periodically fetch list of peers to ping
    time_t now;
    time(&now);
    if (difftime(now, mc->last_peerfetch) > MESHCHAT_PEERFETCH_INTERVAL) {
        mc->last_peerfetch = now;
        cjdnsadmin_fetch_peers(mc->cjdnsadmin);
    }

    // periodically ping peers
    if (difftime(now, mc->last_peerservice) > MESHCHAT_PEERSERVICE_INTERVAL) {
        mc->last_peerservice = now;
        service_peers(mc);
    }

    // check if our listener has something
    if (FD_ISSET(mc->listener, in_set)) {
        struct sockaddr_storage src_addr;
        socklen_t src_addr_len = sizeof(src_addr);
        ssize_t count = recvfrom(mc->listener, buffer, sizeof(buffer), 0,
                (struct sockaddr *)&src_addr, &src_addr_len);
        if (count == -1) {
            perror("recvfrom");
        } else if (count == sizeof(buffer)) {
            fprintf(stderr, "datagram too large for buffer: truncated");
        } else {
            handle_datagram(mc, (struct sockaddr *)&src_addr, buffer, count);
        }
    }
}

void
handle_datagram(meshchat_t *mc, struct sockaddr *in, char *msg, size_t len) {
    peer_t *peer;

    if (!hash_size(mc->peers)) {
        // got a message without peers. :(
        return;
    }
    static char ip[INET6_ADDRSTRLEN];

    // convert ip to string
    struct sockaddr_in6 *addr = (struct sockaddr_in6 *)in;
    if (!inet_ntop(AF_INET6, &(addr->sin6_addr), ip, INET6_ADDRSTRLEN)) {
        perror("inet_ntop");
        return;
    }
    peer = get_peer(mc, ip);
    if (!peer) {
        fprintf(stderr, "Unable to handle message from peer %s: \"%s\"\n",
                sprint_addrport(in), msg);
        return;
    }

    const char *channel;
    if (peer->status != PEER_ACTIVE) {
        printf("Peer woke up: %s\n", peer->ip);
    }
    peer->status = PEER_ACTIVE;
    time_t now = time(0);
    peer->last_message = now;
    // first byte is the event type
    //msg[len--] = '\0';
    struct irc_prefix prefix = {
        .nick = peer->nick,
        .user = NULL,
        .host = peer->ip
    };
    switch(*(msg++)) {
        case EVENT_GREETING:
            // nick,channel...
            //printf("got greeting from %s: \"%s\"\n", sprint_addrport(in), msg);

            // note their nick
            if (peer->nick) {
                free(peer->nick);
            }
            peer->nick = strdup(msg);
            if (!peer->nick) {
                fprintf(stderr, "Unable to update nick\n");
            }
            // TODO: add that they are in the given channels

            // respond back if they are new to us
            if (peer->status != PEER_ACTIVE ||
                    difftime(now, peer->last_greeted) > MESHCHAT_PING_INTERVAL) {
                greet_peer(mc, peer);
            }
            break;
        case EVENT_MSG:
            // channel,message
            channel = msg;
            msg += strlen(channel)+1;
            printf("[%s] <%s@%s> \"%s\"\n", channel, prefix.nick, prefix.host, msg);
            ircd_privmsg(mc->ircd, &prefix, channel, msg);
            break;
        case EVENT_PRIVMSG:
            // message
            printf("<%s@%s> \"%s\"\n", prefix.nick, prefix.host, msg);
            ircd_privmsg(mc->ircd, &prefix, prefix.nick, msg);
            break;
        case EVENT_ACTION:
            channel = msg;
            msg += strlen(channel)+1;
            printf("[%s] * %s \"%s\"\n", channel, sprint_addrport(in), msg);
            ircd_action(mc->ircd, &prefix, channel, msg);
            break;
        case EVENT_NOTICE:
            channel = msg;
            msg += strlen(channel)+1;
            printf("[%s] <%s> ! \"%s\"\n", channel, sprint_addrport(in), msg);
            ircd_notice(mc->ircd, &prefix, channel, msg);
            break;
        case EVENT_JOIN:
            // channel
            channel = msg;
            printf("[%s] <%s joined>\n", channel, sprint_addrport(in));
            ircd_join(mc->ircd, &prefix, channel);
            break;
        case EVENT_PART:
            // channel
            channel = msg;
            msg += strlen(channel)+1;
            printf("[%s] <%s parted> (%s)\n", channel, sprint_addrport(in), msg);
            break;
        case EVENT_NICK:
            ircd_nick(mc->ircd, &prefix, msg);
            peer->nick = strdup(msg);
            printf("%s nick: %s\n", sprint_addrport(in), msg);
            break;
    };
}

// lookup a peer by ip, adding it if it is new
peer_t *
get_peer(meshchat_t *mc, const char *ip) {
    peer_t *peer;
    static char ip_copy[INET6_ADDRSTRLEN];

    // canonicalize the ipv6 string
    if (!canonicalize_ipv6(ip_copy, ip)) {
        fprintf(stderr, "Failed to canonicalize ip %s\n", ip);
    }

    //printf("ip: %s, peers: %u\n", ip_copy, hash_size(mc->peers));
    if (hash_size(mc->peers)) {
        peer = hash_get(mc->peers, (char *)ip_copy);
        if (peer) {
            // we have already seen this ip
            return peer;
        }
    }

    // new peer. add to the list
    peer = peer_new(ip_copy);
    if (!peer) {
        fprintf(stderr, "Unable to create peer\n");
        return NULL;
    }
    hash_set(mc->peers, peer->ip, (void *)peer);
    return peer;
}

void
found_ip(void *obj, const char *ip) {
    meshchat_t *mc = (meshchat_t *)obj;
    get_peer(mc, ip);
}

peer_t *
peer_new(const char *ip) {
    peer_t *peer = (peer_t *)malloc(sizeof(peer_t));
    if (!peer) {
        return NULL;
    }
    peer->status = PEER_UNKNOWN;
    peer->last_greeted = -1;
    peer->last_message = -1;
    peer->nick = NULL;
    strcpy(peer->ip, ip);
    memset(&peer->addr, 0, sizeof(peer->addr));
    peer->addr.sin6_family = AF_INET6;
    peer->addr.sin6_port = htons(MESHCHAT_PORT);
    inet_pton(AF_INET6, ip, &(peer->addr.sin6_addr));
    return peer;
}

void
peer_send(meshchat_t *mc, peer_t *peer, char *msg, size_t len) {
    if (sendto(mc->listener, msg, len, 0, (struct sockaddr *)&peer->addr,
                sizeof(peer->addr)) < 0) {
        perror("sendto");
    }
    //printf("sent \"%*s\" (%zu) to %s\n", (int)len-1, msg, len,
        //sprint_addrport((struct sockaddr *)&peer->addr));
}

static inline void
service_peer(meshchat_t *mc, time_t now, peer_t *peer) {
    if (peer == mc->me) {
        return;
    }
    switch (peer->status) {
        // greet new unknown peer
        case PEER_UNKNOWN:
            greet_peer(mc, peer);
            peer->status = PEER_CONTACTED;
            peer->last_message = now;
            break;
        case PEER_CONTACTED:
        case PEER_ACTIVE:
            if (difftime(now, peer->last_greeted) > MESHCHAT_PING_INTERVAL) {
                // ping active peer
                greet_peer(mc, peer);
            }
            if (difftime(now, peer->last_message) > MESHCHAT_TIMEOUT) {
                // mark unreponsive peer as timed out
                peer->status = PEER_INACTIVE;
            }
            break;
        case PEER_INACTIVE:
            // greet inactive peer after a while
            if (difftime(now, peer->last_greeted) > MESHCHAT_RETRY_INTERVAL) {
                greet_peer(mc, peer);
                peer->status = PEER_CONTACTED;
            }
            break;
    }
}

static inline void
broadcast_all_peer(meshchat_t *mc, peer_t *peer, char *msg, size_t len) {
    // send only to active peer
    if (peer->status == PEER_ACTIVE) {
        peer_send(mc, peer, msg, len);
        printf("sending (%s) %s to %s\n", event_names[(int)msg[0]], msg+1, peer->ip);
    }
}

// send a message to all active peers
void
broadcast_all(meshchat_t *mc, char *msg, size_t len) {
    hash_each_val(mc->peers, broadcast_all_peer(mc, val, msg, len));
}

// send a message to all active peers in a channel
void
broadcast_channel(meshchat_t *mc, char *channel, void *msg, size_t len) {
    // todo: broadcast only to channel
    broadcast_all(mc, msg, len);
    //hash_each_val(mc->peers, broadcast_active_peer(mc, val, msg, len));
}

void
service_peers(meshchat_t *mc) {
    //printf("servicing peers (%u)\n", hash_size(mc->peers));
    time_t now;
    time(&now);
    hash_each_val(mc->peers, service_peer(mc, now, val));
}

void
greet_peer(meshchat_t *mc, peer_t *peer) {
    static char msg[MESHCHAT_PACKETLEN];
    //printf("greeting peer %s\n", peer->ip);
    //const char *rooms = "#rochack";
    // TODO: list rooms here
    msg[0] = EVENT_GREETING;
    strncpy(msg+1, mc->nick, sizeof(mc->nick));
    peer_send(mc, peer, msg, strlen(msg)+1);
    //printf("msg: [%d] %s\n", msg[0], msg+1);
    time(&peer->last_greeted);
}

void
broadcast_event(meshchat_t *mc, enum event_type ev, int argv, ...) {
    static char msg[MESHCHAT_PACKETLEN];
    int i, offset = 1;
    msg[0] = ev;
    va_list ap;
    char *arg;

    va_start(ap, argv);
    for (i = 0; i < argv; i++) {
        arg = va_arg(ap, char *);
        size_t len = strlen(arg)+1;
        if (len + offset > MESHCHAT_PACKETLEN) {
            fprintf(stderr, "truncated message\n");
            break;
        }
        strncpy(msg + offset, arg, len);
        offset += len;
    }
    va_end(ap);
    broadcast_all(mc, msg, offset);
}

void
on_irc_msg(void *obj, char *channel, char *data) {
    meshchat_t *mc = (meshchat_t *)obj;
    broadcast_event(mc, EVENT_MSG, 2, channel, data);
}

void
on_irc_action(void *obj, char *channel, char *data) {
    meshchat_t *mc = (meshchat_t *)obj;
    broadcast_event(mc, EVENT_ACTION, 2, channel, data);
}

void
on_irc_notice(void *obj, char *channel, char *data) {
    meshchat_t *mc = (meshchat_t *)obj;
    broadcast_event(mc, EVENT_NOTICE, 2, channel, data);
}

void
on_irc_privmsg(void *obj, char *recipient, char *data) {
    /*
    meshchat_t *mc = (meshchat_t *)obj;
    static char msg[MESHCHAT_PACKETLEN];
    msg[0] = EVENT_PRIVMSG;
    strncpy(msg+1, data, sizeof(msg)-1);
    // todo: look up peer
    //peer = 
    //peer_send(mc, peer, msg);
    */
}

void
on_irc_join(void *obj, char *channel, char *nick) {
    meshchat_t *mc = (meshchat_t *)obj;
    broadcast_event(mc, EVENT_JOIN, 2, channel, nick);
}

void
on_irc_part(void *obj, char *channel, char *data) {
    meshchat_t *mc = (meshchat_t *)obj;
    broadcast_event(mc, EVENT_PART, 2, channel, data);
}

void
on_irc_nick(void *obj, char *channel, char *data) {
    meshchat_t *mc = (meshchat_t *)obj;
    broadcast_event(mc, EVENT_NICK, 1, data);
    strcpy(mc->nick, data);
}
