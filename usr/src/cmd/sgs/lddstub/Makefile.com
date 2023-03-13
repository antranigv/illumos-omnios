#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License, Version 1.0 only
# (the "License").  You may not use this file except in compliance
# with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#
#
# Copyright 1994-2003 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

PROG=		lddstub
SRCS=		lddstub.s
OBJS=		$(SRCS:%.s=%.o)

include		../../../Makefile.cmd
include		../../Makefile.com

INTERP=         -I'$$ORIGIN/ld.so.1'

ASFLAGS +=	-D_ASM
LDFLAGS=	$(VERSREF) $(INTERP) $(CONVLIBDIR) -lconv -e stub \
		$(LDFLAGS.cmd)

# XXXARM: This went missing, and I don't know how
$(ROOTLIB64)/$(PROG) := FILEMODE = 0555
$(ROOTLIB)/$(PROG) := FILEMODE = 0555
