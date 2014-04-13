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
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "ircd.h"
#include "meshchat.h"
#include "cjdnsadmin.h"

#define MESHCHAT_PORT "14627"
#define MESHCHAT_PACKETLEN 1400

struct meshchat {
	ircd_t *ircd;
	cjdnsadmin_t *cjdnsadmin;
	const char *host;
	const char *port;
	int listener;
};

void handle_datagram(char *buffer, ssize_t len);

meshchat_t *meshchat_new() {
	meshchat_t *mc = calloc(1, sizeof(meshchat_t));
	if (!mc) {
		perror("calloc");
		return NULL;
	}

	mc->cjdnsadmin = cjdnsadmin_new();
	if (!mc->cjdnsadmin) {
		free(mc);
		fprintf(stderr, "fail\n");
		return NULL;
	}

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

	printf("meshchat bound to [%s]:%s\n", addr_str, mc->port);

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

	// check if our listener has something
	if (!FD_ISSET(mc->listener, in_set)) {
		return;
	}

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

void
handle_datagram(char *buffer, ssize_t len) {
	printf("got message: \"%*s\"\n", (int)len, buffer);
}
