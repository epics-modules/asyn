/*asynSyncIO.h*/
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

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
#endif /* __cplusplus */

typedef struct asynSyncIO {
   asynStatus (*connect)(const char *port, int addr, asynUser **ppasynUser);
   asynStatus (*disconnect)(asynUser *pasynUser);
   asynStatus (*openSocket)(const char *server, int port, char **portName);
   int        (*write)(asynUser *pasynUser, char const *buffer, int buffer_len,
                  double timeout);
   int        (*read)(asynUser *pasynUser, char *buffer, int buffer_len, 
                  const char *ieos, int ieos_len, int flush, double timeout,
                  int *eomReason);
   int        (*writeRead)(asynUser *pasynUser,
                  const char *write_buffer, int write_buffer_len,
                  char *read_buffer, int read_buffer_len,
                  const char *ieos, int ieos_len, double timeout,
                  int *eomReason);
   asynStatus (*flush)(asynUser *pasynUser);
} asynSyncIO;
epicsShareExtern asynSyncIO *pasynSyncIO;

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* INCasynSyncIOh */
