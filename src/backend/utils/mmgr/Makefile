#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for utils/mmgr
#
# IDENTIFICATION
#    $PostgreSQL: pgsql/src/backend/utils/mmgr/Makefile,v 1.13 2008/02/19 10:30:09 petere Exp $
#
#-------------------------------------------------------------------------

subdir = src/backend/utils/mmgr
top_builddir = ../../../..
include $(top_builddir)/src/Makefile.global
override CPPFLAGS := -I$(top_srcdir)/src/include/catalog $(CPPFLAGS)
override CPPFLAGS := -I include $(CPPFLAGS)

OBJS =  aset.o asetDirect.o mcxt.o memaccounting.o mpool.o portalmem.o memprot.o vmem_tracker.o redzone_handler.o runaway_cleaner.o idle_tracker.o event_version.o

include $(top_srcdir)/src/backend/common.mk
