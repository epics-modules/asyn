dbLoadDatabase("../../dbd/testQueueOverflow.dbd")
testQueueOverflow_registerRecordDeviceDriver(pdbbase)

# --- Demonstrate a general purpose callback queue that is TOO SMALL ------
#
# Each I/O Intr scanned record connected through devAsynInt32 has its own
# ring buffer, default depth 10 (DEFAULT_RING_BUFFER_SIZE in devAsynInt32.c,
# overridable per record with the "asyn:FIFO" info tag). As long as a
# record's ring buffer is not full, every new value the driver pushes posts
# one more entry on the shared "cbLow"/"cbMedium"/"cbHigh" general purpose
# callback queue (scanIoRequest() -> callbackRequest(); see dbScan.c and
# callback.c in EPICS Base).
#
# With NCHAN records connected at the same scan priority, a burst of new
# data that outruns record processing can therefore post up to
# NCHAN * 10 entries on that priority's queue before a single one of them
# has been serviced. The queue must be sized for at least that many
# entries, or callbackRequest() starts silently dropping requests -- the
# record is simply not scanned that time, with no error visible on the
# record itself.
#
# NCHAN=8 -> worst case needs 8 * 10 = 80 entries queued at once.
# This queue is set to only 32, so it WILL overflow: run the burst below
# and then look at the "Q OVERFLOWS" and "HIGH-WATER MARK" columns printed
# by callbackQueueShow(0), EPICS Base's iocsh command for inspecting the
# callback queues. Compare with st.cmd.large_queue, which sizes cbLow
# correctly (>= NCHAN * 10) for the same burst and shows no overflow.

epicsEnvSet("NCHAN","8")
epicsEnvSet("QSIZE","32")

# callbackSetQueueSize() only has an effect if called before iocInit()
callbackSetQueueSize($(QSIZE))

testQueueOverflowConfigure("PORT1", $(NCHAN))

# Enable ASYN_TRACE_WARNING so per-record ring buffer overflows are also
# printed on the console (in addition to the callback queue overflow).
asynSetTraceMask("PORT1",0,0x21)

dbLoadRecords("../../db/testQueueOverflowChan.db","P=testQueueOverflow:,PORT=PORT1,N=0,PRIO=LOW")
dbLoadRecords("../../db/testQueueOverflowChan.db","P=testQueueOverflow:,PORT=PORT1,N=1,PRIO=LOW")
dbLoadRecords("../../db/testQueueOverflowChan.db","P=testQueueOverflow:,PORT=PORT1,N=2,PRIO=LOW")
dbLoadRecords("../../db/testQueueOverflowChan.db","P=testQueueOverflow:,PORT=PORT1,N=3,PRIO=LOW")
dbLoadRecords("../../db/testQueueOverflowChan.db","P=testQueueOverflow:,PORT=PORT1,N=4,PRIO=LOW")
dbLoadRecords("../../db/testQueueOverflowChan.db","P=testQueueOverflow:,PORT=PORT1,N=5,PRIO=LOW")
dbLoadRecords("../../db/testQueueOverflowChan.db","P=testQueueOverflow:,PORT=PORT1,N=6,PRIO=LOW")
dbLoadRecords("../../db/testQueueOverflowChan.db","P=testQueueOverflow:,PORT=PORT1,N=7,PRIO=LOW")

iocInit()

# Baseline: the queue is empty and has never overflowed.
callbackQueueShow(0)

# Simulate a burst of high-rate incoming data: 200 new values per channel,
# delivered back-to-back with no delay in between, exactly as a fast
# hardware input would push samples faster than the IOC can process them.
testQueueOverflowBurst("PORT1", 200)

# With QSIZE=32 < NCHAN*10=80, cbLow's HIGH-WATER MARK will have hit 32 and
# Q OVERFLOWS will be > 0: some of the 1600 pushed updates were dropped
# before their record ever got a chance to process them.
callbackQueueShow(0)
