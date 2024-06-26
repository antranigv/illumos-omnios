#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# Copyright 2019 Joyent, Inc.
#

PROG=	zonecfg
OBJS=	zonecfg.o zonecfg_lex.o zonecfg_grammar.tab.o

include ../Makefile.cmd
include ../Makefile.ctf

# zonecfg has a name clash with main() and libl.so.1.  However, zonecfg must
# still export a number of "yy*" (libl) interfaces.  Reduce all other symbols
# to local scope.
MAPFILES +=	$(MAPFILE.LEX) $(MAPFILE.NGB)
MAPOPTS =	$(MAPFILES:%=-Wl,-M%)

LFLAGS =	-t
YFLAGS =	-d -b zonecfg_grammar
LDLIBS +=	-lzonecfg -ll -lnsl -ltecla -lzfs -lbrand -ldladm -linetutil
CFLAGS +=	-DYYLMAX=2048
CPPFLAGS +=	-I.
LDFLAGS +=	$(MAPOPTS)
CLEANFILES +=	zonecfg_lex.c zonecfg_grammar.tab.c zonecfg_grammar.tab.h

CERRWARN +=	$(CNOWARN_UNINIT)
CERRWARN +=	-_gcc=-Wno-unused-label
CERRWARN +=	-_gcc=-Wno-unused-variable
CERRWARN +=	-_gcc=-Wno-implicit-function-declaration

SMOFF += all_func_returns

# false positive
zonecfg_lex.o := SMOFF += free

CLOBBERFILES += $(PROG).ln

.KEEP_STATE:

all: $(PROG)

$(PROG): $(OBJS) $(MAPFILES)
	$(LINK.c) -o $@ $(OBJS) $(LDLIBS)
	$(POST_PROCESS)

install: all $(ROOTUSRSBINPROG)

zonecfg_lex.c:	zonecfg_lex.l zonecfg_grammar.tab.h zonecfg.h
	$(LEX) $(LFLAGS) zonecfg_lex.l > $@

zonecfg_grammar.tab.h zonecfg_grammar.tab.c:	zonecfg_grammar.y zonecfg.h
	$(YACC) $(YFLAGS) zonecfg_grammar.y

zonecfg_lex.o zonecfg_grammar.tab.o := CCVERBOSE =

check:	$(PROG).c $(PROG).h
	$(CSTYLE) -pP $(PROG).c $(PROG).h
	$(HDRCHK) $(PROG).h

clean:
	$(RM) $(OBJS) $(CLEANFILES)

include ../Makefile.targ
