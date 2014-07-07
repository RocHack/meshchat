/* vim: set expandtab ts=4 sw=4: */
/*
 * meshchat.h
 */

#ifndef MESHCHAT_H
#define MESHCHAT_H

#include <sys/select.h> // fd_set

#define MESHCHAT_MESSAGE_LEN 512
#define MESHCHAT_CHANNEL_LEN 50
#define MESHCHAT_NAME_LEN 9
#define MESHCHAT_FULLNAME_LEN 32
#define MESHCHAT_HOST_LEN 63

typedef struct meshchat meshchat_t;
typedef struct peer peer_t;

enum peer_status {
    PEER_UNKNOWN,   // we haven't contacted them
    PEER_CONTACTED, // we have sent a greeting
    PEER_ACTIVE,    // they have sent us a greeting recently
    PEER_INACTIVE, // they have not sent us a greeting recently
};

meshchat_t *meshchat_new();

void meshchat_free(meshchat_t *mc);

void meshchat_start(meshchat_t *mc);

void meshchat_add_select_descriptors(meshchat_t *mc, fd_set *in_set,
        fd_set *out_set, int *maxfd);

void meshchat_process_select_descriptors(meshchat_t *mc, fd_set *in_set,
        fd_set *out_set);

#endif /* MESHCHAT_H */
