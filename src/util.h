/* vim: set expandtab ts=4 sw=4: */
/*
 * meshchat.h
 */

#ifndef UTIL_H
#define UTIL_H

const char *sprint_addrport(struct sockaddr *addr);
void strwncpy(char *dst, const char *src, size_t max);

int canonicalize_ipv6(char *dest, const char *src);

#endif /* UTIL_H */

