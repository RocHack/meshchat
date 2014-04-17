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
#include <sys/select.h>
#include "bencode/bencode.h"
#include "cjdnsadmin.h"
#include "util.h"

#define CJDNSADMIN_PORT "11234"
#define CJDNSADMIN_HOST "127.0.0.1"

struct cjdnsadmin {
    int fd;
    const char *host;
    const char *port;

    int fetch_peers_page;

    on_found_ip_t on_found_ip;
    void *on_found_ip_obj;
};

void handle_message(cjdnsadmin_t *adm, char *buffer, ssize_t len);

cjdnsadmin_t *cjdnsadmin_new() {
    cjdnsadmin_t *adm = calloc(1, sizeof(cjdnsadmin_t));
    if (!adm) {
        perror("calloc");
        return NULL;
    }

    adm->port = CJDNSADMIN_PORT;
    adm->host = CJDNSADMIN_HOST;
    adm->fetch_peers_page = 0;

    return adm;
}

void cjdnsadmin_free(cjdnsadmin_t *adm) {
    free(adm);
}

void cjdnsadmin_start(cjdnsadmin_t *adm) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_ADDRCONFIG;
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

    if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        perror("bind");
    }

    printf("connected to cjdns admin at %s\n", sprint_addrport(res->ai_addr));
}

void cjdnsadmin_fetch_peers(cjdnsadmin_t *adm)
{
    static char msg[256];
    size_t len;
    struct bencode *b = ben_dict();
    struct bencode *args = ben_dict();
    ben_dict_set(b, ben_str("q"), ben_str("NodeStore_dumpTable"));
    ben_dict_set(b, ben_str("args"), args);
    ben_dict_set(args, ben_str("page"), ben_int(adm->fetch_peers_page));

    len = ben_encode2(msg, sizeof msg, b);

    int bytes = send(adm->fd, msg, len, 0);
    if (bytes == -1) {
        perror("send");
    } else if (bytes != len) {
        fprintf(stderr, "partial send: %d/%zd", bytes, len);
    }

    ben_free(b);
}

void cjdnsadmin_add_select_descriptors(cjdnsadmin_t *adm, fd_set *in_set,
        fd_set *out_set, int *maxfd) {
    int fd = adm->fd;

    FD_SET(fd, in_set);
    if (fd > *maxfd) {
        *maxfd = fd + 1;
    }
}

void cjdnsadmin_process_select_descriptors(cjdnsadmin_t *adm, fd_set *in_set,
        fd_set *out_set) {
    static char buffer[1024];

    // check if our listener has something
    if (!FD_ISSET(adm->fd, in_set)) {
        return;
    }

    ssize_t size = recv(adm->fd, buffer, sizeof(buffer), 0);
    if (size == -1) {
        perror("recvfrom");
    } else if (size == sizeof(buffer)) {
        fprintf(stderr, "datagram too large for buffer: truncated");
    } else {
        handle_message(adm, buffer, size);
    }
}

void handle_message(cjdnsadmin_t *adm, char *buffer, ssize_t len) {
    struct bencode *b = ben_decode(buffer, len);
    if (!b) {
        fprintf(stderr, "bencode error:\n");
        printf("message from cjdns: \"%*s\"\n", (int)len, buffer);
        return;
    }
    // Get IPs
    struct bencode *table = ben_dict_get_by_str(b, "routingTable");
    size_t i, num_items = ben_list_len(table);
    for (i = 0; i < num_items; i++) {
        struct bencode *item = ben_list_get(table, i);
        struct bencode *ip = ben_dict_get_by_str(item, "ip");
        if (ben_is_str(ip)) {
            const char *ip_str = ben_str_val(ip);
            if (adm->on_found_ip) {
                (*adm->on_found_ip)(adm->on_found_ip_obj, ip_str);
            }
        }
    }
    // check if there is more
    struct bencode *more = ben_dict_get_by_str(b, "more");
    if (more && ben_cmp(more, ben_int(1)) == 0) {
        // get the next page of the routing table
        adm->fetch_peers_page++;
        cjdnsadmin_fetch_peers(adm);
    } else {
        // start from the first page next time
        adm->fetch_peers_page = 0;
    }
    free(b);
}

void
cjdnsadmin_on_found_ip(cjdnsadmin_t *adm, on_found_ip_t cb, void *obj) {
    adm->on_found_ip = cb;
    adm->on_found_ip_obj = obj;
}
