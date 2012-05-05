/*  asynStandardInterfacesBase.c */
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

#include <epicsTypes.h>
#include <cantProceed.h>

#define epicsExportSharedSymbols
#include <shareLib.h>
#include "asynDriver.h"
#include "asynStandardInterfaces.h"

static asynStatus initialize(const char *portName, asynStandardInterfaces *pInterfaces, 
                             asynUser *pasynUser, void *pPvt)

{
    asynStatus status;
    
    if (pInterfaces->common.pinterface) {
        pInterfaces->common.interfaceType = asynCommonType;
        pInterfaces->common.drvPvt = pPvt;
        status = pasynManager->registerInterface(portName, &pInterfaces->common);
        if (status != asynSuccess) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
            "Can't register common");
            return(asynError);
        }
    }

    if (pInterfaces->drvUser.pinterface) {
        pInterfaces->drvUser.interfaceType = asynDrvUserType;
        pInterfaces->drvUser.drvPvt = pPvt;
        status = pasynManager->registerInterface(portName, &pInterfaces->drvUser);
        if (status != asynSuccess) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
            "Can't register drvUser");
            return(asynError);
        }
    }

    if (pInterfaces->option.pinterface) {
        pInterfaces->option.interfaceType = asynOptionType;
        pInterfaces->option.drvPvt = pPvt;
        status = pasynManager->registerInterface(portName, &pInterfaces->option);
        if (status != asynSuccess) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
            "Can't register option");
            return(asynError);
        }
    }

    if (pInterfaces->octet.pinterface) {
        pInterfaces->octet.interfaceType = asynOctetType;
        pInterfaces->octet.drvPvt = pPvt;
        status = pasynOctetBase->initialize(portName, &pInterfaces->octet, 
                                            pInterfaces->octetProcessEosIn, 
                                            pInterfaces->octetProcessEosIn,
                                            pInterfaces->octetInterruptProcess);
        if (status != asynSuccess) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
            "Can't register octet");
            return(asynError);
        }
        if (pInterfaces->octetCanInterrupt) {
            status = pasynManager->registerInterruptSource(portName, &pInterfaces->octet,
                                                           &pInterfaces->octetInterruptPvt);
            if (status != asynSuccess) {
                epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                "Can't register octet interrupt");
                return(asynError);
            }
        }
    }

    if (pInterfaces->uInt32Digital.pinterface) {
        pInterfaces->uInt32Digital.interfaceType = asynUInt32DigitalType;
        pInterfaces->uInt32Digital.drvPvt = pPvt;
        status = pasynUInt32DigitalBase->initialize(portName, &pInterfaces->uInt32Digital);
        if (status != asynSuccess) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
            "Can't register uInt32Digital");
            return(asynError);
        }
        if (pInterfaces->uInt32DigitalCanInterrupt) {
            status = pasynManager->registerInterruptSource(portName, &pInterfaces->uInt32Digital,
                                                           &pInterfaces->uInt32DigitalInterruptPvt);
            if (status != asynSuccess) {
                epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                "Can't register uInt32Digital interrupt");
                return(asynError);
            }
        }
    }

    if (pInterfaces->int32.pinterface) {
        pInterfaces->int32.interfaceType = asynInt32Type;
        pInterfaces->int32.drvPvt = pPvt;
        status = pasynInt32Base->initialize(portName, &pInterfaces->int32);
        if (status != asynSuccess) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
            "Can't register int32");
            return(asynError);
        }
        if (pInterfaces->int32CanInterrupt) {
            status = pasynManager->registerInterruptSource(portName, &pInterfaces->int32,
                                                           &pInterfaces->int32InterruptPvt);
            if (status != asynSuccess) {
                epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                "Can't register int32 interrupt");
                return(asynError);
            }
        }
    }

    if (pInterfaces->float64.pinterface) {
        pInterfaces->float64.interfaceType = asynFloat64Type;
        pInterfaces->float64.drvPvt = pPvt;
        status = pasynFloat64Base->initialize(portName, &pInterfaces->float64);
        if (status != asynSuccess) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
            "Can't register float64");
            return(asynError);
        }
        if (pInterfaces->float64CanInterrupt) {
            status = pasynManager->registerInterruptSource(portName, &pInterfaces->float64,
                                                           &pInterfaces->float64InterruptPvt);
            if (status != asynSuccess) {
                epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                "Can't register float64 interrupt");
                return(asynError);
            }
        }
    }

    if (pInterfaces->int8Array.pinterface) {
        pInterfaces->int8Array.interfaceType = asynInt8ArrayType;
        pInterfaces->int8Array.drvPvt = pPvt;
        status = pasynInt8ArrayBase->initialize(portName, &pInterfaces->int8Array);
        if (status != asynSuccess) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
            "Can't register int8Array");
            return(asynError);
        }
        if (pInterfaces->int8ArrayCanInterrupt) {
            status = pasynManager->registerInterruptSource(portName, &pInterfaces->int8Array,
                                                           &pInterfaces->int8ArrayInterruptPvt);
            if (status != asynSuccess) {
                epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                "Can't register int8Array interrupt");
                return(asynError);
            }
        }
    }

    if (pInterfaces->int16Array.pinterface) {
        pInterfaces->int16Array.interfaceType = asynInt16ArrayType;
        pInterfaces->int16Array.drvPvt = pPvt;
        status = pasynInt16ArrayBase->initialize(portName, &pInterfaces->int16Array);
        if (status != asynSuccess) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
            "Can't register int16Array");
            return(asynError);
        }
        if (pInterfaces->int16ArrayCanInterrupt) {
            status = pasynManager->registerInterruptSource(portName, &pInterfaces->int16Array,
                                                           &pInterfaces->int16ArrayInterruptPvt);
            if (status != asynSuccess) {
                epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                "Can't register int16Array interrupt");
                return(asynError);
            }
        }
    }

    if (pInterfaces->int32Array.pinterface) {
        pInterfaces->int32Array.interfaceType = asynInt32ArrayType;
        pInterfaces->int32Array.drvPvt = pPvt;
        status = pasynInt32ArrayBase->initialize(portName, &pInterfaces->int32Array);
        if (status != asynSuccess) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
            "Can't register int32Array");
            return(asynError);
        }
        if (pInterfaces->int32ArrayCanInterrupt) {
            status = pasynManager->registerInterruptSource(portName, &pInterfaces->int32Array,
                                                           &pInterfaces->int32ArrayInterruptPvt);
            if (status != asynSuccess) {
                epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                "Can't register int32Array interrupt");
                return(asynError);
            }
        }
    }

    if (pInterfaces->float32Array.pinterface) {
        pInterfaces->float32Array.interfaceType = asynFloat32ArrayType;
        pInterfaces->float32Array.drvPvt = pPvt;
        status = pasynFloat32ArrayBase->initialize(portName, &pInterfaces->float32Array);
        if (status != asynSuccess) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
            "Can't register float32Array");
            return(asynError);
        }
        if (pInterfaces->float32ArrayCanInterrupt) {
            status = pasynManager->registerInterruptSource(portName, &pInterfaces->float32Array,
                                                           &pInterfaces->float32ArrayInterruptPvt);
            if (status != asynSuccess) {
                epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                "Can't register float32Array interrupt");
                return(asynError);
            }
        }
    }

    if (pInterfaces->float64Array.pinterface) {
        pInterfaces->float64Array.interfaceType = asynFloat64ArrayType;
        pInterfaces->float64Array.drvPvt = pPvt;
        status = pasynFloat64ArrayBase->initialize(portName, &pInterfaces->float64Array);
        if (status != asynSuccess) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
            "Can't register float64Array");
            return(asynError);
        }
        if (pInterfaces->float64ArrayCanInterrupt) {
            status = pasynManager->registerInterruptSource(portName, &pInterfaces->float64Array,
                                                           &pInterfaces->float64ArrayInterruptPvt);
            if (status != asynSuccess) {
                epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                "Can't register float64Array interrupt");
                return(asynError);
            }
        }
    }

    if (pInterfaces->genericPointer.pinterface) {
        pInterfaces->genericPointer.interfaceType = asynGenericPointerType;
        pInterfaces->genericPointer.drvPvt = pPvt;
        status = pasynGenericPointerBase->initialize(portName, &pInterfaces->genericPointer);
        if (status != asynSuccess) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
            "Can't register genericPointer");
            return(asynError);
        }
        if (pInterfaces->genericPointerCanInterrupt) {
            status = pasynManager->registerInterruptSource(portName, &pInterfaces->genericPointer,
                                                           &pInterfaces->genericPointerInterruptPvt);
            if (status != asynSuccess) {
                epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                "Can't register genericPointer interrupt");
                return(asynError);
            }
        }
    }

    if (pInterfaces->Enum.pinterface) {
        pInterfaces->Enum.interfaceType = asynEnumType;
        pInterfaces->Enum.drvPvt = pPvt;
        status = pasynEnumBase->initialize(portName, &pInterfaces->Enum);
        if (status != asynSuccess) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
            "Can't register enum");
            return(asynError);
        }
        if (pInterfaces->enumCanInterrupt) {
            status = pasynManager->registerInterruptSource(portName, &pInterfaces->Enum,
                                                           &pInterfaces->enumInterruptPvt);
            if (status != asynSuccess) {
                epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                "Can't register enum interrupt");
                return(asynError);
            }
        }
    }
     
    return(asynSuccess);
}


static asynStandardInterfacesBase standardInterfacesBase = {initialize};
epicsShareDef asynStandardInterfacesBase *pasynStandardInterfacesBase = &standardInterfacesBase;

