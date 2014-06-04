/* vim: set expandtab ts=4 sw=4: */
/*
 * cjdnsadmin.c
 */

#include <uv.h>

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
    uv_udp_t handle;
    void* buffer;

    const char *host;
    const char *port;

    int fetch_peers_page;

    struct sockaddr* theaddr;

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


void on_read(uv_udp_t* stream, ssize_t nread, const uv_buf_t* buf,
        const struct sockaddr* addr, unsigned flags) {
    if(nread < 0) {
        perror("uh");
        fprintf(stderr,"error: %s %s %ld %d\n",uv_strerror(nread),uv_err_name(nread),nread,EOF);
        exit(99);
    }
    if(nread == 0) return;
    fprintf(stderr,"read %lu\n",nread);
    handle_message(stream->data,buf->base,nread);
}

static void alloc_buffer(uv_handle_t* handle, size_t suggested, uv_buf_t* buf) {
    cjdnsadmin_t* adm = handle->data;
    buf->base = adm->buffer = realloc(adm->buffer, suggested);
    buf->len = suggested;
}  


void cjdnsadmin_start(cjdnsadmin_t *adm) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_ADDRCONFIG;
    struct addrinfo* res = 0;
    printf("Connecting to %s:%s\n",adm->host,adm->port);
    int err = getaddrinfo(adm->host, adm->port, &hints, &res);
    if (err != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(err));
        return;
    }

    uv_udp_init(uv_default_loop(),&adm->handle);
    adm->handle.data = adm;

    uv_connect_t* request = malloc(sizeof(uv_connect_t));
    request->data = res;

    printf("connected to cjdns admin at %s\n", sprint_addrport(res->ai_addr));

    adm->theaddr = (struct sockaddr*) malloc(res->ai_addrlen);
    memcpy(adm->theaddr, res->ai_addr, res->ai_addrlen);

    freeaddrinfo(res);
    uv_udp_recv_start(&adm->handle,alloc_buffer,on_read);
}

static void on_written(uv_udp_send_t* req, int status) {
    CHECK(status);
    struct bencode* b = req->data;
    ben_free(b);
}

void cjdnsadmin_fetch_peers(cjdnsadmin_t *adm)
{
    struct bencode *b = ben_dict();
    struct bencode *args = ben_dict();
    // TODO: fix memory leak
    ben_dict_set(b, ben_str("q"), ben_str("NodeStore_dumpTable"));
    ben_dict_set(b, ben_str("args"), args);
    ben_dict_set(args, ben_str("page"), ben_int(adm->fetch_peers_page));

    uv_buf_t buf;
    static char msg[256];
    buf.base = msg;
    buf.len = ben_encode2(msg, sizeof msg, b);

    AMNEW(uv_udp_send_t,writer);
    writer->data = b;
    uv_udp_send(writer,
            &adm->handle,
            &buf,1,
            adm->theaddr,
            on_written);

}

void handle_message(cjdnsadmin_t *adm, char *buffer, ssize_t len) {
    // TODO: fix memory leak
    struct bencode *b = ben_decode(buffer, len);
    if (!b) {
        fprintf(stderr, "bencode error: %lu\n",len);
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
    int more_int = more && ben_is_int(more) && ben_int_val(more);
    if (more_int == 1) {
        // get the next page of the routing table
        adm->fetch_peers_page++;
        cjdnsadmin_fetch_peers(adm);
    } else {
        // start from the first page next time
        adm->fetch_peers_page = 0;
    }
    ben_free(b);
}

void
cjdnsadmin_on_found_ip(cjdnsadmin_t *adm, on_found_ip_t cb, void *obj) {
    adm->on_found_ip = cb;
    adm->on_found_ip_obj = obj;
}
