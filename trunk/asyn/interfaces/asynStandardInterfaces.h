/*  asynStandardInterfaces.h */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

/*  31-March-2008 Mark Rivers
 *
 *  This file is provided as a convenience to save a lot of code
 *  when writing drivers that use standard asyn interfaces.
*/

#ifndef asynStandardInterfacesH
#define asynStandardInterfacesH

#include <asynDriver.h>
#include <asynUInt32Digital.h>
#include <asynInt32.h>
#include <asynInt8Array.h>
#include <asynInt16Array.h>
#include <asynInt32Array.h>
#include <asynFloat32Array.h>
#include <asynFloat64.h>
#include <asynFloat64Array.h>
#include <asynGenericPointer.h>
#include <asynEnum.h>
#include <asynOctet.h>
#include <asynDrvUser.h>
#include <asynOption.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

typedef struct asynStandardInterfaces {
    asynInterface common;

    asynInterface drvUser;

    asynInterface option;

    asynInterface octet;
    int octetProcessEosIn;
    int octetProcessEosOut;
    int octetInterruptProcess;
    int octetCanInterrupt;
    void *octetInterruptPvt;

    asynInterface uInt32Digital;
    int uInt32DigitalCanInterrupt;
    void *uInt32DigitalInterruptPvt;

    asynInterface int32;
    int int32CanInterrupt;
    void *int32InterruptPvt;

    asynInterface float64;
    int float64CanInterrupt;
    void *float64InterruptPvt;

    asynInterface int8Array;
    int int8ArrayCanInterrupt;
    void *int8ArrayInterruptPvt;

    asynInterface int16Array;
    int int16ArrayCanInterrupt;
    void *int16ArrayInterruptPvt;

    asynInterface int32Array;
    int int32ArrayCanInterrupt;
    void *int32ArrayInterruptPvt;

    asynInterface float32Array;
    int float32ArrayCanInterrupt;
    void *float32ArrayInterruptPvt;

    asynInterface float64Array;
    int float64ArrayCanInterrupt;
    void *float64ArrayInterruptPvt;

    asynInterface genericPointer;
    int genericPointerCanInterrupt;
    void *genericPointerInterruptPvt;

    asynInterface Enum;
    int enumCanInterrupt;
    void *enumInterruptPvt;

} asynStandardInterfaces;

typedef struct asynStandardInterfacesBase {
    asynStatus (*initialize)(const char *portName, asynStandardInterfaces *pInterfaces, 
                             asynUser *pasynUser, void *pPvt);
} asynStandardInterfacesBase;

epicsShareExtern asynStandardInterfacesBase *pasynStandardInterfacesBase;

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* asynStandardInterfacesH */
