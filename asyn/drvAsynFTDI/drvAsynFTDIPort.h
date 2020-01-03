/**********************************************************************
* Asyn device support using FTDI communications library                *
**********************************************************************/
#ifndef DRVASYNFTDIPORT_H
#define DRVASYNFTDIPORT_H

#include <shareLib.h>  

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define NO_PROC_EOS_BIT 0x01
#define UART_SPI_BIT    0x02 // 0 = UART; 1 = SPI

epicsShareFunc int drvAsynFTDIPortConfigure(const char *portname,
                                           const int vendor,
                                           const int product,
                                           const int baudrate,
                                           const int latency,
                                           unsigned int priority,
                                           int noAutoConnect,
                                           int flags);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
#endif  /* DRVASYNFTDIPORT_H */
