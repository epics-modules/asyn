# testQueueOverflow

Demonstrates why an I/O Intr scanned asyn database
needs its EPICS general purpose callback queue sized for at least

```
(ring buffer depth) x (number of I/O Intr records at that scan priority)
```

and what happens when it isn't.

## Background

Every record that uses the standard `devAsynInt32` (and similar) device
support with `SCAN = "I/O Intr"` gets its own per-record ring buffer between
the driver and the record, with a default depth of 10
(`DEFAULT_RING_BUFFER_SIZE` in `devAsynInt32.c`, overridable per record with
the `asyn:FIFO` info tag). As long as that ring buffer is not full, every new
value the driver delivers via `registerInterruptUser()` calls
`scanIoRequest()`, which posts one entry onto the EPICS general purpose
callback queue for the record's scan priority (`cbLow`/`cbMedium`/`cbHigh`
- see `dbScan.c` and `callback.c` in EPICS Base). That queue has a fixed
size (`callbackSetQueueSize()`, default 2000, inspectable with the iocsh
command `callbackQueueShow()`) shared by every record at that priority in
the whole IOC.

If a driver receives data fast enough that N connected records can each
have up to 10 outstanding (unprocessed) values in their ring buffers at
once, the shared queue can see up to `N * 10` entries posted before a single
one of them is serviced. If the queue is smaller than that, `callbackRequest()`
starts silently dropping requests - the record is simply not scanned that
time, with no error on the record itself, only a `callbackRequest: ERROR
cb... ring buffer full` message on the console.

`testQueueOverflow` is a minimal asyn port driver with a configurable number
of `asynInt32` parameters ("CHANNEL0" .. "CHANNELn-1") and a `burst()` method
that pushes a configurable number of new values to every channel back-to-back
with no delay in between, simulating a driver that receives data faster than
the IOC can process it.

## Building

Build like any other app in this tree:

```
make -C testQueueOverflowApp
make -C iocBoot/ioctestQueueOverflow
```

## Running

Two st.cmd files load the same driver (8 channels) and the same 8 I/O Intr
scanned `longin` records (one per channel, `PRIO=LOW`), but size the `cbLow`
queue differently, and both trigger the same burst (200 updates/channel =
1600 interrupt callbacks) after `iocInit()`:

- `st.cmd` sets `callbackSetQueueSize(32)` - deliberately **too small**
  (8 channels x ring depth 10 = 80 needed).
- `st.cmd.large_queue` sets `callbackSetQueueSize(100)` - correctly sized,
  with headroom above the 80 minimum.

```
cd iocBoot/ioctestQueueOverflow
../../bin/<EPICS_HOST_ARCH>/testQueueOverflow st.cmd
../../bin/<EPICS_HOST_ARCH>/testQueueOverflow st.cmd.large_queue
```

Each script calls `callbackQueueShow(0)` once right after `iocInit()`
(baseline) and once again after the burst, so the two calls can be compared.

## What to look for

With the undersized queue (`st.cmd`), the burst prints `callbackRequest:
ERROR cbLow ring buffer full` several times, and the queue table changes
from:

```
PRIORITY  HIGH-WATER MARK  ITEMS IN Q  Q SIZE  % USED  Q OVERFLOWS
   cbLow                0           0      32     0.0            0
```

to:

```
PRIORITY  HIGH-WATER MARK  ITEMS IN Q  Q SIZE  % USED  Q OVERFLOWS
   cbLow               32           0      32     0.0            6
```

`Q OVERFLOWS` > 0 means some of the 1600 pushed updates were dropped before
their record ever got a chance to process them.

With the correctly sized queue (`st.cmd.large_queue`), the same burst drives
the queue to exactly the predicted worst case and no further:

```
PRIORITY  HIGH-WATER MARK  ITEMS IN Q  Q SIZE  % USED  Q OVERFLOWS
   cbLow               80          41     100    41.0            0
```

`HIGH-WATER MARK` == `8 channels * ring depth 10` == 80, and `Q OVERFLOWS`
stays 0: the queue never ran out of room, so no scan request was ever
dropped.

## Side effect: "N ring buffer overflows" warnings

Both runs also print a large number of lines like:

```
testQueueOverflow:Chan3 devAsynInt32::getCallbackValue warning, 2 ring buffer overflows
```

Do not mistake these for the queue overflow being demonstrated here - they
are a separate, expected effect of the *per-record* ring buffer (depth 10)
being much smaller than the burst (200 updates/channel). Once a record's own
ring buffer is full, `devAsynInt32` does not call `scanIoRequest()` again
for that value (see the comment in `interruptCallbackInput()` in
`devAsynInt32.c`); instead it just overwrites the oldest buffered value with
the newest one, so the record ends up processing the most recent value
instead of every value. This happens even with `st.cmd.large_queue`, where
the shared callback queue never overflows - it is a normal, harmless
consequence of pushing far more updates than the small per-record buffer can
hold, not a symptom of the callback-queue sizing problem this test is about.
If you need every single value delivered to a record (rather than only the
latest), the fix is a larger per-record ring buffer (`asyn:FIFO`), which
only makes the callback-queue sizing requirement demonstrated above larger,
not smaller.
