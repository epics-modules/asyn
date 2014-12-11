#!../../bin/darwin-x86/usbtmcTest

###############################################################################
# Set up environment
< envPaths
epicsEnvSet(P, "$(USBTMC_P=usbtmc:)")
epicsEnvSet(R, "$(USBTMC_R=asyn)")

###############################################################################
# Register support components
cd "$(TOP)"
dbLoadDatabase "dbd/testUSBTMC.dbd"
testUSBTMC_registerRecordDeviceDriver pdbbase

###############################################################################
# Configure hardware
# usbtmcConfigure(port, vendorNum, productNum, serialNumberStr, priority, flags)
usbtmcConfigure("usbtmc1")
asynSetTraceIOMask("usbtmc1",0,0x2)
asynSetTraceMask("usbtmc1",0,0x03)

###############################################################################
# Load record instances
dbLoadRecords("db/asynRecord.db","P=$(P),R=$(R),PORT=usbtmc1,ADDR=0,OMAX=100,IMAX=100")

###############################################################################
# Start IOC
cd "$(TOP)/iocBoot/$(IOC)"
iocInit

epicsThreadSleep 2
asynReport 2

dbpf "$(P)$(R).OFMT" "ASCII"
dbpf "$(P)$(R).IFMT" "Hybrid"
dbpf "$(P)$(R).TMOD" "Write/Read"

dbpf "$(P)$(R).AOUT" "*IDN?"
epicsThreadSleep 2

echo In another window run caget -S $(P)$(R).BINP and confirm that the IDN string was read.

dbpr "$(P)$(R)" 1
