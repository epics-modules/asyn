TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE

# build an ioc application

PROD_IOC = _NAME_
# _NAME_.dbd will be created and installed
DBD += _NAME_.dbd

_NAME__DBD += base.dbd

# _NAME__registerRecordDeviceDriver.cpp will be created from _NAME_.dbd
_NAME__SRCS += _NAME__registerRecordDeviceDriver.cpp
_NAME__SRCS_DEFAULT += _NAME_Main.cpp
_NAME__SRCS_vxWorks += -nil-

# Add vxWorks support stuff
_NAME__OBJS_vxWorks += $(EPICS_BASE_BIN)/vxComLibrary

# Add the support libraries needed by this application
#_NAME__LIBS += asyn

# To build SNL programs, SNCSEQ must be defined
# in the <top>/configure/RELEASE file:
ifneq ($(SNCSEQ),)
    _NAME__DBD += sncExample.dbd
    _NAME__SRCS += sncExample.stt
    _NAME__LIBS += seq pv
endif

# Finally include the libraries from base
_NAME__LIBS += $(EPICS_BASE_IOC_LIBS)

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

