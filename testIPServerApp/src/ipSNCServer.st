program ipSNCServer("P=testIPServer:=, PORT=P5000")

option +r;

%%#include <string.h>
%%#include <epicsString.h>
%%#include <epicsEvent.h>
%%#include <asynDriver.h>
%%#include <asynOctet.h>
%%#include <asynOctetSyncIO.h>

#define BUFFER_SIZE 80

string input;  assign input     to "{P}stringInput";
string output; assign output    to "{P}stringOutput";
int connected; assign connected to "{P}connected";

char *listenerPortName;
char *IOPortName;
int readStatus;
int writeStatus;
char *pasynUser;    /* This is really asynUser* */
char *registrarPvt; /* This is really void* */
char *eventId;      /* Thus is really epicsEventId */

/* Define escaped C functions at end of file */
%% static void initialize(SS_ID ssId, struct UserVar *pvar);
%% static int readSocket(SS_ID ssId, struct UserVar *pvar);
%% static int writeSocket(SS_ID ssId, struct UserVar *pvar);

ss ipSNCServer
{
    state init {
        when() {
            listenerPortName = macValueGet("PORT");
            %%initialize(ssId, pVar);
            connected = 0;
        } state waitConnect
    }

    state waitConnect {
        when() {
            pvPut(connected);
            %%epicsEventWait( (epicsEventId)pVar->eventId);
            pvPut(connected);
        } state processCommands
    }

    state processCommands {
        when(connected) {
            %%pVar->readStatus = readSocket(ssId, pVar);
            if (readStatus == 0) {
                pvPut(input);
                strcpy(output, "OK");
                %%pVar->writeStatus = writeSocket(ssId, pVar);
                if (writeStatus == 0) {
                    pvPut(output);
                }
            }
        } state processCommands

        when(!connected) {
        } state waitConnect
 
    }
}

%{
static void connectionCallback(void *drvPvt, asynUser *pasynUserIn, char *portName, size_t len, int eomReason)
{
    struct UserVar  *pvar = (struct UserVar *)drvPvt;
    asynUser *pasynUser;
    asynStatus status;

    pvar->IOPortName = epicsStrDup(portName);
    status = pasynOctetSyncIO->connect(portName, 0, &pasynUser, NULL);
    pvar->pasynUser = (char *)pasynUser;
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
              "ipSNCServer: connectionCallback, portName=%s\n", portName);
    if (status) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
                  "ipSNCServer:connectionCallback: unable to connect to port %s\n",
                  portName);
        return;
    }
    status = pasynOctetSyncIO->setInputEos(pasynUser, "\r\n", 2);
    if (status) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
                  "ipSNCServer:connectionCallback: unable to set input EOS on %s: %s\n",
                  portName, pasynUser->errorMessage);
        return;
    }
    status = pasynOctetSyncIO->setOutputEos(pasynUser, "\r\n", 2);
    if (status) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
                  "ipSNCServer:connectionCallback: unable to set output EOS on %s: %s\n",
                  portName, pasynUser->errorMessage);
        return;
    }
    /* Send an EPICS event to wake up the SNL program */
    pvar->connected = 1;
    epicsEventSignal( (epicsEventId)pvar->eventId);
}

static void initialize(SS_ID ssId, struct UserVar *pvar)
{
    int addr=0;
    asynInterface *pasynInterface;
    asynUser *pasynUser;
    asynOctet *pasynOctet;
    void *registrarPvt;
    int status;

    pvar->eventId = (char *)epicsEventCreate(epicsEventEmpty);
    pasynUser = pasynManager->createAsynUser(0,0);
    pvar->pasynUser = (char *)pasynUser;
    pasynUser->userPvt = pvar;
    status = pasynManager->connectDevice(pasynUser, pvar->listenerPortName, addr);
    if(status!=asynSuccess) {
        printf("can't connect to port %s: %s\n", pvar->listenerPortName, pasynUser->errorMessage);
        return;
    }
    pasynInterface = pasynManager->findInterface(pasynUser,asynOctetType,1);
    if(!pasynInterface) {
        printf("%s driver not supported\n",asynOctetType);
        return;
    }
    pasynOctet = (asynOctet *)pasynInterface->pinterface;
    status = pasynOctet->registerInterruptUser(
                 pasynInterface->drvPvt, pasynUser,
                 connectionCallback,pvar, &registrarPvt);
    pvar->registrarPvt = registrarPvt;
    if(status!=asynSuccess) {
        printf("ipSNCServer devAsynOctet registerInterruptUser %s\n",
               pasynUser->errorMessage);
    }
}

static int readSocket(SS_ID ssId, struct UserVar *pvar)
{
    char buffer[BUFFER_SIZE];
    size_t nread;
    int eomReason;
    asynUser *pasynUser = (asynUser *)pvar->pasynUser;
    asynStatus status;

    status = pasynOctetSyncIO->read(pasynUser, buffer, BUFFER_SIZE,
                                    -1.0, &nread, &eomReason);
    if (status) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
                  "ipSNCServer:readSocket: read error on: %s: %s\n",
                  pvar->IOPortName, pasynUser->errorMessage);
        pvar->connected = 0;
        strcpy(pvar->input, "");
    }
    else {
        asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,
                  "ipSNCServer:readSocket: %s read %s\n",
                   pvar->IOPortName, buffer);
        strcpy(pvar->input, buffer);
    }
    return(status);
}

static int writeSocket(SS_ID ssId, struct UserVar *pvar)
{
    size_t nwrite;
    asynUser *pasynUser = (asynUser *)pvar->pasynUser;
    asynStatus status;

    status = pasynOctetSyncIO->write(pasynUser, pvar->output, strlen(pvar->output),
                                     0.0, &nwrite);
    if (status) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
                  "ipSNCServer:writeSocket: write error on: %s: %s\n",
                  pvar->IOPortName, pasynUser->errorMessage);
        pvar->connected = 0;
    }
    else {
        asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,
                   "ipSNCServer:writeSocket: %s write %s\n",
                   pvar->IOPortName, pvar->output);
    }
    return(status);
}

}%
