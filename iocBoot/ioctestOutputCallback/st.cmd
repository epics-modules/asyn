dbLoadDatabase("../../dbd/testOutputCallback.dbd")
testOutputCallback_registerRecordDeviceDriver(pdbbase)

# Arguments: Portname, canBlock
testOutputCallbackConfigure("PORT1", 0)
# Enable ASYN_TRACE_WARNING
asynSetTraceMask("PORT1",0,0x21)
asynSetTraceIOMask("PORT1",0,0x2)

### Use PINI=NO on output records and SCAN=I/O Intr on input records
dbLoadRecords("../../db/testOutputCallback.db","P=testOutputCallback:,PORT=PORT1,ADDR=0,TIMEOUT=1,PINI=NO")

dbLoadRecords("../../db/asynRecord.db","P=testOutputCallback:,R=asyn1,PORT=PORT1,ADDR=0,OMAX=80,IMAX=80")

iocInit()


