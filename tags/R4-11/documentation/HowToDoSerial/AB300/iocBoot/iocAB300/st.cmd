dbLoadDatabase("../../dbd/AB300.dbd",0,0)
AB300_registerRecordDeviceDriver(pdbbase) 
dbLoadRecords("../../db/devAB300.db","P=AB300:,R=,L=0,A=0")

# Create a diagnostic asynRecord
cd ${AB300}
dbLoadRecords("db/asynRecord.db","P=AB300,R=Test,PORT=L0,ADDR=0,IMAX=0,OMAX=0")

#The following command is for a serial line terminal concentrator
drvAsynIPPortConfigure("L0","164.54.3.137:4001",0,0,0)

#The following commands are for a local serial line
#drvAsynSerialPortConfigure("L0","/dev/ttyS0",0,0,0)
#asynSetOption("L0", -1, "baud", "9600")
#asynSetOption("L0", -1, "bits", "8")
#asynSetOption("L0", -1, "parity", "none")
#asynSetOption("L0", -1, "stop", "1")
#asynSetOption("L0", -1, "clocal", "Y")
#asynSetOption("L0", -1, "crtscts", "N")

asynSetTraceMask("L0",-1,0x9)
asynSetTraceIOMask("L0",-1,0x2)

iocInit()
