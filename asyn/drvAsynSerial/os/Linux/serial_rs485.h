#ifndef SERIAL_RS485
#define SERIAL_RS485

#include <sys/ioctl.h>
#include <linux/serial.h>
#include <linux/version.h>


/* Older Linux systems don't support RS485. 
 * The features we need were introduced in 2.6.35 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35)
  #define ASYN_RS485_SUPPORTED      1
  #define TIOCGRS485                0x542E
  #define TIOCSRS485                0x542F
#endif

#endif

