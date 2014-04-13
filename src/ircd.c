/*
 * ircd.c
 */

#include <stdlib.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include "ircd.h"

struct ircd {
    // listener socket (TCP accept())
    int listener;
    //
};

ircd_t *ircd_new() {
	ircd_t *ircd = calloc(1, sizeof(ircd_t));
	if (!ircd) {
		perror("calloc");
		return NULL;
	}

	return ircd;
}

void
ircd_free(ircd_t *ircd) {
}

void
ircd_start(ircd_t *ircd) {
    struct addrinfo hints;
    struct addrinfo *result;

    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0;
    // rest 0

    int status = getaddrinfo(NULL, "ircd", &hints, &result);
    if (!status) {
        printf("getaddrinfo: ");
        puts(gai_strerror(status));
        return;
    }

    ircd->listener = socket(AF_INET6, SOCK_STREAM, 0);
    if (ircd->listener < 0) {
        perror("socket");
        return;
    }

    if (bind(ircd->listener, result->ai_addr, result->ai_addrlen) < 0) {
        perror("bind");
        return;
    }

    if (listen(ircd->listener, IRCD_BACKLOG) > 0) {
        perror("listen");
        return;
    }
}

void
ircd_add_select_descriptors(ircd_t *mc, fd_set *in_set,
		fd_set *out_set, int *maxfd) {
}

void
ircd_process_select_descriptors(ircd_t *mc, fd_set *in_set,
		fd_set *out_set) {
}
