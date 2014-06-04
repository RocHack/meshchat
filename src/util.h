/* vim: set expandtab ts=4 sw=4: */
/*
 * meshchat.h
 */

#ifndef UTIL_H
#define UTIL_H

const char *sprint_addrport(const struct sockaddr *addr);
int strwncpy(char *dst, const char *src, size_t max);

void current_clock(struct timespec* a); // do we need the current time, or is relative good?
double time_since(struct timespec* event);

int canonicalize_ipv6(char *dest, const char *src);

#define NEW(type) ((type*)malloc(sizeof(type)))
#define AMNEW(type,name) type* name = NEW(type)

#define ZERO(obj) memset(&obj,0,sizeof(obj))

#define GETDATA(type,name,handle) type* name = (type*) handle->data

#define CHECK(status) if(status < 0) { fprintf(stderr,"error: %s %s\n",uv_strerror(status),uv_err_name(status)); exit(23); }

#endif /* UTIL_H */

