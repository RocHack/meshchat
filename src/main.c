/*
 * main.c
 */

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "meshchat.h"
#include "ircd.h"

int main()
{
	meshchat_t *mc = meshchat_new();
	if (!mc) {
		fprintf(stderr, "fail\n");
		exit(1);
	}

	ircd_t *ircd = ircd_new();
	if (!ircd) {
		fprintf(stderr, "fail\n");
		exit(1);
	}

	return 0;
}

