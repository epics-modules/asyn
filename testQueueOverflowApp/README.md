# testQueueOverflow

Demonstrates and verifies that an I/O Intr scanned asyn record never needs more
than **one** entry of the EPICS general purpose callback queue, no matter how
deep its ring buffer is or how fast the driver delivers data.

## Background

Every record that uses the standard `devAsynInt32` (and similar) device support
with `SCAN = "I/O Intr"` gets its own per-record ring buffer between the driver
and the record, with a default depth of 10 (`DEFAULT_RING_BUFFER_SIZE` in
`devAsynInt32.c`, overridable per record with the `asyn:FIFO` info tag). Each
new value the driver delivers via `registerInterruptUser()` is pushed into that
ring buffer, and the record is asked to process via `scanIoRequest()`, which
posts an entry onto the EPICS general purpose callback queue for the record's
scan priority (`cbLow`/`cbMedium`/`cbHigh` - see `dbScan.c` and `callback.c` in
EPICS Base). That queue has a fixed size (`callbackSetQueueSize()`, default
2000, inspectable with the iocsh command `callbackQueueShow()`) shared by every
record at that priority in the whole IOC.

**Previously** every buffered value posted its own queue entry. N records each
holding up to 10 unprocessed values could therefore post `N * 10` entries before
any of them was serviced, and if the queue was smaller than that,
`callbackRequest()` silently dropped requests - the record was simply not
scanned that time, with no error on the record itself, only a
`callbackRequest: ERROR cb... ring buffer full` message on the console. Sizing
the queue then required knowing every record's ring buffer depth.

**Now** at most one process request is outstanding per record: an interrupt
callback only calls `scanIoRequest()` if no request is pending, and
`getCallbackValue()` issues the next request as it pops each value while the
ring buffer is still not empty. The re-request happens while the ring buffer
lock is still held, so an interrupt callback either sees the request still
pending and relies on the consumer to re-request, or sees it cleared and
requests itself - a value can never be left in the ring buffer with no request
outstanding. The queue therefore needs `N` entries for `N` I/O Intr records at
a given priority, independent of ring buffer depth and data rate.

`testQueueOverflow` is a minimal asyn port driver with a configurable number of
`asynInt32` parameters ("CHANNEL0" .. "CHANNELn-1") and a `burst()` method that
pushes a configurable number of new values to every channel, either
back-to-back or paced by a delay.

## Building

Build like any other app in this tree:

```
make -C testQueueOverflowApp
make -C iocBoot/ioctestQueueOverflow
```

## Running

Both st.cmd files load 8 I/O Intr scanned `longin` records (one per channel,
`PRIO=LOW`) against a deliberately small 32-entry `cbLow` queue, and differ only
in the per-record ring buffer depth:

- `st.cmd` - ring buffer depth 10 (the default).
- `st.cmd.deep_fifo` - ring buffer depth 100, ten times as many values able to
  sit unprocessed per record. Under the old mechanism this would have required
  `8 * 100 = 800` queue entries.

```
cd iocBoot/ioctestQueueOverflow
../../bin/<EPICS_HOST_ARCH>/testQueueOverflow st.cmd
../../bin/<EPICS_HOST_ARCH>/testQueueOverflow st.cmd.deep_fifo
```

Each script runs two bursts, calling `callbackQueueShow(0)` before and after so
the numbers can be compared.

## What to look for

**Burst 1 - back-to-back (200 values/channel, faster than the IOC can
process).** The queue peaks at one entry per record and never overflows:

```
PRIORITY  HIGH-WATER MARK  ITEMS IN Q  Q SIZE  % USED  Q OVERFLOWS
   cbLow                8           7      32    21.9            0
```

`HIGH-WATER MARK` == 8 == the number of I/O Intr records, and `Q OVERFLOWS`
stays 0 - with a 32-entry queue, 1600 pushed values, and (in
`st.cmd.deep_fifo`) a ring buffer depth of 100. There are no
`callbackRequest: ERROR cbLow ring buffer full` messages.

**Burst 2 - paced (300 values/channel, slower than record processing).** Each
value is consumed before the next arrives, so the ring buffers repeatedly go
empty and refill. This is the case a missed process request would break: the
ring buffer would back up, report `ring buffer overflows` warnings, and finally
leave records stuck at a stale value. Expect no overflow warnings during the
paced burst, and every record ending at the last value pushed:

```
DBF_LONG:           299 = 0x12b
```

Note that a `ring buffer overflows` warning may still be printed just after the
paced burst starts - that is the overflow count accumulated during burst 1,
reported the next time a value is popped, not a loss in the paced phase.

## Side effect of burst 1: "N ring buffer overflows" warnings

Burst 1 also prints a large number of lines like:

```
testQueueOverflow:Chan3 devAsynInt32::getCallbackValue warning, 2 ring buffer overflows
```

These are **not** callback queue overflows. They are the expected effect of the
*per-record* ring buffer (depth 10) being much smaller than the burst (200
values/channel). When a record's ring buffer is full, `devAsynInt32` overwrites
the oldest buffered value with the newest one, so the record ends up processing
the most recent value rather than every value. This is normal and unrelated to
callback queue sizing - it happens with any queue size, and it is exactly why
the ring buffer guarantees the record eventually sees the latest value. If you
need every single value delivered to a record, use a larger per-record ring
buffer (`asyn:FIFO`), which - as `st.cmd.deep_fifo` shows - does *not* increase
the callback queue requirement.
