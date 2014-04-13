/* vim: set expandtab ts=4 sw=4: */
/*
 * cjdnsadmin.c
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "cjdnsadmin.h"

#define CJDNSADMIN_PORT "11234"
#define CJDNSADMIN_HOST "127.0.0.1"

struct cjdnsadmin {
    int fd;
    const char *host;
    const char *port;
};

cjdnsadmin_t *cjdnsadmin_new() {
    cjdnsadmin_t *adm = calloc(1, sizeof(cjdnsadmin_t));
    if (!adm) {
        perror("calloc");
        return NULL;
    }

    adm->port = CJDNSADMIN_PORT;
    adm->host = CJDNSADMIN_HOST;

    return adm;
}

void cjdnsadmin_free(cjdnsadmin_t *adm) {
    free(adm);
}

void cjdnsadmin_start(cjdnsadmin_t *adm) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE|AI_ADDRCONFIG;
    struct addrinfo* res = 0;
    int err = getaddrinfo(adm->host, adm->port, &hints, &res);
    if (err != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(err));
        return;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd == -1) {
        perror("socket");
    }
    adm->fd = fd;

    if (bind(fd, res->ai_addr, res->ai_addrlen) < 0) {
        perror("bind");
    }

    struct sockaddr_in *addr = (struct sockaddr_in *)res->ai_addr;
    char addr_str[INET6_ADDRSTRLEN];
    if (!inet_ntop(AF_INET, &(addr->sin_addr), addr_str, INET_ADDRSTRLEN)) {
        perror("inet_ntop");
        addr_str[0] = '\0';
    }

    printf("connected to cjdns admin at %s:%s\n", addr_str, adm->port);
}

void cjdnsadmin_fetch_peers(cjdnsadmin_t *adm, on_fetch_peers_t *cb, void *obj)
{

}

void cjdnsadmin_add_select_descriptors(cjdnsadmin_t *adm, fd_set *in_set,
        fd_set *out_set, int *maxfd) {
}

void cjdnsadmin_process_select_descriptors(cjdnsadmin_t *adm, fd_set *in_set,
        fd_set *out_set) {
}

