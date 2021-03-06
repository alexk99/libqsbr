/*
 * Copyright (c) 2015 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#ifndef	_QSBR_H_
#define	_QSBR_H_

#include <sys/cdefs.h>
#include <stdbool.h>

struct qsbr;
typedef struct qsbr qsbr_t;
typedef unsigned long qsbr_epoch_t;

__BEGIN_DECLS

qsbr_t *	qsbr_create(void);
void		qsbr_destroy(qsbr_t *);

void		qsbr_unregister(qsbr_t *);
int		qsbr_register(qsbr_t *);
void		qsbr_checkpoint(qsbr_t *);
qsbr_epoch_t	qsbr_barrier(qsbr_t *);
bool		qsbr_sync(qsbr_t *, qsbr_epoch_t);
qsbr_epoch_t qsbr_get_epoch(qsbr_t *qs);
void qsbr_wait(qsbr_t *qsbr, const struct timespec sleep);
void		qsbr_thread_offline(qsbr_t *qs);
void		qsbr_thread_online(qsbr_t *qs);

__END_DECLS

#endif
