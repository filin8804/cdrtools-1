#ident @(#)libhfs.mk	1.3 01/01/21 
###########################################################################
# Sample makefile for non-shared libraries
###########################################################################
SRCROOT=	..
RULESDIR=	RULES
include		$(SRCROOT)/$(RULESDIR)/rules.top
###########################################################################

INSDIR=		lib
TARGETLIB=	hfs
CPPOPTS +=	-DAPPLE_HYB
CFILES=		data.c block.c low.c file.c btree.c node.c record.c \
		volume.c hfs.c\
		gdata.c
HFILES=		block.h btree.h data.h file.h hfs.h hybrid.h internal.h \
		low.h node.h record.h volume.h
LIBS=
XMK_FILE=	Makefile.man

###########################################################################
include		$(SRCROOT)/$(RULESDIR)/rules.lib
###########################################################################
