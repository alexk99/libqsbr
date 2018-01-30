/*
 * Copyright (c) 2016-2018 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <err.h>
#include <stdint.h>
#include <assert.h>

#include "qsbr.h"
#include "utils.h"

static unsigned			nsec = 10; /* seconds */

static pthread_barrier_t	barrier;
static unsigned				nworkers;
static volatile bool			stop;

#define	CACHE_LINE_SIZE		64

typedef struct {
	unsigned int *		ptr;
	unsigned int		visible;
	char			_pad[
	    CACHE_LINE_SIZE - (sizeof(unsigned int *) + sizeof(int))];
} data_struct_t;

#define	DS_COUNT		4
#define	MAGIC_VAL		0x5a5a5a5a

static unsigned			magic_val = MAGIC_VAL;
static qsbr_t *			qsbr;
static data_struct_t		ds[DS_COUNT]
    __attribute__((__aligned__(CACHE_LINE_SIZE)));

/*
 * Start new epoch and wait until all registered threads have observed it
 */
static void
wait(qsbr_t *qsbr, const struct timespec sleep)
{
   qsbr_epoch_t new_epoch = qsbr_barrier(qsbr);
   while (!qsbr_sync(qsbr, new_epoch))
      (void)nanosleep(&sleep, NULL);
}

static void
qsbr_writer(unsigned target)
{
	data_struct_t *obj = &ds[target];

	if (obj->visible) {
		/*
		 * The data structure is visible.  First, ensure it is no
		 * longer visible (think of "remove" semantics).
		 */
		assert(obj->visible);
		obj->visible = false;

		/* make sure all readers have stopped using target object */
		struct timespec sleep;
		sleep.tv_sec = 0;
		sleep.tv_nsec = 100;
		wait(qsbr, sleep);

		/* now it's safe to modify the target */
		obj->ptr = NULL;
	}
	else {
		/* object is not visible.
		 * publish it back.
		 */
		obj->ptr = &magic_val;
		atomic_thread_fence(memory_order_release);
		assert(!obj->visible);
		obj->visible = true;
	}
}

static void *
qsbr_stress(void *arg)
{
	const unsigned id = (uintptr_t)arg;
	unsigned n = 0;

	qsbr_register(qsbr);

	/*
	 * There are NCPU threads concurrently reading data and a single
	 * writer thread (ID 0) modifying data.  The writer will modify
	 * the pointer used by the readers to NULL as soon as it considers
	 * the object ready for reclaim.
	 */

	pthread_barrier_wait(&barrier);
	while (!stop) {
		n = ++n & (DS_COUNT - 1);

		if (id == 0) {
			qsbr_writer(n);
			continue;
		}

		/*
		 * Reader: iterate through the data structures and,
		 * if the object is visible (think of "lookup" semantics),
		 * read its value through a pointer.  The writer will set
		 * the pointer to NULL when it thinks the object is ready
		 * to be reclaimed.
		 *
		 * Incorrect reclamation mechanism would lead to the crash
		 * in the following pointer dereference.
		 */

		if (ds[n].visible && *ds[n].ptr != MAGIC_VAL)
			abort();

		qsbr_checkpoint(qsbr);
	}

	/* ensure that the writer will stop
	 * if it waits for readers
	 */
	qsbr_checkpoint(qsbr);

	pthread_barrier_wait(&barrier);
	pthread_exit(NULL);
	return NULL;
}

static void
ding(int sig)
{
	(void)sig;
	printf("stoped\n");
	stop = true;
}

static void
run_test(void *func(void *))
{
	struct sigaction sigalarm;
	pthread_t *thr;
	int ret;

	/*
	 * Setup the threads.
	 */
	nworkers = sysconf(_SC_NPROCESSORS_CONF) + 1;
	printf("num workers: %d\n", nworkers);
	thr = calloc(nworkers, sizeof(pthread_t));
	pthread_barrier_init(&barrier, NULL, nworkers);
	stop = false;

	memset(&sigalarm, 0, sizeof(struct sigaction));
	sigalarm.sa_handler = ding;
	ret = sigaction(SIGALRM, &sigalarm, NULL);
	assert(ret == 0); (void)ret;

	/*
	 * Create some data structures and the EBR object.
	 */
	memset(&ds, 0, sizeof(ds));
	qsbr = qsbr_create();

	/*
	 * Spin the test.
	 */
	alarm(nsec);

	for (unsigned i = 0; i < nworkers; i++) {
		if ((errno = pthread_create(&thr[i], NULL,
		    func, (void *)(uintptr_t)i)) != 0) {
			err(EXIT_FAILURE, "pthread_create");
		}
	}

	for (unsigned i = 0; i < nworkers; i++)
		pthread_join(thr[i], NULL);

	pthread_barrier_destroy(&barrier);
	qsbr_destroy(qsbr);
}

int
main(int argc, char **argv)
{
	if (argc >= 2) {
		nsec = (unsigned)atoi(argv[1]);
	}
	puts("QSBR stress test");
	run_test(qsbr_stress);
	puts("ok");
	return 0;
}