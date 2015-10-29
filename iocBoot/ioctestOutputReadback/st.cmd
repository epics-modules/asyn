dbLoadDatabase("../../dbd/testOutputReadback.dbd")
testOutputReadback_registerRecordDeviceDriver(pdbbase)

testOutputReadbackConfigure("PORT1", 1)
#asynSetTraceMask("PORT1",0,0xff)
asynSetTraceIOMask("PORT1",0,0x2)

### Use PINI=NO on output records and SCAN=I/O Intr on input records
dbLoadRecords("../../db/testOutputReadback.db","P=testOutputReadback:,PORT=PORT1,ADDR=0,TIMEOUT=1,SCAN=I/O Intr,PINI=NO")

dbLoadRecords("../../db/asynRecord.db","P=testOutputReadback:,R=asyn1,PORT=PORT1,ADDR=0,OMAX=80,IMAX=80")

iocInit()


