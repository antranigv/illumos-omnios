/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/* Copyright 2023 Richard Lowe */

/*
 * Simple tests of static TLS, somewhat messy to to be easier parameterised.
 */

#include <sys/debug.h>
#include <sys/types.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <synch.h>
#include <thread.h>

#define	NTHREADS 100

mutex_t recheckmtx = DEFAULTMUTEX;
cond_t recheckcv = DEFAULTCV;

mutex_t terminatemtx = DEFAULTMUTEX;
cond_t terminatecv = DEFAULTCV;

uint32_t donecnt = 0;

/*
 * The model we expect is passed in from the test wrapper -DMODEL='"foo"' for
 * any "foo" that appears in the compiler manual for this attribute.  Note the
 * double quoting.
 *
 * If TLS_FROM_SO is defined these symbols are expected to come from a shared
 * object (lib.c) to ensure that platforms that can transition IE->LE don't.
 */
#ifndef TLS_FROM_SO
__thread uint32_t foo __attribute__((tls_model(MODEL))) = 0x8675309;
__thread char bar[BUFSIZ]
    __attribute__((tls_model(MODEL), section(".tbss"))) = {0};
#else
extern __thread uint32_t foo __attribute__((tls_model(MODEL)));
extern __thread char bar[BUFSIZ] __attribute__((tls_model(MODEL)));
#endif

/*
 * Called on the main thread, check the initial values
 * mutate them, check it stuck.
 */
void
check_values_nowait(void)
{
	uint32_t rand_foo = arc4random();
	char rand_buf[BUFSIZ] = {0};

	/* Initial value */
	VERIFY3S(foo, ==, 0x8675309);
	VERIFY3S(memcmp(bar, rand_buf, BUFSIZ), ==, 0);

	foo = rand_foo;
	arc4random_buf(rand_buf, BUFSIZ);
	memcpy(bar, rand_buf, BUFSIZ);

	VERIFY3S(foo, ==, rand_foo);
	VERIFY3S(memcmp(bar, rand_buf, BUFSIZ), ==, 0);
}

/*
 * Called on every other thread, check we got the initial values mutate them
 * and check it stuck, then wait for all threads to have done this, and check
 * again that our values are good (ie. no other thread mutated our values)
 */
void *
check_values(void *arg __unused)
{
	uint32_t rand_foo = arc4random();
	char rand_buf[BUFSIZ] = {0};

	/* Initial value */
	VERIFY3S(foo, ==, 0x8675309);
	VERIFY3S(memcmp(bar, rand_buf, BUFSIZ), ==, 0);

	foo = rand_foo;
	arc4random_buf(rand_buf, BUFSIZ);
	memcpy(bar, rand_buf, BUFSIZ);

	VERIFY3S(foo, ==, rand_foo);
	VERIFY3S(memcmp(bar, rand_buf, BUFSIZ), ==, 0);

	/* Tell main thread we've done our initial work */
	mutex_lock(&terminatemtx);
	donecnt++;
	cond_signal(&terminatecv);
	mutex_unlock(&terminatemtx);

	/* Wait to be told to recheck when everyone is done */
	mutex_lock(&recheckmtx);
	cond_wait(&recheckcv, &recheckmtx);
	mutex_unlock(&recheckmtx);

	VERIFY3S(foo, ==, rand_foo);
	VERIFY3S(memcmp(bar, rand_buf, BUFSIZ), ==, 0);

	thr_exit(NULL);
}

int
main(int argc __unused, char **argv __unused)
{
	/*
	 * Check values on the initial thread, this also mutates them to make
	 * sure no other thread sees those new values
	 */
	check_values_nowait();

	for (int i = 0; i < 100; i++) {
		thr_create(NULL, 0, check_values, NULL, 0, NULL);
	}

	/* Wait for all threads to finish their initial check and mutation */
	mutex_lock(&terminatemtx);
	while (donecnt < NTHREADS) {
		cond_wait(&terminatecv, &terminatemtx);
	}
	mutex_unlock(&terminatemtx);

	/*
	 * Tell threads to re-check their values to make sure no thread
	 * affected another
	 */
	cond_broadcast(&recheckcv);

	/* Wait for everyone to recheck */
	while (thr_join(0, NULL, NULL) == 0)
		;

	return (EXIT_SUCCESS);
}
