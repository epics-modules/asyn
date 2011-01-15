/* drvVxi11.h */

/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

/*
 *Common part of interfaces to generic GPIB driver
 *
 * Current Author: Benjamin Franksen
 * Original Author: John Winans
 */
#ifndef drvVxi11H
#define drvVxi11H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern int vxi11Configure(char *portName, char *hostName, int flags,
        char *defTimeoutString,
        char *vxiName,
        unsigned int priority,
        int noAutoConnect);
extern int vxi11SetRpcTimeout(double timeout);

extern int E5810Reboot(char * inetAddr,char *password);
extern int E2050Reboot(char * inetAddr);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /*drvVxi11H*/
