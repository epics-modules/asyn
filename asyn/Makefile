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
DBD += asyn.dbd
asyn_SRCS += asynManager.c
asyn_SRCS += asynShellCommands.c
INC += epicsInterruptibleSyscall.h
INC += asynShellCommands.h
INC += asynSyncIO.h
asyn_SRCS += asynSyncIO.c
asyn_SRCS += epicsInterruptibleSyscall.c

SRC_DIRS += $(ASYN)/devAsyn
DBD += devAsyn.dbd
DBDINC += asynRecord
asyn_SRCS += devAsyn.c
asyn_SRCS += asynRecord.c
asyn_SRCS += devAsynGeneric.c
DB += asyn.db
DB += asynRecord.db
DB += asynGeneric.db

SRC_DIRS += $(ASYN)/asynGpib
INC += asynGpibDriver.h
asyn_SRCS += asynGpib.c

SRC_DIRS += $(ASYN)/devGpib
INC += devGpib.h
INC += devSupportGpib.h
INC += devCommonGpib.h
DBD += devGpib.dbd
asyn_SRCS += devCommonGpib.c
asyn_SRCS += devSupportGpib.c

SRC_DIRS += $(ASYN)/vxi11
asyn_SRCS += vxi11core_xdr.c
asyn_SRCS += drvVxi11.c
asyn_SRCS += E5810Reboot.c
asyn_SRCS += E2050Reboot.c
asyn_SRCS += TDS3000Reboot.c
DBD += drvVxi11.dbd


SRC_DIRS += $(ASYN)/drvGenericSerial
INC += drvGenericSerial.h
asyn_SRCS += drvGenericSerial.c
DBD += drvGenericSerial.dbd

SRC_DIRS += $(ASYN)/drvAsynSerial
INC += drvAsynLocalSerialPort.h
asyn_SRCS += drvAsynLocalSerialPort.c
DBD += drvAsynLocalSerialPort.dbd
INC += drvAsynTCPPort.h
asyn_SRCS += drvAsynTCPPort.c
DBD += drvAsynTCPPort.dbd

ifdef IPAC
SRC_DIRS += $(ASYN)/gsIP488
asyn_SRCS_vxWorks += drvGsIP488.c
DBD += drvGsIP488.dbd
asyn_LIBS_vxWorks += Ipac
endif

include $(TOP)/configure/RULES

ifdef T_A
ifeq ($(OS_CLASS),vxWorks)
vxi11core_xdr.c: ../vxi11/rpc/vxi11core_xdr.c
	cp ../vxi11/rpc/*.[ch] .
else
ifeq ($(OS_CLASS),RTEMS)
vxi11core_xdr.c: ../vxi11/rpc/vxi11core_xdr.c
	cp ../vxi11/rpc/*.[ch] .
else
RPCGEN_FLAGS_solaris = -M
vxi11core_xdr.c: ../vxi11/vxi11core.rpcl ../vxi11/vxi11intr.rpcl
	cp ../vxi11/vxi11core.rpcl .
	cp ../vxi11/vxi11intr.rpcl .
	rpcgen $(RPCGEN_FLAGS_$(OS_CLASS)) vxi11core.rpcl
	rpcgen $(RPCGEN_FLAGS_$(OS_CLASS)) vxi11intr.rpcl
endif
endif
endif
