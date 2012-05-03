#include <iocsh.h>
#include <epicsExport.h>

/*
 * IOC shell command registration
 */
static const iocshArg vxi11ConfigureArg0 = { "portName",iocshArgString};
static const iocshArg vxi11ConfigureArg1 = { "host name",iocshArgString};
static const iocshArg vxi11ConfigureArg2 = { "flags (lock devices : recover with IFC)",iocshArgInt};
static const iocshArg vxi11ConfigureArg3 = { "default timeout",iocshArgString};
static const iocshArg vxi11ConfigureArg4 = { "vxiName",iocshArgString};
static const iocshArg vxi11ConfigureArg5 = { "priority",iocshArgInt};
static const iocshArg vxi11ConfigureArg6 = { "disable auto-connect",iocshArgInt};
static const iocshArg *vxi11ConfigureArgs[] = {&vxi11ConfigureArg0,
    &vxi11ConfigureArg1, &vxi11ConfigureArg2, &vxi11ConfigureArg3,
    &vxi11ConfigureArg4, &vxi11ConfigureArg5,&vxi11ConfigureArg6};
static const iocshFuncDef vxi11ConfigureFuncDef = {"vxi11Configure",7,vxi11ConfigureArgs};
static void vxi11ConfigureCallFunc(const iocshArgBuf *args)
{
    printf("ERROR: vxi11Configure is not supported on WIN32\n");
}

extern int E5810Reboot(char * inetAddr,char *password);
extern int E2050Reboot(char * inetAddr);
extern int TDS3000Reboot(char * inetAddr);

static const iocshArg E5810RebootArg0 = { "inetAddr",iocshArgString};
static const iocshArg E5810RebootArg1 = { "password",iocshArgString};
static const iocshArg *E5810RebootArgs[2] = {&E5810RebootArg0,&E5810RebootArg1};
static const iocshFuncDef E5810RebootFuncDef = {"E5810Reboot",2,E5810RebootArgs};
static void E5810RebootCallFunc(const iocshArgBuf *args)
{
    printf("ERROR: E5810Reboot is not supported on WIN32\n");
}

static const iocshArg E2050RebootArg0 = { "inetAddr",iocshArgString};
static const iocshArg *E2050RebootArgs[1] = {&E2050RebootArg0};
static const iocshFuncDef E2050RebootFuncDef = {"E2050Reboot",1,E2050RebootArgs};
static void E2050RebootCallFunc(const iocshArgBuf *args)
{
    printf("ERROR: E2050Reboot is not supported on WIN32\n");
}
static const iocshArg TDS3000RebootArg0 = { "inetAddr",iocshArgString};
static const iocshArg *TDS3000RebootArgs[1] = {&TDS3000RebootArg0};
static const iocshFuncDef TDS3000RebootFuncDef = {"TDS3000Reboot",1,TDS3000RebootArgs};
static void TDS3000RebootCallFunc(const iocshArgBuf *args)
{
    printf("ERROR: TDS3000Reboot is not supported on WIN32\n");
}

/*
 * This routine is called before multitasking has started, so there's
 * no race condition in the test/set of firstTime.
 */
static void vxi11RegisterCommands (void)
{
    static int firstTime = 1;
    if (firstTime) {
        firstTime = 0;
        iocshRegister(&vxi11ConfigureFuncDef,vxi11ConfigureCallFunc);
        iocshRegister(&E2050RebootFuncDef,E2050RebootCallFunc);
        iocshRegister(&E5810RebootFuncDef,E5810RebootCallFunc);
        iocshRegister(&TDS3000RebootFuncDef,TDS3000RebootCallFunc);
    }
}
epicsExportRegistrar(vxi11RegisterCommands);

