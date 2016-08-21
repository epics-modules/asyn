#ifndef SERIAL_RS485
#define SERIAL_RS485

#include <sys/ioctl.h>
#include <linux/serial.h>
#include <linux/version.h>

#define TIOCGRS485      0x542E
#define TIOCSRS485      0x542F

/* Older Linux systems don't support RS485. 
 * The features we need were introduced in 2.6.35 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35)
  #define ASYN_RS485_SUPPORTED      1
#else
  #define SER_RS485_ENABLED         0x1
  #define SER_RS485_RTS_ON_SEND     0x2
  #define SER_RS485_RTS_AFTER_SEND  0x4
  struct serial_rs485 {
    int flags;
    unsigned int delay_rts_before_send;
    unsigned int delay_rts_after_send;
  };
#endif

#endif

