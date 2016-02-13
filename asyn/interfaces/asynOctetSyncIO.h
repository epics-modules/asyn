/*asynOctetSyncIO.h*/
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

#ifndef INCasynOctetSyncIOh
#define INCasynOctetSyncIOh 1

#include <shareLib.h>
#include "asynDriver.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct asynOctetSyncIO {
   asynStatus (*connect)(const char *port, int addr,
                         asynUser **ppasynUser, const char *drvInfo);
   asynStatus (*disconnect)(asynUser *pasynUser);
   asynStatus (*write)(asynUser *pasynUser, char const *buffer, size_t buffer_len,
                  double timeout,size_t *nbytesTransfered);
   asynStatus (*read)(asynUser *pasynUser, char *buffer, size_t buffer_len, 
                  double timeout, size_t *nbytesTransfered,int *eomReason);
   asynStatus (*writeRead)(asynUser *pasynUser,
                  const char *write_buffer, size_t write_buffer_len,
                  char *read_buffer, size_t read_buffer_len,
                  double timeout,
                  size_t *nbytesOut, size_t *nbytesIn, int *eomReason);
   asynStatus (*flush)(asynUser *pasynUser);
   asynStatus (*setInputEos)(asynUser *pasynUser,
                  const char *eos,int eoslen);
   asynStatus (*getInputEos)(asynUser *pasynUser,
                  char *eos, int eossize, int *eoslen);
   asynStatus (*setOutputEos)(asynUser *pasynUser,
                  const char *eos,int eoslen);
   asynStatus (*getOutputEos)(asynUser *pasynUser,
                  char *eos, int eossize, int *eoslen);
   asynStatus (*writeOnce)(const char *port, int addr,
                  char const *buffer, size_t buffer_len, double timeout,
                  size_t *nbytesTransfered, const char *drvInfo);
   asynStatus (*readOnce)(const char *port, int addr,
                  char *buffer, size_t buffer_len, double timeout,
                  size_t *nbytesTransfered,int *eomReason, const char *drvInfo);
   asynStatus (*writeReadOnce)(const char *port, int addr,
                  const char *write_buffer, size_t write_buffer_len,
                  char *read_buffer, size_t read_buffer_len,
                  double timeout,
                  size_t *nbytesOut, size_t *nbytesIn, int *eomReason,
                  const char *drvInfo);
   asynStatus (*flushOnce)(const char *port, int addr,const char *drvInfo);
   asynStatus (*setInputEosOnce)(const char *port, int addr,
                  const char *eos,int eoslen,const char *drvInfo);
   asynStatus (*getInputEosOnce)(const char *port, int addr,
                  char *eos, int eossize, int *eoslen,const char *drvInfo);
   asynStatus (*setOutputEosOnce)(const char *port, int addr,
                  const char *eos,int eoslen,const char *drvInfo);
   asynStatus (*getOutputEosOnce)(const char *port, int addr,
                  char *eos, int eossize, int *eoslen,const char *drvInfo);
} asynOctetSyncIO;
epicsShareExtern asynOctetSyncIO *pasynOctetSyncIO;

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* INCasynOctetSyncIOh */
