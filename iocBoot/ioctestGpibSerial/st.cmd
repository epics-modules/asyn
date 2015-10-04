< envPaths

dbLoadDatabase("../../dbd/testGpibSerial.dbd")
testGpibSerial_registerRecordDeviceDriver(pdbbase)

#dbLoadRecords("../../db/testGpib.db","P=gpib,R=Test,PORT=L0,A=1")
dbLoadRecords("../../db/asynRecord.db","P=asyn,R=Record,PORT=L0,ADDR=0,OMAX=0,IMAX=0")

# The following command is for a serial line terminal concentrator
#drvAsynIPPortConfigure("L0","serials8n3:4004 COM",0,0,0)
#
# The following commands are for a USB-tty or a local serial line
#drvAsynSerialPortConfigure("L0","/dev/ttyUSB0",0,0,0)
drvAsynSerialPortConfigure("L0","/dev/ttyS0",0,0,0)
# The following commands are for Windows serial ports. i
# Either syntax is OK for COM1-COM9.  For COM10 and above the \\.\COMxx syntax is required.
#drvAsynSerialPortConfigure("L0","COM1",0,0,0)
#drvAsynSerialPortConfigure("L0","\\.\COM2",0,0,0)
#drvAsynSerialPortConfigure("L0","/dev/tty.PL2303-0000101D",0,0,0)
asynSetOption("L0", -1, "baud", "2400")
asynSetOption("L0", -1, "bits", "8")
asynSetOption("L0", -1, "parity", "none")
asynSetOption("L0", -1, "stop", "1")
asynSetOption("L0", -1, "clocal", "Y")
asynSetOption("L0", -1, "crtscts", "N")

#asynSetTraceFile("L0",-1,"")
#asynSetTraceMask("L0",-1,0x019)
asynSetTraceIOMask("L0",-1,0x2)

asynOctetSetOutputEos("L0",-1,"\r\n")
asynOctetSetInputEos("L0",-1,"\n")

iocInit()
