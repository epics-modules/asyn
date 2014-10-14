TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

DBD += testConnect.dbd

LIBRARY_IOC += testConnectSupport
LIB_SRCS += testConnect.cpp
LIB_LIBS += asyn
LIB_LIBS += $(EPICS_BASE_IOC_LIBS)

#=============================

PROD_IOC += testConnect

# <name>_registerRecordDeviceDriver.cpp will be created from <name>.dbd
PROD_SRCS_DEFAULT += testConnect_registerRecordDeviceDriver.cpp testConnectMain.cpp
prodVx_SRCS_vxWorks  += testConnect_registerRecordDeviceDriver.cpp
PROD_LIBS += testConnectSupport asyn
PROD_LIBS += $(EPICS_BASE_IOC_LIBS)

PROD_OBJS_vxWorks += $(EPICS_BASE_BIN)/vxComLibrary

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
