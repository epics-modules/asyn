dbLoadDatabase("../../dbd/testArrayRingBuffer.dbd")
testArrayRingBuffer_registerRecordDeviceDriver(pdbbase)

# Turn on asynTraceFlow and asynTraceError for global trace, i.e. no connected asynUser.
#asynSetTraceMask("", 0, 17)

testArrayRingBufferConfigure("testARB", 100)

dbLoadRecords("../../db/testArrayRingBuffer.db","P=testARB:,R=A1:,PORT=testARB,ADDR=0,TIMEOUT=1,NELM=100,RING_SIZE=10")
dbLoadRecords("../../db/asynRecord.db","P=testARB:,R=asyn1,PORT=testARB,ADDR=0,OMAX=80,IMAX=80")
#asynSetTraceMask("testARB",0,0x21)
#asynSetTraceMask("testARB",0,0xFF)
asynSetTraceIOMask("testARB",0,0x4)

iocInit()

