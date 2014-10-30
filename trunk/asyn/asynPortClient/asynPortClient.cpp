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

asynClient::asynClient(const char *portName, int addr, const char *asynInterfaceType, const char *drvInfo, 
                       double timeout)
    : pasynUser_(NULL), pasynUserSyncIO_(NULL), timeout_(timeout), portName_(epicsStrDup(portName)),
      addr_(addr), asynInterfaceType_(epicsStrDup(asynInterfaceType)), drvInfo_(epicsStrDup(drvInfo))
{
    asynStatus status;

    status = pasynManager->connectDevice(pasynUser_, portName, addr);
    if (status) {
        throw std::runtime_error(std::string("connectDevice failed:") + pasynUser_->errorMessage);
    }
    pasynInterface_ = pasynManager->findInterface(pasynUser_, asynInterfaceType, 1);
    if (status) {
        throw std::runtime_error(std::string("findInterface failed:") + asynInterfaceType);
    }
}

asynClient::~asynClient()
{
    if (portName_) free(portName_);
    if (asynInterfaceType_) free(asynInterfaceType_);
    if (drvInfo_) free(drvInfo_);
    if (pasynUser_) pasynManager->freeAsynUser(pasynUser_);
}

void asynClient::report(FILE *fp, int details)
{
    fprintf(fp, "\n");
    fprintf(fp, "portName=%s\n", portName_);
    fprintf(fp, "addr=%d\n", addr_);
    fprintf(fp, "asynInterfaceType=%s\n", asynInterfaceType_);
    fprintf(fp, "drvInfo=%s\n", drvInfo_);
    fprintf(fp, "pasynUser=%p\n", pasynUser_);
}

