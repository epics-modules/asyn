/**********************************************************************
* Asyn device support using FTDI communications library                *
**********************************************************************/
#ifndef DRVASYNFTDIPORT_H
#define DRVASYNFTDIPORT_H

#include <shareLib.h>  

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

epicsShareFunc int drvAsynFTDIPortConfigure(const char *portname,
                                           const int vendor,
                                           const int product,
                                           const int baudrate,
                                           const int latency,
                                           unsigned int priority,
                                           int noAutoConnect,
                                           int noProcessEos);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
#endif  /* DRVASYNFTDIPORT_H */
