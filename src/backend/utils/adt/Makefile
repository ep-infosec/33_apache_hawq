#
# Makefile for utils/adt
#
# $PostgreSQL: pgsql/src/backend/utils/adt/Makefile,v 1.60 2006/04/05 22:11:55 tgl Exp $
#

subdir = src/backend/utils/adt
top_builddir = ../../../..
include $(top_builddir)/src/Makefile.global
override CPPFLAGS := -I$(top_srcdir)/src/backend/gp_libpq_fe  $(CPPFLAGS)

# seems to be required for some date/time stuff 1999/07/22 bjm
ifeq "$(findstring alpha,$(host_cpu))" "alpha"
ifeq "$(GCC)" "yes"
override CFLAGS+= -mieee
endif
endif

OBJS = acl.o array_userfuncs.o arrayfuncs.o arrayutils.o ascii.o \
	bool.o cash.o char.o date.o datetime.o datum.o dbsize.o \
	domains.o encode.o float.o format_type.o formatting.o genfile.o \
	geo_ops.o geo_selfuncs.o gp_optimizer_functions.o \
	gp_partition_functions.o inet_net_ntop.o inet_net_pton.o int.o \
	int8.o json.o jsonb.o jsonb_gin.o jsonb_op.o jsonb_util.o like.o lockfuncs.o jsonfuncs.o mac.o matrix.o misc.o nabstime.o name.o \
	network.o not_in.o numeric.o numutils.o oid.o oracle_compat.o \
	percentile.o pg_locale.o pg_lzcompress.o pgstatfuncs.o pivot.o \
	pseudotypes.o pxf_functions.o quote.o regexp.o regproc.o ri_triggers.o rowtypes.o \
	ruleutils.o selfuncs.o tid.o timestamp.o varbit.o varchar.o varlena.o \
	version.o xid.o xml.o

like.o: like.c like_match.c

include $(top_srcdir)/src/backend/common.mk
