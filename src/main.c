/* vim: set expandtab ts=4 sw=4: */
/*
 * main.c
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/time.h>
#include "meshchat.h"
#include "ircd.h"

int main()
//int main(int argc, char *argv[])
{
    meshchat_t *mc;
    struct timeval tv;
    fd_set in_set, out_set;
    int maxfd = 0;

    mc = meshchat_new();
    if (!mc) {
        fprintf(stderr, "fail\n");
        exit(1);
    }

    // Start connecting stuff
    meshchat_start(mc);

    while (1) {

        // Initialize the sets
        FD_ZERO (&in_set);
        FD_ZERO (&out_set);

        // Wait 1 sec for the events.
        tv.tv_usec = 0;
        tv.tv_sec = 1;

        meshchat_add_select_descriptors(mc, &in_set, &out_set, &maxfd);

        // Call select()
        if (select(maxfd + 1, &in_set, &out_set, 0, &tv) < 0) {
            perror("select");
        }

        meshchat_process_select_descriptors(mc, &in_set, &out_set);
    }

    return 0;
}

