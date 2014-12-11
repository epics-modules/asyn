/*asynOctet.h*/
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
/* Interface supported by low level octet drivers. */

#ifndef asynOctetH
#define asynOctetH
#include <shareLib.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define ASYN_EOM_CNT 0x0001 /*Request count reached*/
#define ASYN_EOM_EOS 0x0002 /*End of String detected*/
#define ASYN_EOM_END 0x0004 /*End indicator detected*/

typedef void (*interruptCallbackOctet)(void *userPvt, asynUser *pasynUser,
                      char *data,size_t numchars, int eomReason);

typedef struct asynOctetInterrupt {
    asynUser *pasynUser;
    int      addr;
    interruptCallbackOctet callback;
    void *userPvt;
}asynOctetInterrupt;


#define asynOctetType "asynOctet"
typedef struct asynOctet{
    asynStatus (*write)(void *drvPvt,asynUser *pasynUser,
                    const char *data,size_t numchars,size_t *nbytesTransfered);
    asynStatus (*read)(void *drvPvt,asynUser *pasynUser,
                    char *data,size_t maxchars,size_t *nbytesTransfered,
                    int *eomReason);
    asynStatus (*flush)(void *drvPvt,asynUser *pasynUser);
    asynStatus (*registerInterruptUser)(void *drvPvt,asynUser *pasynUser,
                    interruptCallbackOctet callback, void *userPvt,
                    void **registrarPvt);
    asynStatus (*cancelInterruptUser)(void *drvPvt, asynUser *pasynUser,
                    void *registrarPvt);
    asynStatus (*setInputEos)(void *drvPvt,asynUser *pasynUser,
                    const char *eos,int eoslen);
    asynStatus (*getInputEos)(void *drvPvt,asynUser *pasynUser,
                    char *eos, int eossize, int *eoslen);
    asynStatus (*setOutputEos)(void *drvPvt,asynUser *pasynUser,
                    const char *eos,int eoslen);
    asynStatus (*getOutputEos)(void *drvPvt,asynUser *pasynUser,
                    char *eos, int eossize, int *eoslen);
}asynOctet;

/* asynOctetBase does the following:
   calls  registerInterface for asynOctet.
   Implements registerInterruptUser and cancelInterruptUser
   Provides default implementations of all methods.
   registerInterruptUser and cancelInterruptUser can be called
   directly rather than via queueRequest.
*/

#define asynOctetBaseType "asynOctetBase"
typedef struct asynOctetBase {
    asynStatus (*initialize)(const char *portName,
        asynInterface *pasynOctetInterface,
        int processEosIn,int processEosOut,int interruptProcess);
    void       (*callInterruptUsers)(asynUser *pasynUser,void *pasynPvt,
        char *data,size_t *nbytesTransfered,int *eomReason);
} asynOctetBase;
epicsShareExtern asynOctetBase *pasynOctetBase;

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /*asynOctetH*/
