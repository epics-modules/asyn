TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

ASYN = $(TOP)
ASYN_LIB = $(TOP)/lib/$(T_A)
ASYN_BIN = $(TOP)/bin/$(T_A)

DBD += devTestGpib.dbd

DBD += testGpib.dbd
testGpib_DBD += testGpibSupport.dbd

ifeq ($(OS_CLASS), vxWorks)
DBD += testGpibVx.dbd
testGpibVx_DBD += testGpibSupport.dbd ni1014Include.dbd
endif

#=============================

PROD_IOC_DEFAULT += testGpib
PROD_IOC_vxWorks += testGpibVx

LIBRARY_IOC += devTestGpib
devTestGpib_SRCS += devTestGpib.c
devTestGpib_LIBS += asyn
ifdef IPAC
testGpibVx_DBD += ipacInclude.dbd
devTestGpib_LIBS_vxWorks += Ipac
endif
devTestGpib_LIBS += $(EPICS_BASE_IOC_LIBS)


# <name>_registerRecordDeviceDriver.cpp will be created from <name>.dbd
testGpib_SRCS_DEFAULT += testGpib_registerRecordDeviceDriver.cpp testGpibMain.c
testGpibVx_SRCS_vxWorks += testGpibVx_registerRecordDeviceDriver.cpp

testGpib_LIBS += devTestGpib
testGpib_LIBS += testSupport asyn
SYS_PROD_LIBS_cygwin32 += $(CYGWIN_RPC_LIB)
testGpib_LIBS += $(EPICS_BASE_IOC_LIBS)

testGpibVx_OBJS_vxWorks += $(EPICS_BASE_BIN)/vxComLibrary
vxComLibrary_DIR = $(EPICS_BASE_BIN)

testGpibVx_LIBS += devTestGpib
ifdef IPAC
testGpibVx_LIBS_vxWorks += Ipac
endif
testGpibVx_LIBS += testSupport asyn
testGpibVx_LIBS += $(EPICS_BASE_IOC_LIBS)


#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
