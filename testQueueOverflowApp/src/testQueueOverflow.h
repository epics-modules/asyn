/*
 * testQueueOverflow.h
 *
 * Asyn port driver that simulates a high-rate input device (e.g. many samples
 * per second arriving from hardware) in order to demonstrate how asynManager's
 * generic device support (devAsynInt32, etc.) turns every new value delivered
 * through registerInterruptUser() into one scanIoRequest()/callbackRequest()
 * on the EPICS general purpose callback queue (see callback.c in EPICS Base).
 *
 * Each I/O Intr scanned record has its own per-record ring buffer (default
 * depth 10, see DEFAULT_RING_BUFFER_SIZE in devAsynInt32.c, overridable with
 * the "asyn:FIFO" info tag). As long as that ring buffer is not yet full,
 * every new value queues one more entry on the shared callback queue for the
 * record's PRIO. That queue (cbLow/cbMedium/cbHigh) has a fixed size
 * (default 2000, see callbackSetQueueSize()/callbackQueueShow() in Base), so
 * with N records connected at the same priority, a burst of updates that
 * outruns record processing can post up to N * 10 entries before any of them
 * are serviced -- the queue must be sized for at least that many entries or
 * it will overflow and updates will be silently lost.
 *
 * Author: Ralph Lange
 */

#ifndef testQueueOverflow_H
#define testQueueOverflow_H

#include "asynPortDriver.h"

/** Asyn port driver with a configurable number of asynInt32 parameters
  * ("CHANNEL0" .. "CHANNELn-1"), each usable as the drvInfo for one
  * I/O Intr scanned record. burst() pushes a configurable number of new
  * values to every channel back-to-back, without yielding, to simulate
  * a driver that receives data faster than records can be processed. */
class testQueueOverflow : public asynPortDriver {
public:
    testQueueOverflow(const char *portName, int numChannels);
    ~testQueueOverflow();

    /* Called from the iocsh testQueueOverflowBurst() command */
    void burst(int numUpdates);

private:
    int numChannels_;
    int *channelParam_;
};

#endif
