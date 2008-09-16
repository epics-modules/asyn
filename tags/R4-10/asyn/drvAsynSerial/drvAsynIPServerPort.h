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

#ifndef DRVASYNIPSERVERPORT_H
#define DRVASYNIPSERVERPORT_H

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

int drvAsynIPServerPortConfigure(const char *portName, const char *serverInfo,
                                 unsigned int maxClients, unsigned int priority, 
                                 int noAutoConnect, int noProcessEos);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
#endif  /* DRVASYNIPSERVERPORT_H */
