/*****************************************************************
                          COPYRIGHT NOTIFICATION
*****************************************************************

(C)  COPYRIGHT 1993 UNIVERSITY OF CHICAGO

This software was developed under a United States Government license
described on the COPYRIGHT_UniversityOfChicago file included as part
of this distribution.
**********************************************************************/

/*
 *      Original Author: Mark Rivers
 *
 * Modification Log:
 * -----------------
 * 01-Mar-2004  Mark Rivers, created from old serialIO.h
 */

#include <shareLib.h>
#include "asynDriver.h"
#include "drvGenericSerial.h"

#ifndef	INCasynSyncIOh
#define	INCasynSyncIOh 1

#ifdef __cplusplus
extern "C" {
#endif
epicsShareFunc asynStatus epicsShareAPI
    asynSyncIOConnect(const char *port, int addr, asynUser **ppasynUser);
epicsShareFunc asynStatus epicsShareAPI
    asynSyncIOConnectSocket(const char *server, int port, asynUser **ppasynUser);
epicsShareFunc int epicsShareAPI
    asynSyncIOWrite(asynUser *pasynUser, char const *buffer, int buffer_len,
                    double timeout);
epicsShareFunc asynStatus epicsShareAPI
    asynSyncIOFlush(asynUser *pasynUser);
epicsShareFunc int epicsShareAPI
    asynSyncIORead(asynUser *pasynUser, char *buffer, int buffer_len,
                   const char *ieos, int ieos_len, int flush, double timeout);
epicsShareFunc int epicsShareAPI
    asynSyncIOWriteRead(asynUser *pasynUser,
                        const char *write_buffer, int write_buffer_len,
                        char *read_buffer, int read_buffer_len,
                        const char *ieos, int ieos_len, double timeout);

#ifdef __cplusplus
}
#endif

#endif
