/*
 * ircd.h
 */

#ifndef IRCD_H
#define IRCD_H

typedef struct ircd ircd_t;

ircd_t *ircd_new();

void ircd_free();

#endif /* IRCD_H */
