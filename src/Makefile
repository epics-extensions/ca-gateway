#*************************************************************************
# Copyright (c) 2002 The University of Chicago, as Operator of Argonne
# National Laboratory.
# Copyright (c) 2002 Berliner Speicherring-Gesellschaft fuer Synchrotron-
# Strahlung mbH (BESSY).
# Copyright (c) 2002 The Regents of the University of California, as
# Operator of Los Alamos National Laboratory.
# Copyright (c) 2014-2016 ITER Organization.
# This file is distributed subject to a Software License Agreement found
# in the file LICENSE that is included with this distribution.
#*************************************************************************

TOP = ..
include $(TOP)/configure/CONFIG

ifeq ($(USE_DENY_FROM),YES)
USR_CXXFLAGS += -DUSE_DENYFROM
endif

ifeq ($(USE_NEG_REGEXP),YES)
USR_CXXFLAGS += -DUSE_NEG_REGEXP
endif

ifeq ($(CMPLR_CLASS),msvc)
  USR_LDFLAGS_WIN32 += /SUBSYSTEM:CONSOLE
endif

ifeq ($(USE_PCRE2),YES)
  USR_CXXFLAGS += -DUSE_PCRE2
  ifeq ($(OS_CLASS),WIN32)
    ifdef PCRE_DIR
      # Modern or MSYS2 pcre used in Windows context
      USR_INCLUDES += -I$(PCRE_DIR)/include
      PROD_LIBS += pcre2-8
      pcre2-8_DIR = $(PCRE_DIR)/lib
    else
      # MSYS2 package pcre2-devel used in MSYSTEM context
      USR_SYS_LIBS += pcre2-8
    endif
  else
    USR_SYS_LIBS += pcre2-8
  endif
else
  # On Linux basic regex is part of libc
  ifneq ($(OS_CLASS),Linux)
    USR_INCLUDES += -I$(REGEX_DIR)/include
    PROD_LIBS += regex
    regex_DIR = $(REGEX_DIR)/lib/$(T_A)
  endif
endif

PROD_LIBS += cas gdd
PROD_LIBS += $(EPICS_BASE_HOST_LIBS)

gateway_SRCS += gateway.cc
gateway_SRCS += gatePv.cc
gateway_SRCS += gateResources.cc
gateway_SRCS += gateServer.cc
gateway_SRCS += gateAs.cc
gateway_SRCS += gateVc.cc
gateway_SRCS += gateAsyncIO.cc
gateway_SRCS += gateAsCa.cc
gateway_SRCS += gateStat.cc
gateway_SRCS += gateDbFld.cc

# HACK: Needs access to private CAS header casCtx.h
USR_INCLUDES += -I$(EPICS_BASE)/src/cas/generic
USR_INCLUDES += -I$(EPICS_BASE)/src/ca/legacy/pcas/generic

# To compile in caPutLog functionality, define the location of the caPutLog
# module as 'CAPUTLOG' in the appropriate extensions configure/RELEASE* file
ifdef CAPUTLOG
  USR_LIBS += caPutLog $(EPICS_BASE_IOC_LIBS)
  USR_CXXFLAGS += -DWITH_CAPUTLOG
  USR_CFLAGS += -DWITH_CAPUTLOG
endif

PROD_HOST = gateway

include $(TOP)/configure/RULES

xxxx:
	@echo HOST_OPT: $(HOST_OPT)
	@echo CXX $(CXX)
	@echo CXXFLAGS $(CXXFLAGS)
	@echo LINK.cc: $(LINK.cc)
	@echo LINK.c: $(LINK.c)
	@echo EPICS_BASE: $(EPICS_BASE)
	@echo EPICS_HOST_ARCH: $(EPICS_HOST_ARCH)
	@echo SHARED_LIBRARIES: $(SHARED_LIBRARIES)
	@echo CAPUTLOG: $(CAPUTLOG)
	@echo EPICS_BASE_HOST_LIBS: $(EPICS_BASE_HOST_LIBS)
