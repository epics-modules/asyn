/**********************************************************************
* Asyn device support using TCP stream or UDP datagram port           *
**********************************************************************/
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

#ifndef DRVASYNIPPORT_H
#define DRVASYNIPPORT_H

#include <shareLib.h>  

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

epicsShareFunc int drvAsynIPPortConfigure(const char *portName,
                                          const char *hostInfo,
                                          unsigned int priority,
                                          int noAutoConnect,
                                          int userFlags);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
#endif  /* DRVASYNIPPORT_H */
