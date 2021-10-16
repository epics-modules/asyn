/*asynInterposeDummy.c*/

/*
 * Example of an asynInterpose implementation
 * The dummy implementation does *nothing*,
 * simply forwards requests to the lower port.
 *
 * Use this as as a base example to build your asynInterpose.
 *
 * Author: davide.marcato@lnl.infn.it
 */

#include "asynInterposeDummy.h"

ASYN_API
int asynInterposeDummyConfig(const char *portName) {
  interposePvt *pvt;
  asynInterface *plowerLevelInterface;
  asynStatus status;
  asynUser *pasynUser;
  int addr = 0;
  
  // Populate private data
  pvt = callocMustSucceed(1, sizeof(interposePvt), "asynInterposeDummy");
  pvt->portName = epicsStrDup(portName);
  pvt->dummyInterface.interfaceType = asynOctetType;
  pvt->dummyInterface.pinterface = &octet;
  pvt->dummyInterface.drvPvt = pvt;
  pasynUser = pasynManager->createAsynUser(0, 0);
  pvt->pasynUser = pasynUser;
  pvt->pasynUser->userPvt = pvt;

  // Connect
  status = pasynManager->connectDevice(pasynUser, portName, addr);
  if (status != asynSuccess) {
    printf("%s connectDevice failed\n", portName);
    pasynManager->freeAsynUser(pasynUser);
    free(pvt);
    return -1;
  }

  // Add callback
  status = pasynManager->exceptionCallbackAdd(pasynUser, ExceptionHandler);
  if (status != asynSuccess) {
    printf("%s exceptionCallbackAdd failed\n", portName);
    pasynManager->freeAsynUser(pasynUser);
    free(pvt);
    return -1;
  }

  // Interpose port
  status =
      pasynManager->interposeInterface(portName, addr, &pvt->dummyInterface, &plowerLevelInterface);
  if (status != asynSuccess) {
    printf("%s interposeInterface failed\n", portName);
    pasynManager->exceptionCallbackRemove(pasynUser);
    pasynManager->freeAsynUser(pasynUser);
    free(pvt);
    return -1;
  }
  pvt->poctet = (asynOctet *)plowerLevelInterface->pinterface;
  pvt->octetPvt = plowerLevelInterface->drvPvt;

  return (0);
}

// Catch all exceptions
static void ExceptionHandler(asynUser *pasynUser, asynException exception) {
  // On connections/disconnections
  // Occurs every streamdevice transaction
  if (exception == asynExceptionConnect) {
    // Default = do nothing
  }
}

/**
 * asynOctet methods
 */

static asynStatus writeIt(void *ppvt, asynUser *pasynUser, const char *data, size_t numchars,
                          size_t *nbytesTransfered) {
  interposePvt *pvt = (interposePvt *)ppvt;

  // Write on underlying port
  return pvt->poctet->write(pvt->octetPvt, pasynUser, data, numchars, nbytesTransfered);
}

static asynStatus readIt(void *ppvt, asynUser *pasynUser, char *data, size_t maxchars,
                         size_t *nbytesTransfered, int *eomReason) {
  interposePvt *pvt = (interposePvt *)ppvt;

  // Read from underlying port
  return pvt->poctet->read(pvt->octetPvt, pasynUser, data, maxchars, nbytesTransfered, eomReason);
}

static asynStatus flushIt(void *ppvt, asynUser *pasynUser) {
  interposePvt *pvt = (interposePvt *)ppvt;
  return pvt->poctet->flush(pvt->octetPvt, pasynUser);
}

static asynStatus registerInterruptUser(void *ppvt, asynUser *pasynUser,
                                        interruptCallbackOctet callback, void *userPvt,
                                        void **registrarPvt) {
  interposePvt *pvt = (interposePvt *)ppvt;
  return pvt->poctet->registerInterruptUser(pvt->octetPvt, pasynUser, callback, userPvt,
                                            registrarPvt);
}

static asynStatus cancelInterruptUser(void *ppvt, asynUser *pasynUser, void *registrarPvt) {
  interposePvt *pvt = (interposePvt *)ppvt;
  return pvt->poctet->cancelInterruptUser(pvt->octetPvt, pasynUser, registrarPvt);
}

static asynStatus setInputEos(void *ppvt, asynUser *pasynUser, const char *eos, int eoslen) {
  interposePvt *pvt = (interposePvt *)ppvt;
  return pvt->poctet->setInputEos(pvt->octetPvt, pasynUser, eos, eoslen);
}

static asynStatus getInputEos(void *ppvt, asynUser *pasynUser, char *eos, int eossize,
                              int *eoslen) {
  interposePvt *pvt = (interposePvt *)ppvt;
  return pvt->poctet->getInputEos(pvt->octetPvt, pasynUser, eos, eossize, eoslen);
}

static asynStatus setOutputEos(void *ppvt, asynUser *pasynUser, const char *eos, int eoslen) {
  interposePvt *pvt = (interposePvt *)ppvt;
  return pvt->poctet->setOutputEos(pvt->octetPvt, pasynUser, eos, eoslen);
}

static asynStatus getOutputEos(void *ppvt, asynUser *pasynUser, char *eos, int eossize,
                               int *eoslen) {
  interposePvt *pvt = (interposePvt *)ppvt;
  return pvt->poctet->getOutputEos(pvt->octetPvt, pasynUser, eos, eossize, eoslen);
}

/* register asynInterposeDummyConfig*/
static const iocshArg asynInterposeDummyConfigArg0 = {"portName", iocshArgString};
static const iocshArg *asynInterposeDummyConfigArgs[] = {&asynInterposeDummyConfigArg0};
static const iocshFuncDef asynInterposeDummyConfigFuncDef = {"asynInterposeDummyConfig", 1,
                                                             asynInterposeDummyConfigArgs};
static void asynInterposeDummyConfigCallFunc(const iocshArgBuf *args) {
  asynInterposeDummyConfig(args[0].sval);
}

static void asynInterposeDummyRegister(void) {
  static int firstTime = 1;
  if (firstTime) {
    firstTime = 0;
    iocshRegister(&asynInterposeDummyConfigFuncDef, asynInterposeDummyConfigCallFunc);
  }
}
epicsExportRegistrar(asynInterposeDummyRegister);
