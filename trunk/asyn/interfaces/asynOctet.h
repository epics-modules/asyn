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

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define asynOctetType "asynOctet"
typedef struct asynOctet{
    asynStatus (*read)(void *drvPvt,asynUser *pasynUser,
                       char *data,int maxchars,int *nbytesTransfered);
    asynStatus (*write)(void *drvPvt,asynUser *pasynUser,
                        const char *data,int numchars,int *nbytesTransfered);
    asynStatus (*flush)(void *drvPvt,asynUser *pasynUser);
    asynStatus (*setEos)(void *drvPvt,asynUser *pasynUser,
                         const char *eos,int eoslen);
    asynStatus (*getEos)(void *drvPvt,asynUser *pasynUser,
                        char *eos, int eossize, int *eoslen);
}asynOctet;


#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /*asynOctetH*/
