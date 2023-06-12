/*************************************************************************\
* Copyright (c) 2016 Michael Davidsaver
 * EPICS BASE is distributed subject to a Software License Agreement found
 * in file LICENSE that is included with this distribution.
 \*************************************************************************/

#include <stdexcept>

#include <string.h>

#include <epicsGuard.h>
#include <epicsThread.h>
#include <epicsUnitTest.h>
#include <testMain.h>

#include <asynPortDriver.h>
#include <asynPortClient.h>

// Need interrupt accept from dbAccess.h unless asyn is built with EPICS_LIBCOM_ONLY
#ifdef EPICS_LIBCOM_ONLY
    static int interruptAccept;
#else
    #include <dbAccess.h>
#endif

#ifdef __rtems__
// no test data needed (when running individual CI tests)
const void* epicsRtemsFSImage = 0;
#endif

namespace {

typedef epicsGuard<asynPortDriver> Guard;

epicsInt32 lastint32;
size_t cbcount;

void int32cb(void *userPvt, asynUser *pasynUser, epicsInt32 data)
{
    (void)pasynUser;
    testDiag("int32cb() called with %d", (int)data);
    cbcount++;
    asynInt32Client *client = (asynInt32Client*)userPvt;
    lastint32 = data;
    epicsInt32 other;
    testOk1(client->read(&other)==asynSuccess);
    testOk1(data==other);
    // ensure recursive call doesn't lead to callback loop
    // Don't do this or boom!  There goes the stack
    //testOk1(client->write(data+1));
    testDiag("int32cb() done");
}

static asynPortDriver *instantiateDriver(const char *portName, bool autoDestroy) {
    return new asynPortDriver(portName, 0,
                              asynDrvUserMask|asynInt32Mask,
                              asynInt32Mask,
                              autoDestroy ? ASYN_DESTRUCTIBLE : 0,
                              0, 0,
                              epicsThreadGetStackSize(epicsThreadStackSmall));
}

void testA(asynPortDriver *portA)
{
    int idx1=-1, idx2=-1, sevr, ival;
    const char *name;
    double dval;

    lastint32 = 0;
    cbcount = 0;

    testDiag("Basic parameter creation");

    testOk1(portA->findParam(0, "int32", &idx1)==asynParamNotFound);
    testOk1(portA->getParamName(0, 0, &name)==asynParamBadIndex);
    testOk1(portA->getParamAlarmSeverity(0, &sevr)==asynParamBadIndex);
    testOk1(portA->getParamAlarmStatus(0, &sevr)==asynParamBadIndex);

    testOk1(portA->createParam(0, "int32", asynParamInt32, &idx1)==asynSuccess);

    testOk1(portA->findParam(0, "int32", &idx2)==asynSuccess);
    testOk1(idx1==idx2);
    testOk1(portA->getParamName(0, 0, &name)==asynSuccess);
    testOk1(strcmp(name, "int32")==0);
    testOk1(portA->getParamAlarmSeverity(0, &sevr)==asynSuccess);
    testOk1(sevr==0);
    testOk1(portA->getParamAlarmStatus(0, &sevr)==asynSuccess);
    testOk1(sevr==0);
    testOk1(portA->getDoubleParam(0, idx1, &dval)==asynParamWrongType);
    testOk1(portA->getIntegerParam(0, idx1, &ival)==asynParamUndefined);

    testOk1(portA->createParam(0, "int32", asynParamInt32, &idx2)==asynError);

    testOk1(portA->createParam(0, "int64", asynParamInt64, &idx1)==asynSuccess);
    testOk1(portA->createParam(0, "float64", asynParamFloat64, &idx1)==asynSuccess);
    testOk1(portA->createParam(0, "uint32", asynParamUInt32Digital, &idx1)==asynSuccess);
    testOk1(portA->createParam(0, "y", asynParamInt32, &idx1)==asynSuccess);
    testOk1(portA->createParam(0, "z", asynParamFloat64, &idx1)==asynSuccess);
    // this is the 6th parameter
    testOk1(portA->createParam(0, "more", asynParamFloat64, &idx1)==asynSuccess);

    testOk1(portA->findParam(0, "more", &idx2)==asynSuccess);
    // this is a string parameter
    testOk1(portA->createParam(0, "string", asynParamOctet, &idx1)==asynSuccess);

    {
        Guard G(*portA);
        testOk1(portA->findParam(0, "int32", &idx1)==asynSuccess);

        testOk1(portA->setDoubleParam(0, idx1, 4.2)==asynParamWrongType);
        testOk1(portA->setIntegerParam(0, idx1, 42)==asynSuccess);

        testOk1(portA->getIntegerParam(0, idx1, &ival)==asynSuccess);
        testOk1(ival==42);
    }

    {
        Guard G(*portA);
        std::string stdString;
        char str[20];

        testOk1(portA->findParam(0, "string", &idx1)==asynSuccess);

        testOk1(portA->setStringParam(0, idx1, "Testing 123")==asynSuccess);
        testOk1(portA->getStringParam(0, idx1, sizeof(str)-1, str)==asynSuccess);
        testOk1(strcmp(str, "Testing 123")==0);
        testOk1(portA->getStringParam(0, idx1, stdString)==asynSuccess);
        testOk1(stdString == "Testing 123");
        stdString = "Hello world";
        testOk1(portA->setStringParam(0, idx1, stdString)==asynSuccess);
        stdString = "";
        testOk1(portA->getStringParam(0, idx1, stdString)==asynSuccess);
        testOk1(stdString == "Hello world");
    }

    {
        testOk1(portA->findParam(0, "y", &idx1)==asynSuccess);

        asynInt32Client client(portA->portName, -1, "y");
        testOk1(client.registerInterruptUser(&int32cb)==asynSuccess);

        lastint32 = 1234;
        portA->callParamCallbacks();
        testOk1(cbcount==0);
        testOk1(lastint32==1234);

        epicsInt32 val;
        testOk1(client.read(&val)==asynParamUndefined);

        {
            Guard G(*portA);
            // calls setFlag twice, but should only queue once
            portA->setIntegerParam(0, idx1, 55);
            portA->setIntegerParam(0, idx1, 55);
            testOk1(portA->callParamCallbacks()==asynSuccess);
        }

        testOk1(lastint32==55);
        testOk1(cbcount==1);

        client.write(43);
        testOk1(client.read(&val)==asynSuccess);
        testOk1(val==43);

        testOk1(lastint32==43);
        testOk1(cbcount==2);
    }
}

} // namespace

static void checkShutdown(const char *portName) {
    asynUser *pasynUser = pasynManager->createAsynUser(0, 0);
    pasynManager->connectDevice(pasynUser, portName, 0);

    asynStatus status = pasynManager->queueRequest(pasynUser, asynQueuePriorityMedium, 0);
    testOk1(status != asynSuccess);

    int enabled = 1;
    status = pasynManager->isEnabled(pasynUser, &enabled);
    testOk1(status == asynSuccess && enabled == 0);

    status = pasynManager->enable(pasynUser, 1);
    testOk1(status != asynSuccess);

    enabled = 1;
    status = pasynManager->isEnabled(pasynUser, &enabled);
    testOk1(status == asynSuccess && enabled == 0);

    pasynManager->freeAsynUser(pasynUser);
}

MAIN(asynPortDriverTest)
{
    const int testsPerRun = 54;
    const int testRuns = 4;
    const int additionalTests = 8;
    testPlan(testsPerRun * testRuns + additionalTests);
    interruptAccept=1;
    try {
        {
            testDiag("Testing a non-destructible port");
            testA(instantiateDriver("portA", false));
        }
        {
            testDiag("Testing a destructible port, late shutdown");
            testA(instantiateDriver("portB", true));
        }
        {
            testDiag("Testing a destructible port, \"proper\" early shutdown");
            asynPortDriver *tempPort = instantiateDriver("portC", true);
            testA(tempPort);
            std::string portName(tempPort->portName);
            asynUser *pasynUser = pasynManager->createAsynUser(0, 0);
            pasynManager->connectDevice(pasynUser, tempPort->portName, 0);
            pasynManager->lockPort(pasynUser);
            pasynManager->shutdownPort(pasynUser);
            pasynManager->unlockPort(pasynUser);
            pasynManager->freeAsynUser(pasynUser);

            checkShutdown(portName.c_str());
        }
        {
            testDiag("Testing a destructible port, early deletion");
            asynPortDriver *tempPort = instantiateDriver("portD", true);
            testA(tempPort);
            std::string portName(tempPort->portName);
            delete tempPort;
            checkShutdown(portName.c_str());
        }
    } catch(std::exception& e) {
        testAbort("Unhandled C++ exception: %s", e.what());
    }
    return testDone();
}
