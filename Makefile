#ident %W% %E% %Q%
###########################################################################
SRCROOT=	.
DIRNAME=	SRCROOT
RULESDIR=	RULES
include		$(SRCROOT)/$(RULESDIR)/rules.top
###########################################################################

include		$(SRCROOT)/TARGETS/Targetdirs

###########################################################################
include		$(SRCROOT)/$(RULESDIR)/rules.dir
###########################################################################