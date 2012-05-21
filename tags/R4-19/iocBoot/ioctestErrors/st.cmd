dbLoadDatabase("../../dbd/testErrors.dbd")
testErrors_registerRecordDeviceDriver(pdbbase)

testErrorsConfigure("PORT1",)
#asynSetTraceMask("PORT1",0,0xff)
asynSetTraceIOMask("PORT1",0,0x2)

dbLoadRecords("../../db/testErrors.db","P=testErrors:,R=test1:,PORT=PORT1,ADDR=0,TIMEOUT=1,SCAN=I/O Intr")
#dbLoadRecords("../../db/testErrors.db","P=testErrors:,R=test1:,PORT=PORT1,ADDR=0,TIMEOUT=1,SCAN=2 second")
dbLoadRecords("../../db/asynRecord.db","P=testErrors:,R=asyn1,PORT=PORT1,ADDR=0,OMAX=80,IMAX=80")
iocInit()

