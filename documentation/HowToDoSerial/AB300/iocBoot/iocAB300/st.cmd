dbLoadDatabase("../../dbd/AB300.dbd",0,0)
AB300_registerRecordDeviceDriver(pdbbase) 
dbLoadRecords("../../db/AB300.db","user=AB300")

#The following command is for a serial line terminal concentrator
drvGenericSerialConfigure("L0","164.54.3.137:4001",1,0)

#The following commands are for a local serial line
#drvGenericSerialConfigure("L0","/dev/ttyS0",0,0)
#asynSetPortOption("L0", "baud", "9600")
#asynSetPortOption("L0", "bits", "8")
#asynSetPortOption("L0", "parity", "none")
#asynSetPortOption("L0", "stop", "1")
#asynSetPortOption("L0", "clocal", "Y")
#asynSetPortOption("L0", "crtscts", "N")

asynSetTraceMask("L0",-1,0xff)
asynSetTraceIOMask("L0",-1,0x2)

iocInit()
