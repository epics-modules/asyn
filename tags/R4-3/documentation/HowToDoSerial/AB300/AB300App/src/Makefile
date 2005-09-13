TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================


#AB300.dbd will be created from AB300Include.dbd
DBD += AB300.dbd
DBD += AB300Vx.dbd

## Build and add other local sources like this:

#The following creates an application
PROD_IOC = AB300
PROD_vxWorks = AB300Vx

#=============================

#AB300_registerRecordDeviceDriver.cpp will be created from AB300.dbd
AB300_SRCS += AB300_registerRecordDeviceDriver.cpp
AB300_SRCS += AB300Main.cpp
AB300_LIBS += devAB300
AB300_LIBS += asyn
AB300_LIBS += $(EPICS_BASE_IOC_LIBS)

AB300Vx_SRCS += AB300Vx_registerRecordDeviceDriver.cpp
AB300Vx_LIBS += Ipac TyGSOctal
AB300Vx_LIBS += devAB300
AB300Vx_LIBS += asyn
AB300Vx_LIBS += $(EPICS_BASE_IOC_LIBS)


include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
