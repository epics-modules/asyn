< envPaths

dbLoadDatabase("../../dbd/testGpibSerial.dbd")
testGpibSerial_registerRecordDeviceDriver(pdbbase)

dbLoadRecords("../../db/testGpib.db","name=gpibTest,L=0,A=3")

#The following command is for a serial line terminal concentrator
drvGenericSerialConfigure("L0","164.54.9.93:4003",0,0)

#The following commands are for a local serial line
#drvGenericSerialConfigure("L0","/dev/cua/a",0,0, 9600,"cs8","-parenb","-crtscts","clocal")
#drvGenericSerialConfigure("L0","/dev/cua/a",0,0)

asynSetTraceMask("L0",3,0xff)
asynSetTraceIOMask("L0",3,0x2)

iocInit()
