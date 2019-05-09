EXTENSION = pg_mem_counters
MODULES = pg_mem_counters
DATA = pg_mem_counters--1.0.sql
OBJS = pg_mem_counters.o
REGRESS = pg_mem_counters

ifndef PG_CONFIG
PG_CONFIG = pg_config
endif

ifdef USE_PGXS
PGXS := $(shell ${PG_CONFIG} --pgxs)
include $(PGXS)
else
subdir = contrib/pg_mem_counters
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif


