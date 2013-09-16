dbLoadDatabase("../../dbd/testErrors.dbd")
testErrors_registerRecordDeviceDriver(pdbbase)

testErrorsConfigure("PORT1",)
#asynSetTraceMask("PORT1",0,0xff)
asynSetTraceIOMask("PORT1",0,0x2)

### Use periodic scanning and normal timestamp (TSE=0)
#dbLoadRecords("../../db/testErrors.db","P=testErrors:,PORT=PORT1,ADDR=0,TIMEOUT=1,TSE=0,SCAN=2 second")

### Use I/O Intr scanning and normal timestamp (TSE=0)
#dbLoadRecords("../../db/testErrors.db","P=testErrors:,PORT=PORT1,ADDR=0,TIMEOUT=1,TSE=0,SCAN=I/O Intr")

### Use periodic scanning and timestamp from device support (TSE=-2)
#dbLoadRecords("../../db/testErrors.db","P=testErrors:,PORT=PORT1,ADDR=0,TIMEOUT=1,TSE=-2,SCAN=2 second")

### Use I/O Intr scanning and timestamp from device support (TSE=-2)
dbLoadRecords("../../db/testErrors.db","P=testErrors:,PORT=PORT1,ADDR=0,TIMEOUT=1,TSE=-2,SCAN=I/O Intr")

### Use user-defined time stamp source by uncommenting this line
asynRegisterTimeStampSource("PORT1", "myTimeStampSource")

dbLoadRecords("../../db/asynRecord.db","P=testErrors:,R=asyn1,PORT=PORT1,ADDR=0,OMAX=80,IMAX=80")

asynSetOption("PORT1",0,"someKey","someValue")

iocInit()


