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

#define EOMCNT 0x0001 /*Request count reached*/
#define EOMEOS 0x0002 /*End of String detected*/
#define EOMEND 0x0004 /*End indicator detected*/

#define asynOctetType "asynOctet"
typedef struct asynOctet{
    asynStatus (*read)(void *drvPvt,asynUser *pasynUser,
                       char *data,int maxchars,int *nbytesTransfered,
                       int *eomReason);
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
