TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

ASYN = $(TOP)
ASYN_LIB = $(TOP)/lib/$(T_A)
ASYN_BIN = $(TOP)/bin/$(T_A)

DBD += devTestGpib.dbd

ifeq ($(OS_CLASS), WIN32)
DBD  += testGpibWin32.dbd
testGpibWin32_DBD += win32Include.dbd
else
DBD += testGpib.dbd
testGpib_DBD += commonInclude.dbd
endif

ifeq ($(OS_CLASS), vxWorks)
DBD += testGpibVx.dbd
testGpibVx_DBD += commonInclude.dbd ni1014Include.dbd
endif

#=============================

PROD_IOC_DEFAULT += testGpib
PROD_IOC_vxWorks += testGpibVx

LIBRARY_IOC += devTestGpib
devTestGpib_SRCS += devTestGpib.c
devTestGpib_LIBS += asyn
testGpibVx_DBD += commonInclude.dbd ni1014Include.dbd
ifdef IPAC
testGpibVx_DBD += ipacInclude.dbd
devTestGpib_LIBS_vxWorks += Ipac
endif
devTestGpib_LIBS += $(EPICS_BASE_IOC_LIBS)


# <name>_registerRecordDeviceDriver.cpp will be created from <name>.dbd
testGpib_SRCS_DEFAULT += testGpib_registerRecordDeviceDriver.cpp testGpibMain.c
testGpib_SRCS_WIN32   += testGpibWin32_registerRecordDeviceDriver.cpp testGpibMain.c
testGpibVx_SRCS_vxWorks += testGpibVx_registerRecordDeviceDriver.cpp

testGpib_LIBS += devTestGpib
testGpib_LIBS += testSupport asyn
SYS_PROD_LIBS_cygwin32 += rpc
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
