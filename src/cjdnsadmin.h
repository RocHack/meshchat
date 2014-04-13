/* vim: set expandtab ts=4 sw=4: */
/*
 * cjdnsadmin.h
 */

#ifndef CJDNSADMIN_H
#define CJDNSADMIN_H

typedef struct cjdnsadmin cjdnsadmin_t;

typedef void (*on_found_ip_t) (void *obj, const char *ip);

cjdnsadmin_t *cjdnsadmin_new();

void cjdnsadmin_free(cjdnsadmin_t *adm);

void cjdnsadmin_start(cjdnsadmin_t *adm);

void cjdnsadmin_fetch_peers(cjdnsadmin_t *adm);

void cjdnsadmin_add_select_descriptors(cjdnsadmin_t *adm, fd_set *in_set,
        fd_set *out_set, int *maxfd);

void cjdnsadmin_process_select_descriptors(cjdnsadmin_t *adm, fd_set *in_set,
        fd_set *out_set);

void cjdnsadmin_on_found_ip(cjdnsadmin_t *adm, on_found_ip_t cb, void *obj);

#endif /* CJDNSADMIN_H */
