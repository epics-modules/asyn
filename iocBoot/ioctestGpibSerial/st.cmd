< envPaths

dbLoadDatabase("../../dbd/testGpibSerial.dbd")
testGpibSerial_registerRecordDeviceDriver(pdbbase)

dbLoadRecords("../../db/testGpib.db","name=gpibTest,L=0,A=0")
dbLoadRecords("../../db/asynRecord.db","P=asyn,R=Test,PORT=L0,ADDR=3,OMAX=0,IMAX=0")

#The following command is for a serial line terminal concentrator
#asynSetTraceMask("",-1,0xff)
drvAsynTCPPortConfigure("L0","164.54.9.90:4004",0,0)

#The following commands are for a local serial line
#drvAsynLocalSerialPortConfigure("L0","/dev/cua/a",0,0)
#asynSetPortOption("L0", "baud", "9600")
#asynSetPortOption("L0", "bits", "8")
#asynSetPortOption("L0", "parity", "none")
#asynSetPortOption("L0", "stop", "1")
#asynSetPortOption("L0", "clocal", "Y")
#asynSetPortOption("L0", "crtscts", "N")

#asynSetTraceMask("L0",-1,0xff)
#asynSetTraceIOMask("L0",-1,0x2)

iocInit()
