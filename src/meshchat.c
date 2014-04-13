/* vim: set expandtab ts=4 sw=4: */
/*
 * meshchat.c
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include "ircd.h"
#include "hash/hash.h"
#include "meshchat.h"
#include "cjdnsadmin.h"
#include "util.h"

#define MESHCHAT_PORT "14627"
#define MESHCHAT_PACKETLEN 1400
#define MESHCHAT_PEERFETCH_INTERVAL 60
#define MESHCHAT_PEERSERVICE_INTERVAL 5
#define MESHCHAT_TIMEOUT 60
#define MESHCHAT_PING_INTERVAL 20
#define MESHCHAT_RETRY_INTERVAL 600

struct meshchat {
    ircd_t *ircd;
    cjdnsadmin_t *cjdnsadmin;
    const char *host;
    const char *port;
    int listener;
    time_t last_peerfetch;
    time_t last_peerservice;
    hash_t *peers;
};

struct peer {
    char ip[40];
    //sockaddr_t 
    enum peer_status status;
    time_t last_greeting;   // they sent to us
    time_t last_greeted;    // we sent to them
};

void handle_datagram(char *buffer, ssize_t len);
void found_ip(void *obj, const char *ip);
void service_peers(meshchat_t *mc);
peer_t *peer_new(const char *ip);
void peer_send(peer_t *peer, char *msg, size_t bytes);
void greet_peer(meshchat_t *mc, peer_t *peer);

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

    mc->ircd = ircd_new();
    if (!mc->ircd) {
        fprintf(stderr, "fail\n");
        exit(1);
    }

    // todo: allow custom port/hostname
    mc->port = MESHCHAT_PORT;
    mc->host = 0; // wildcard

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

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE|AI_ADDRCONFIG;
    struct addrinfo* res = 0;
    int err = getaddrinfo(mc->host, mc->port, &hints, &res);
    if (err != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(err));
        return;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd==-1) {
        perror("socket");
    }
    mc->listener = fd;

    if (bind(fd, res->ai_addr, res->ai_addrlen) < 0) {
        perror("bind");
    }

    struct sockaddr_in6 *addr = (struct sockaddr_in6 *)res->ai_addr;
    char addr_str[INET6_ADDRSTRLEN];
    if (!inet_ntop(AF_INET6, &(addr->sin6_addr), addr_str, INET6_ADDRSTRLEN)) {
        perror("inet_ntop");
        addr_str[0] = '\0';
    }

    printf("meshchat bound to %s\n", sprint_addrport(res->ai_addr));

    freeaddrinfo(res);
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
            handle_datagram(buffer, count);
        }
    }
}

void
handle_datagram(char *buffer, ssize_t len) {
    printf("got message: \"%*s\"\n", (int)len, buffer);
}

void
found_ip(void *obj, const char *ip) {
    meshchat_t *mc = (meshchat_t *)obj;
    //printf("ip: %s, peers: %u\n", ip, hash_size(mc->peers));
    if (hash_size(mc->peers) && hash_has(mc->peers, (char *)ip)) {
        // we have already seen this ip
        return;
    }

    // restrict the network to a small collection of peers, for testing
    if (1 &&
#define IP(addr) strcmp(ip, addr) &&
#include "mynetwork.def"
    1) {
        return;
    }

    // new peer. add to the list
    peer_t *peer = peer_new(ip);
    if (!peer) {
        fprintf(stderr, "Unable to create peer\n");
        return;
    }
    hash_set(mc->peers, (char *)ip, (void *)peer);
    //printf("ip: %s\n", ip);
}

peer_t *
peer_new(const char *ip) {
    peer_t *peer = (peer_t *)malloc(sizeof(peer_t));
    if (peer) {
        peer->status = PEER_UNKNOWN;
        peer->last_greeted = -1;
        peer->last_greeting = -1;
        strcpy(peer->ip, ip);
    }
    return peer;
}

void
peer_send(peer_t *peer, char *msg, size_t bytes) {
}

static inline void
service_peer(meshchat_t *mc, time_t now, const char *key, peer_t *peer) {
    //peer_print(peer);
    switch (peer->status) {
        // greet new unknown peer
        case PEER_UNKNOWN:
            greet_peer(mc, peer);
            peer->status = PEER_CONTACTED;
            break;
        case PEER_CONTACTED:
        case PEER_ACTIVE:
            if (difftime(now, peer->last_greeted) > MESHCHAT_PING_INTERVAL) {
                // ping active peer
                greet_peer(mc, peer);
            }
            if (difftime(now, peer->last_greeting) > MESHCHAT_TIMEOUT) {
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

void
service_peers(meshchat_t *mc) {
    //printf("servicing peers (%u)\n", hash_size(mc->peers));
    time_t now;
    time(&now);
    hash_each(mc->peers, service_peer(mc, now, key, val));
}

void
greet_peer(meshchat_t *mc, peer_t *peer) {
    char *msg = "hello";
    //printf("greeting peer %s\n", peer->ip);
    peer_send(peer, msg, strlen(msg)+1);
    time(&peer->last_greeted);
}
