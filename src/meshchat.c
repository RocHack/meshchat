/* vim: set expandtab ts=4 sw=4: */
/*
 * meshchat.c
 */

#include <uv.h>

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
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
#define MESHCHAT_PEERFETCH_INTERVAL 600
#define MESHCHAT_PEERSERVICE_INTERVAL 60
#define MESHCHAT_TIMEOUT 60
#define MESHCHAT_PING_INTERVAL 20
#define MESHCHAT_RETRY_INTERVAL 900

struct meshchat {
    ircd_t *ircd;
    cjdnsadmin_t *cjdnsadmin;
    //const char *host;
    int port;
    uv_udp_t handle;
    void* buffer;
    char ip[INET6_ADDRSTRLEN];
    struct timespec last_peerfetch;
    struct timespec last_peerservice;
    hash_t *peers;
    char nick[MESHCHAT_NAME_LEN]; // our node's nick
    struct peer *me;
};

struct peer {
    char ip[40];
    struct sockaddr_in6 addr;
    enum peer_status status;
    struct timespec last_message;    // they sent to us
    struct timespec last_greeted;    // we sent to them
    char *nick;
};

enum event_type {
    EVENT_GREETING = 1,
    EVENT_MSG,
    EVENT_NOTICE,
    EVENT_JOIN,
    EVENT_PART,
    EVENT_NICK,
};

const char *event_names[] = {
    NULL,
    "greeting",
    "msg",
    "action",
    "notice",
    "join",
    "part",
    "nick"
};

static void
handle_datagram(uv_udp_t* handle,
        ssize_t nread,
        const uv_buf_t* buf,
        const struct sockaddr* in,
        unsigned flags);

peer_t *get_peer(meshchat_t *mc, const char *ip);
static void found_ip(void *obj, const char *ip);
static void service_peers(uv_timer_t *timer);
peer_t *peer_new(const char *ip);
void peer_send(meshchat_t *mc, peer_t *peer, char *msg, size_t len);
void greet_peer(meshchat_t *mc, peer_t *peer);

void on_irc_msg(void *obj, char *channel, char *data);
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
        .on_part    = {mc, on_irc_part},
        .on_join    = {mc, on_irc_join},
        .on_nick    = {mc, on_irc_nick},
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

static void fetch_peers(uv_timer_t* timer) {
    cjdnsadmin_fetch_peers((cjdnsadmin_t*)timer->data);
}

void alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    meshchat_t* mc = (meshchat_t*)handle->data;
    buf->base = mc->buffer = realloc(mc->buffer,suggested_size);
    buf->len = suggested_size;
}

void
meshchat_start(meshchat_t *mc) {
    // start the local IRC server
    ircd_start(mc->ircd);

    // connect to cjdns admin
    cjdnsadmin_start(mc->cjdnsadmin);

    uv_udp_init(uv_default_loop(),&mc->handle);

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

    const char *addrport = sprint_addrport(addr);
    //strcpy(mc->addrport, sprint_addrport(addr));
    if (!inet_ntop(AF_INET6, &addr6->sin6_addr, mc->ip, INET6_ADDRSTRLEN)) {
        perror("inet_ntop");
    } else {
        mc->me = get_peer(mc, mc->ip);
        ircd_set_hostname(mc->ircd, mc->ip);
    }

    if (uv_udp_bind(&mc->handle, addr, UV_UDP_IPV6ONLY | UV_UDP_REUSEADDR) < 0) {
        perror("bind");
        printf("meshchat failed to bind to %s\n", addrport);
    } else {
        printf("meshchat bound to %s\n", addrport);
    }

    freeifaddrs(ifaddr);

    // periodically fetch list of peers to ping
    uv_timer_t* timer = malloc(sizeof(uv_timer_t));
    timer->data = mc->cjdnsadmin;
    uv_timer_init(uv_default_loop(),timer);
    uv_timer_start(timer, fetch_peers, 
            0, 1000 * MESHCHAT_PEERFETCH_INTERVAL);

    // never stopping the timer, so forget about freeing it.

    timer = malloc(sizeof(uv_timer_t));
    timer->data = mc;
    uv_timer_init(uv_default_loop(),timer);
    uv_timer_start(timer,service_peers,
            0 , 1000 * MESHCHAT_PEERSERVICE_INTERVAL);

    mc->handle.data = mc;

    uv_udp_recv_start(&mc->handle, alloc_cb, handle_datagram);
    // handle_datagram(mc, (struct sockaddr *)&src_addr, buffer, count);
}

static void
handle_datagram(uv_udp_t* handle,
        ssize_t nread,
        const uv_buf_t* buf,
        const struct sockaddr* in,
        unsigned flags) {

    meshchat_t *mc = handle->data;
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
                sprint_addrport(in), buf->base);
        return;
    }

    const char *channel;
    if (peer->status != PEER_ACTIVE) {
        //printf("Peer woke up: %s\n", peer->ip);
        // TODO: add the peer back to their channels
    }
    peer->status = PEER_ACTIVE;
    current_clock(&peer->last_message);
    // first byte is the event type
    //msg[len--] = '\0';
    struct irc_prefix prefix = {
        .nick = peer->nick,
        .user = NULL,
        .host = peer->ip
    };
    const char* msg = buf->base;
    switch(*(msg++)) {
        case EVENT_GREETING:
            // nick,channel...
            //printf("got greeting from %s: \"%s\"\n", sprint_addrport(in), msg);

            // note their nick
            ;
            size_t nick_len = strlen(msg) + 1;
            if (!nick_len) break;
            if (peer->nick) {
                free(peer->nick);
            }
            peer->nick = strndup(msg, nick_len);
            prefix.nick = peer->nick;
            if (!peer->nick) {
                perror("Unable to update nick. strndup");
                break;
            }

            // add that they are in the given channels
            for (channel = msg + nick_len;
                    channel - msg < buf->len && channel[0];
                    channel += strlen(channel) + 1) {
                ircd_join(mc->ircd, &prefix, channel);
            }

            // respond back if they are new to us
            if (peer->status != PEER_ACTIVE ||
                    time_since(&peer->last_greeted) > MESHCHAT_PING_INTERVAL) {
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
            ircd_part(mc->ircd, &prefix, channel, NULL);
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
    ZERO(peer->last_greeted);
    ZERO(peer->last_message);
    peer->nick = NULL;
    strcpy(peer->ip, ip);
    memset(&peer->addr, 0, sizeof(peer->addr));
    peer->addr.sin6_family = AF_INET6;
    peer->addr.sin6_port = htons(MESHCHAT_PORT);
    inet_pton(AF_INET6, ip, &(peer->addr.sin6_addr));
    return peer;
}

void on_sent(uv_udp_send_t* sent, int status) {
    CHECK(status);
    //printf("sent \"%*s\" (%zu) to %s\n", (int)len-1, msg, len,
        //sprint_addrport((struct sockaddr *)&peer->addr));
    free(sent);
}

void
peer_send(meshchat_t *mc, peer_t *peer, char *msg, size_t len) {
    // probably need a uv_udp_send_t for each send, so can send to multiple peers
    // without getting addresses mixed up? XXX: maybe not?
    
    uv_udp_send_t* req = NEW(uv_udp_send_t);
    uv_buf_t buf;
    buf.base = msg;
    buf.len = len;
    uv_udp_send(req,&mc->handle, &buf, 1, (struct sockaddr *)&peer->addr, on_sent);
}

static inline void
service_peer(meshchat_t *mc, peer_t *peer) {
    if (peer == mc->me) {
        return;
    }
    switch (peer->status) {
        // greet new unknown peer
        case PEER_UNKNOWN:
            greet_peer(mc, peer);
            peer->status = PEER_CONTACTED;
            current_clock(&peer->last_message);
            break;
        case PEER_CONTACTED:
        case PEER_ACTIVE:
            if (time_since(&peer->last_greeted) > MESHCHAT_PING_INTERVAL) {
                // ping active peer
                greet_peer(mc, peer);
            }
            if (time_since(&peer->last_message) > MESHCHAT_TIMEOUT) {
                // mark unreponsive peer as timed out
                if (peer->status == PEER_ACTIVE) {
                    // tell irc that they are gone
                    struct irc_prefix prefix = {
                        .nick = peer->nick,
                        .user = NULL,
                        .host = peer->ip
                    };
                    ircd_quit(mc->ircd, &prefix, "Timed out");
                }
                peer->status = PEER_INACTIVE;
            }
            break;
        case PEER_INACTIVE:
            // greet inactive peer after a while
            if (time_since(&peer->last_greeted) > MESHCHAT_RETRY_INTERVAL) {
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
service_peers(uv_timer_t* handle) {
    meshchat_t *mc = handle->data;
    //printf("servicing peers (%u)\n", hash_size(mc->peers));
    hash_each_val(mc->peers, service_peer(mc, val));
}

void
greet_peer(meshchat_t *mc, peer_t *peer) {
    static char msg[MESHCHAT_PACKETLEN];
    size_t len = 1;
    //printf("greeting peer %s\n", peer->ip);
    msg[0] = EVENT_GREETING;
    // format: nick,channels...
    size_t nick_len = strlen(mc->nick) + 1;
    strncpy(msg + 1, mc->nick, nick_len);
    len += nick_len;
    len += ircd_get_channels(mc->ircd, msg + len, sizeof(msg) - len);
    peer_send(mc, peer, msg, len);
    current_clock(&peer->last_greeted);
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
    // TODO: only send the event to peers whom we think are in the channel
}

void
on_irc_notice(void *obj, char *channel, char *data) {
    meshchat_t *mc = (meshchat_t *)obj;
    broadcast_event(mc, EVENT_NOTICE, 2, channel, data);
}

// client joined a channel
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
