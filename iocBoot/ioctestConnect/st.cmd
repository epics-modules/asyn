dbLoadDatabase("../../dbd/testConnect.dbd")
testConnect_registerRecordDeviceDriver(pdbbase)

# This is a bogus IP address that will never connect
#drvAsynIPPortConfigure("IPPort", "164.54.160.220:20", 0, 0, 1);
# This is a real IP address that will connect
drvAsynIPPortConfigure("IPPort", "newport-xps12:5001", 0, 0, 1);
#drvAsynIPPortConfigure("IPPort", "newport-xps12:5001 COM", 0, 0, 1);
testConnectConfigure("PORT1", "IPPort", "FirmwareVersionGet(char *)")
#asynSetTraceMask("PORT1",0,9)
asynSetTraceIOMask("PORT1",0,0x2)

dbLoadRecords("../../db/asynRecord.db","P=testConnect:,R=asyn1,PORT=PORT1,ADDR=0,OMAX=80,IMAX=80")
iocInit()

