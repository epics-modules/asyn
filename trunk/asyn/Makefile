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
INC += epicsInterruptibleSyscall.h
INC += epicsString1.h
asyn_SRCS += asynManager.c
asyn_SRCS += epicsInterruptibleSyscall.c
asyn_SRCS += epicsString1.c

SRC_DIRS += $(ASYN)/asynGpib
INC += asynGpibDriver.h
asyn_SRCS += asynGpib.c

SRC_DIRS += $(ASYN)/drvAsynSerial
INC += drvAsynSerialPort.h
asyn_SRCS += drvAsynSerialPort.c
DBD += drvAsynSerialPort.dbd
INC += drvAsynTCPPort.h
asyn_SRCS += drvAsynTCPPort.c
DBD += drvAsynTCPPort.dbd

SRC_DIRS += $(ASYN)/asynMisc
DBD += asyn.dbd
INC += asynShellCommands.h
INC += asynSyncIO.h
INC += asynInterposeEos.h
asyn_SRCS += asynShellCommands.c
asyn_SRCS += asynSyncIO.c
asyn_SRCS += asynInterposeEos.c
asyn_SRCS += asynInterposeFlush.c

SRC_DIRS += $(ASYN)/asynRecord
DBD += asynRecDevDrv.dbd
DBDINC += asynRecord
asyn_SRCS += asynRecord.c
asyn_SRCS += drvAsyn.c
DB += asynRecord.db

SRC_DIRS += $(ASYN)/vxi11
asyn_SRCS += vxi11core_xdr.c
asyn_SRCS += drvVxi11.c
asyn_SRCS += E5810Reboot.c
asyn_SRCS += E2050Reboot.c
asyn_SRCS += TDS3000Reboot.c
DBD += drvVxi11.dbd

ifdef IPAC
SRC_DIRS += $(ASYN)/gsIP488
asyn_SRCS_vxWorks += drvGsIP488.c
DBD += drvGsIP488.dbd
asyn_LIBS_vxWorks += Ipac
endif

SRC_DIRS += $(ASYN)/devGpib
INC += devGpib.h
INC += devSupportGpib.h
INC += devCommonGpib.h
DBD += devGpib.dbd
asyn_SRCS += devCommonGpib.c
asyn_SRCS += devSupportGpib.c

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
