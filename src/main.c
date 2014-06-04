/* vim: set expandtab ts=4 sw=4: */
/*
 * main.c
 */

#include <uv.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include "meshchat.h"
#include "ircd.h"

int main()
//int main(int argc, char *argv[])
{
    meshchat_t *mc;

    mc = meshchat_new();
    if (!mc) {
        fprintf(stderr, "fail\n");
        exit(1);
    }

    // Start connecting stuff
    meshchat_start(mc);

    uv_run(uv_default_loop(),UV_RUN_DEFAULT);

    return 0;
}

