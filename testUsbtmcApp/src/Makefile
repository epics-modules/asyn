TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#=============================
# Build the IOC application for everything that has libusb support

ifeq ($(DRV_USBTMC),YES)
PROD_HOST += testUSBTMC

# testUSBTMC.dbd will be created and installed
DBD += testUSBTMC.dbd

# testUSBTMC.dbd will be made up from these files:
testUSBTMC_DBD += base.dbd

# Include dbd files from all support applications:
testUSBTMC_DBD += drvAsynUSBTMC.dbd
testUSBTMC_DBD += asyn.dbd

# Add all the support libraries needed by this IOC
testUSBTMC_LIBS += asyn

# testUSBTMC_registerRecordDeviceDriver.cpp derives from testUSBTMC.dbd
testUSBTMC_SRCS += testUSBTMC_registerRecordDeviceDriver.cpp

# Build the main IOC entry point on workstation OSs.
testUSBTMC_SRCS_DEFAULT += testUSBTMCMain.cpp
testUSBTMC_SRCS_vxWorks += -nil-

# Add support from base/src/vxWorks if needed
#testUSBTMC_OBJS_vxWorks += $(EPICS_BASE_BIN)/vxComLibrary

# Finally link to the EPICS Base libraries
testUSBTMC_LIBS += $(EPICS_BASE_IOC_LIBS)
testUSBTMC_SYS_LIBS += usb-1.0

endif

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

