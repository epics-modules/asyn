#include <epicsThread.h>

/*
 * These routines provide an operating-system-independent mechanism
 * for interrupting a thread which is performing a 'slow' system call
 * such as a read from a socket or serial port.
 *
 * Once epicsInterruptibleSyscallWasInterrupted() returns TRUE it will
 * continue to do so until epicsInterruptibleSyscallArm() has been called
 * to rearm the mechanism.
 * 
 * Some systems close the file descriptor as a side effect of unblocking
 * the slow system call.  The epicsInterruptibleSyscallWasClosed() function
 * is provided to detect this condition.
 *
 *
 * Example:
 * 1) In the device support 'init' routine:
 *         pdev->intrContext = *epicsInterruptibleSyscallMustCreate("dev");
 *
 * 2) In the device support 'connect' routine:
 *         epicsInterruptibleSyscallArm(pdev->intrContext, pdev->fd, myTid);
 *
 * 3) In the device support 'read' routine:
 *         epicsTimerStartDelay(pdev->timer, 10.0);
 *         i = read(pdev->fd, pdev->buf, pdev->bufsize);
 *         epicsTimerCancel(pdev->timer);
 *         if(epicsInterruptibleSyscallWasInterrupted(pdev->intrContext)) {
 *             errlogPrintf("Timed out while reading.\n");
 *             if (!epicsInterruptibleSyscallWasClosed(pdev->intrContext))
 *                 close(pdev->fd);
 *            return -1;
 *        }
 *         
 * 4) In the device support timer handler:
 *        epicsInterruptibleSyscallInterrupt(pdev->intrContext);
 *        epicsTimerStartDelay(pdev->timer, 10.0);
 *    The timer rearms itself to deal with the (unlikely) possibility that
 *        (a) the timer first times out before the read is entered and
 *        (b) that the read operation times out.
 */
 
struct epicsInterruptibleSyscallContext;
typedef struct epicsInterruptibleSyscallContext epicsInterruptibleSyscallContext;

epicsInterruptibleSyscallContext *epicsInterruptibleSyscallCreate(void);
epicsInterruptibleSyscallContext *epicsInterruptibleSyscallMustCreate(const char *msg);
int epicsInterruptibleSyscallArm(epicsInterruptibleSyscallContext *c, int fd, epicsThreadId tid);
int epicsInterruptibleSyscallDelete(epicsInterruptibleSyscallContext *c);

int epicsInterruptibleSyscallInterrupt(epicsInterruptibleSyscallContext *c);
int epicsInterruptibleSyscallWasInterrupted(epicsInterruptibleSyscallContext *c);
int epicsInterruptibleSyscallWasClosed(epicsInterruptibleSyscallContext *c);
