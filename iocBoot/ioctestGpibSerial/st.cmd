< envPaths

dbLoadDatabase("../../dbd/testGpibSerial.dbd")
testGpibSerial_registerRecordDeviceDriver(pdbbase)

dbLoadRecords("../../db/testGpib.db","name=gpibTest,L=0,A=3")
dbLoadRecords("../../db/asynGeneric.db","ioc=gpibTest")

#The following command is for a serial line terminal concentrator
drvGenericSerialConfigure("L0","164.54.9.93:4003",0,0,0)

#The following commands are for a local serial line
#drvGenericSerialConfigure("L0","/dev/cua/a",0,0)
#asynSetPortOption("L0", "baud", "9600")
#asynSetPortOption("L0", "bits", "8")
#asynSetPortOption("L0", "parity", "none")
#asynSetPortOption("L0", "stop", "1")
#asynSetPortOption("L0", "clocal", "Y")
#asynSetPortOption("L0", "crtscts", "N")


asynSetTraceMask("L0",3,0xff)
asynSetTraceIOMask("L0",3,0x2)

iocInit()
