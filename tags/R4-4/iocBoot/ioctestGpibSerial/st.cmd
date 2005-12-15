< envPaths

dbLoadDatabase("../../dbd/testGpibSerial.dbd")
testGpibSerial_registerRecordDeviceDriver(pdbbase)
# For WIN32 use the following 2 lines rather than those above
#dbLoadDatabase("../../dbd/testGpibSerialWin32.dbd")
#testGpibSerialWin32_registerRecordDeviceDriver(pdbbase)

#dbLoadRecords("../../db/testGpib.db","P=gpib,R=Test,PORT=L0,A=1")
dbLoadRecords("../../db/asynRecord.db","P=asyn,R=Record,PORT=L0,ADDR=0,OMAX=0,IMAX=0")

#The following command is for a serial line terminal concentrator
drvAsynIPPortConfigure("L1","164.54.9.90:4004",0,0,0)

#The following commands are for a local serial line
#drvAsynSerialPortConfigure("L0","/dev/ttyb",0,0,0)
drvAsynSerialPortConfigure("L0","/dev/ttyS0",0,0,0)
#asynSetOption("L0", -1, "baud", "9600")
#asynSetOption("L0", -1, "bits", "8")
#asynSetOption("L0", -1, "parity", "none")
#asynSetOption("L0", -1, "stop", "1")
#asynSetOption("L0", -1, "clocal", "Y")
#asynSetOption("L0", -1, "crtscts", "N")

#asynSetTraceFile("L0",-1,"")
#asynSetTraceMask("L0",-1,0x09)
#asynSetTraceIOMask("L0",-1,0x2)

iocInit()
