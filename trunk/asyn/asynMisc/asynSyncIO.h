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

#ifndef	INCasynSyncIOh
#define	INCasynSyncIOh 1

#include <shareLib.h>
#include "asynDriver.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct asynSyncIO {
   asynStatus (*connect)(const char *port, int addr, asynUser **ppasynUser);
   asynStatus (*openSocket)(const char *server, int port, char **portName);
   int        (*write)(asynUser *pasynUser, char const *buffer, int buffer_len,
                  double timeout);
   int        (*read)(asynUser *pasynUser, char *buffer, int buffer_len, 
                  const char *ieos, int ieos_len, int flush, double timeout);
   int        (*writeRead)(asynUser *pasynUser,
                  const char *write_buffer, int write_buffer_len,
                  char *read_buffer, int read_buffer_len,
                  const char *ieos, int ieos_len, double timeout);
   asynStatus (*flush)(asynUser *pasynUser);
} asynSyncIO;
epicsShareExtern asynSyncIO *pasynSyncIO;

#ifdef __cplusplus
}
#endif

#endif /* INCasynSyncIOh */
