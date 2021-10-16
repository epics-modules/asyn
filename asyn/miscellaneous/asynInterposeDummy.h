/*asynInterposeDummy.h*/

/*
 * Example of an asynInterpose implementation
 * The dummy implementation does *nothing*,
 * simply forwards requests to the lower port.
 *
 * Use this as as a base example to build your asynInterpose.
 *
 * Author: davide.marcato@lnl.infn.it
 */

#ifndef asynInterposeDummy_H
#define asynInterposeDummy_H

#include <cantProceed.h>
#include <epicsAssert.h>
#include <epicsExport.h>
#include <epicsStdio.h>
#include <epicsString.h>
#include <iocsh.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asynDriver.h"
#include "asynOctet.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

// Dummy private data
typedef struct interposePvt {
  char *portName;
  asynInterface dummyInterface; /* This asynOctet interface */
  asynOctet *poctet;            /* The methods we're overriding */
  void *octetPvt;               /* Private data of next lower interface */
  asynUser *pasynUser;          /* For connect/disconnect reporting */
  epicsInt8 escape;             /* The escape char to deduplicate */
} interposePvt;

ASYN_API
int asynInterposeDummyConfig(const char *portName);

/* Connect/disconnect handling */
static void ExceptionHandler(asynUser *pasynUser, asynException exception);

/* asynOctet methods */
static asynStatus writeIt(void *ppvt, asynUser *pasynUser, const char *data, size_t numchars,
                          size_t *nbytesTransfered);
static asynStatus readIt(void *ppvt, asynUser *pasynUser, char *data, size_t maxchars,
                         size_t *nbytesTransfered, int *eomReason);
static asynStatus flushIt(void *ppvt, asynUser *pasynUser);
static asynStatus registerInterruptUser(void *ppvt, asynUser *pasynUser,
                                        interruptCallbackOctet callback, void *userPvt,
                                        void **registrarPvt);
static asynStatus cancelInterruptUser(void *drvPvt, asynUser *pasynUser, void *registrarPvt);
static asynStatus setInputEos(void *ppvt, asynUser *pasynUser, const char *eos, int eoslen);
static asynStatus getInputEos(void *ppvt, asynUser *pasynUser, char *eos, int eossize, int *eoslen);
static asynStatus setOutputEos(void *ppvt, asynUser *pasynUser, const char *eos, int eoslen);
static asynStatus getOutputEos(void *ppvt, asynUser *pasynUser, char *eos, int eossize,
                               int *eoslen);
static asynOctet octet = {
    writeIt,     readIt,      flushIt,      registerInterruptUser, cancelInterruptUser,
    setInputEos, getInputEos, setOutputEos, getOutputEos};

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* asynInterposeEos_H */
