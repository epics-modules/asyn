/**********************************************************************
* Asyn device support using FTDI communications library                *
**********************************************************************/
#ifndef DRVASYNFTDIPORT_H
#define DRVASYNFTDIPORT_H

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define UART_SPI_BIT    0x01 // 0 = UART; 1 = SPI

ASYN_API int drvAsynFTDIPortConfigure(const char *portname,
                                           const int vendor,
                                           const int product,
                                           const int baudrate,
                                           const int latency,
                                           unsigned int priority,
                                           int noAutoConnect,
                                           int noProcessEos,
                                           int mode); // UART = 0x00; SPI = 0x01;

#ifdef __cplusplus
}
#endif  /* __cplusplus */
#endif  /* DRVASYNFTDIPORT_H */
