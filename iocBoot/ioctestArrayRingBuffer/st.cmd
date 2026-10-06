dbLoadDatabase("../../dbd/testArrayRingBuffer.dbd")
testArrayRingBuffer_registerRecordDeviceDriver(pdbbase)

# Turn on asynTraceFlow and asynTraceError for global trace, i.e. no connected asynUser.
#asynSetTraceMask("", 0, 17)

# Third argument is canBlock; set it to 1 to make the port ASYN_CANBLOCK, so that
# writing to an output record leaves it asynchronously active (PACT=1) while the
# device write is in flight.
testArrayRingBufferConfigure("testARB", 100, 0)

dbLoadRecords("../../db/testArrayRingBuffer.db","P=testARB:,R=A1:,PORT=testARB,ADDR=0,TIMEOUT=1,NELM=100,RING_SIZE=10")

# Optional: an output array record with readbacks (asyn:READBACK), which exercises
# the devAsynXXXArray output path that requests processing with scanOnce().  The
# driver's own array callbacks drive the readback, so no writes to it are needed.
# RING_SIZE=0 selects the variant without a ring buffer.  Use scanOnceQueueShow()
# to see how many scanOnce queue entries the record uses.
#dbLoadRecords("../../db/testArrayRingBufferOut.db","P=testARB:,R=A1:,PORT=testARB,ADDR=0,TIMEOUT=1,NELM=100,RING_SIZE=10")

dbLoadRecords("../../db/asynRecord.db","P=testARB:,R=asyn1,PORT=testARB,ADDR=0,OMAX=80,IMAX=80")
#asynSetTraceMask("testARB",0,0x21)
#asynSetTraceMask("testARB",0,0xFF)
asynSetTraceIOMask("testARB",0,0x4)

iocInit()

