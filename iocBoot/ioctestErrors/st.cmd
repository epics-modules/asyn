dbLoadDatabase("../../dbd/testErrors.dbd")
testErrors_registerRecordDeviceDriver(pdbbase)

# The second argument is ASYN_CANBLOCK.  0 for a synchronous driver, 1 for asynchronous
testErrorsConfigure("PORT1",1)
#asynSetTraceMask("PORT1",0,0xff)
asynSetTraceIOMask("PORT1",0,0x2)

### Use periodic scanning and normal timestamp (TSE=0) and no ring buffer on string and waveform records
#dbLoadRecords("../../db/testErrors.db","P=testErrors:,PORT=PORT1,ADDR=0,TIMEOUT=1,TSE=0,SCAN=2 second,FIFO=0")

### Use I/O Intr scanning and normal timestamp (TSE=0) and no ring buffer on string and waveform records
#dbLoadRecords("../../db/testErrors.db","P=testErrors:,PORT=PORT1,ADDR=0,TIMEOUT=1,TSE=0,SCAN=I/O Intr,FIFO=0")

### Use periodic scanning and timestamp from device support (TSE=-2) and no ring buffer on string and waveform records
#dbLoadRecords("../../db/testErrors.db","P=testErrors:,PORT=PORT1,ADDR=0,TIMEOUT=1,TSE=-2,SCAN=2 second,FIFO=0")

### Use I/O Intr scanning and timestamp from device support (TSE=-2) and no ring buffer on string and waveform records
#dbLoadRecords("../../db/testErrors.db","P=testErrors:,PORT=PORT1,ADDR=0,TIMEOUT=1,TSE=-2,SCAN=I/O Intr,FIFO=0")

### Use I/O Intr scanning and timestamp from device support (TSE=-2) and 5 element ring buffer on string and waveform records
dbLoadRecords("../../db/testErrors.db","P=testErrors:,PORT=PORT1,ADDR=0,TIMEOUT=1,TSE=-2,SCAN=I/O Intr,FIFO=5")

### Use user-defined time stamp source by uncommenting this line
asynRegisterTimeStampSource("PORT1", "myTimeStampSource")

dbLoadRecords("../../db/asynRecord.db","P=testErrors:,R=asyn1,PORT=PORT1,ADDR=0,OMAX=80,IMAX=80")

asynSetOption("PORT1",0,"someKey","someValue")

iocInit()


