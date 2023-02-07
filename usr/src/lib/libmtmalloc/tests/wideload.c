/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 1998 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <limits.h>

/*
 * This file tests for large (2GB and greater) allocations.
 *
 * cc -O -o wideload wideload.c -lmtmalloc
 */

main(int argc, char ** argv)
{
	char * foo;
	char * bar;
	size_t request = LONG_MAX;

	foo = malloc(0); /* prime the pump */

	foo = (char *)sbrk(0);
	printf ("Before big malloc brk is %p request %d\n", foo, request);
	foo = malloc(request + 100);
	bar = (char *)sbrk(0);
	printf ("After big malloc brk is %p\n", bar);
}
