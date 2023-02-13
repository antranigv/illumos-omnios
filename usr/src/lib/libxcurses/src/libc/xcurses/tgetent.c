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
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

/*
 * tgetent.c
 *
 * XCurses Library
 *
 * Copyright 1990, 1995 by Mortice Kern Inc.  All rights reserved.
 *
 */

#ifdef M_RCSID
#ifndef lint
static char rcsID[] = "$Header: /rd/src/libc/xcurses/rcs/tgetent.c 1.1 1995/07/21 14:20:22 ant Exp $";
#endif
#endif

#include <private.h>

/*
 * Return  if termcap file (terminfo database) cannot be opened;
 * 0 if the terminal name is not present in the termcap file; 1 if
 * all goes well.
 */
int
(tgetent)(buffer, name)
char *buffer, *name;
{
	int err;

#ifdef M_CURSES_TRACE
	__m_trace("tgetent(%p, %p = \"%s\")", buffer, name, name);
#endif

	(void) setupterm(name, fileno(stdout), &err);

	return __m_return_int("tgetent", err);
}
