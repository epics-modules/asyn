/*
 * asynPortClient.cpp
 * 
 * Classes for asyn clients to communicate with asyn drivers
 *
 * Author: Mark Rivers
 *
 * Created October 27, 2014
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdexcept>

#define epicsExportSharedSymbols
#include <shareLib.h>
#include "asynPortClient.h"

//static const char *driverName = "asynPortClient";

/** Constructor for asynPortClient class
  * \param[in] portName  The name of the asyn port to connect to
  * \param[in] addr The address on the asyn port to connect to
  * \param[in] asynInterfaceType The name of the asynInterface to connect to (e.g.asynInt32, asynOctet, etc.)
  * \param[in] drvInfo  The drvInfo string to identify which property of the port is being connected to
  * \param[in] timeout The default timeout for all communications between the client and the port driver 
*/
asynPortClient::asynPortClient(const char *portName, int addr, const char *asynInterfaceType, const char *drvInfo, 
                       double timeout)
    : pasynUser_(NULL), pasynUserSyncIO_(NULL), timeout_(timeout), portName_(epicsStrDup(portName)),
      addr_(addr), asynInterfaceType_(epicsStrDup(asynInterfaceType)), drvInfo_(NULL)
    ,interruptPvt_(NULL)
{
    asynStatus status;
    asynInterface *pinterface;
    asynDrvUser *pDrvUser;

    if (drvInfo) drvInfo_=epicsStrDup(drvInfo);
    pasynUser_ = pasynManager->createAsynUser(0,0);
    status = pasynManager->connectDevice(pasynUser_, portName, addr);
    if (status) {
        throw std::runtime_error(std::string("connectDevice failed:").append(pasynUser_->errorMessage));
    }
    pasynInterface_ = pasynManager->findInterface(pasynUser_, asynInterfaceType, 1);
    if (!pasynInterface_) {
        throw std::runtime_error(std::string("findInterface failed:").append(asynInterfaceType));
    }
    if (!drvInfo) return;
    pinterface = pasynManager->findInterface(pasynUser_, asynDrvUserType, 1);
    if (!pinterface) return;
    pDrvUser = (asynDrvUser *)pinterface->pinterface;
    status = pDrvUser->create(pinterface->drvPvt, pasynUser_, drvInfo, 0, 0);
    if (status) {
        throw std::runtime_error(std::string("drvUser->create failed:"));
    }
}

/** Destructor for asynPortClient class
  * Frees all allocated resources 
*/
asynPortClient::~asynPortClient()
{
    if (portName_) free(portName_);
    if (asynInterfaceType_) free(asynInterfaceType_);
    if (drvInfo_) free(drvInfo_);
    if (pasynUser_) pasynManager->freeAsynUser(pasynUser_);
}

/** Reports the properties of this client
*/
void asynPortClient::report(FILE *fp, int details)
{
    fprintf(fp, "\n");
    fprintf(fp, "portName=%s\n", portName_);
    fprintf(fp, "addr=%d\n", addr_);
    fprintf(fp, "asynInterfaceType=%s\n", asynInterfaceType_);
    fprintf(fp, "drvInfo=%s\n", drvInfo_);
    fprintf(fp, "pasynUser=%p\n", pasynUser_);
}

