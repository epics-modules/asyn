#*************************************************************************
# Copyright (c) 2002 The University of Chicago, as Operator of Argonne
#     National Laboratory.
# Copyright (c) 2002 The Regents of the University of California, as
#     Operator of Los Alamos National Laboratory.
# EPICS BASE Versions 3.13.7
# and higher are distributed subject to a Software License Agreement found
# in file LICENSE that is included with this distribution. 
#*************************************************************************
TOP = ..
include $(TOP)/configure/CONFIG

ASYN = $(TOP)/asyn
#USR_CFLAGS += -DDEBUG

LIBRARY_IOC += asyn
asyn_LIBS += $(EPICS_BASE_IOC_LIBS)

SRC_DIRS += $(ASYN)/asynDriver
INC += asynDriver.h
asyn_SRCS += asynDriver.c

SRC_DIRS += $(ASYN)/gpibDriver
INC += gpibDriver.h

#SRC_DIRS += $(ASYN)/serialDriver
#INC += serialDriver.h

SRC_DIRS += $(ASYN)/devGpib
INC += devGpib.h
INC += devSupportGpib.h
INC += devCommonGpib.h
DBD += devGpib.dbd
asyn_SRCS += devCommonGpib.c
asyn_SRCS += devSupportGpib.c

include $(TOP)/configure/RULES
