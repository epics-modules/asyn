dbLoadDatabase("../../dbd/testEpics.dbd")
testEpics_registerRecordDeviceDriver(pdbbase)

cacheInt32DriverInit("cacheA",0.05,0)
interruptInt32DriverInit("intA",0.1,0)
dbLoadRecords("../../db/asynRecord.db","P=asyn,R=Record_PA_A0,PORT=cacheA,ADDR=0,OMAX=0,IMAX=0")
dbLoadRecords("../../db/devInt32.db","PORT=cacheA,ADDR=0")
dbLoadRecords("../../db/devInt32Interrupt.db","PORT=intA")
iocInit()
