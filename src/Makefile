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

ifeq ($(PURIFY),YES)
  ifeq ($(OS_CLASS),solaris)
    PURIFY_FLAGS = -first-only -chain-length=26 -max_threads=256
    # Put the cache files in the appropriate bin directory
    PURIFY_FLAGS += -always-use-cache-dir -cache-dir=$(shell $(PERL) $(TOOLS)/fullPathName.pl .)
    DEBUGCMD = purify $(PURIFY_FLAGS)
  endif
endif

ifeq ($(QUANTIFY),YES)
  ifeq ($(OS_CLASS),solaris)
    #QUANTIFY_FLAGS += -measure-timed-calls=user+system
    QUANTIFY_FLAGS += -collection-granularity=function
    QUANTIFY_FLAGS += -use-machine=UltraSparcIII:1002MHz
    QUANTIFY_FLAGS += -max_threads=160

    #QUANTIFY_FLAGS += -record-system-calls=no

    # -measure-timed-calls=elapsed-time (default) gives wall clock time
    #   for system calls
    # -measure-timed-calls=user+system gives user+system time
    # -record-system-calls=no gives 0 time for system calls
    # -collection-granularity=function runs faster than default=line
    # -use-machine=UltraSparc:168MHz timing for Nike
    # -use-machine=UltraSparcIII:1002MHz timing for Ctlapps1
    DEBUGCMD = quantify $(QUANTIFY_FLAGS)
  endif
endif

ifeq ($(USE_DENY_FROM),YES)
USR_CXXFLAGS += -DUSE_DENYFROM
endif

ifeq ($(USE_NEG_REGEXP),YES)
USR_CXXFLAGS += -DUSE_NEG_REGEXP
endif

# Reserve file descriptor for fopen to avoid fd limit of 256 on Solaris
USR_CXXFLAGS_solaris += -DRESERVE_FOPEN_FD

ifeq ($(CMPLR_CLASS),msvc)
  USR_LDFLAGS_WIN32 += /SUBSYSTEM:CONSOLE
endif

ifeq ($(USE_PCRE),YES)
  USR_CXXFLAGS += -DUSE_PCRE
  ifeq ($(OS_CLASS),WIN32)
    USR_INCLUDES += -I$(PCRE_DIR)/inc
    PROD_LIBS += pcre3
    ifeq ($(CMPLR_CLASS),msvc)
      ifeq ($(T_A),windows-x64)
        pcre3_DIR = $(PCRE_DIR)/lib/x64
      else
        pcre3_DIR = $(PCRE_DIR)/lib
      endif
    else
      pcre3_DIR = $(PCRE_DIR)/bin
    endif
  else
    USR_SYS_LIBS += pcre
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
	@echo PURIFY: $(PURIFY)
	@echo PURIFY_FLAGS: $(PURIFY_FLAGS)
	@echo PURIFYCMD: $(PURIFYCMD)
	@echo QUANTIFY: $(QUANTIFY)
	@echo QUANTIFYCMD: $(QUANTIFYCMD)
	@echo CXX $(CXX)
	@echo CXXFLAGS $(CXXFLAGS)
	@echo LINK.cc: $(LINK.cc)
	@echo LINK.c: $(LINK.c)
	@echo TARGET_OBJS: $(TARGET_OBJS)
	@echo PRODNAME_OBJS: $(PRODNAME_OBJS)
	@echo PROD_LD_OBJS: $(PROD_LD_OBJS)
	@echo PRODUCT_OBJS: $(PRODUCT_OBJS)
	@echo PROD_OBJS: $(PROD_OBJS)
	@echo EPICS_BASE: $(EPICS_BASE)
	@echo HOST_ARCH: $(HOST_ARCH)
	@echo ARCH_DEP_LDFLAGS_ML_NO: $(ARCH_DEP_LDFLAGS_ML_NO)
	@echo ARCH_DEP_LDFLAGS_ML_YES: $(ARCH_DEP_LDFLAGS_ML_YES)
	@echo ARCH_DEP_LDFLAGS_ML: $(ARCH_DEP_LDFLAGS_ML)
	@echo ARCH_DEP_LDFLAGS_MD_NO: $(ARCH_DEP_LDFLAGS_MD_NO)
	@echo ARCH_DEP_LDFLAGS_MD_YES: $(ARCH_DEP_LDFLAGS_MD_YES)
	@echo ARCH_DEP_LDFLAGS_MD: $(ARCH_DEP_LDFLAGS_MD)
	@echo ACC_SFLAGS_YES: $(ACC_SFLAGS_YES)
	@echo ACC_SFLAGS_NO: $(ACC_SFLAGS_NO)
	@echo SHARED_LIBRARIES: $(SHARED_LIBRARIES)
	@echo DEBUGCMD: $(DEBUGCMD)
	@echo CAPUTLOG: $(CAPUTLOG)
	@echo EPICS_BASE_HOST_LIBS: $(EPICS_BASE_HOST_LIBS)
