# Following must be added for many board support packages
#cd <full path to target bin directory>
cd "src/EPICS/modules/soft/asyn/iocBoot/ioctestGpibSerialVx"

< cdCommands

cd topbin
ld < testGpibSerialVx.munch
cd startup

dbLoadDatabase("../../dbd/testGpibSerialVx.dbd")
testGpibSerialVx_registerRecordDeviceDriver(pdbbase)

dbLoadRecords("../../db/testGpib.db","name=gpibTest,L=0,A=3")
dbLoadRecords("../../db/asynTrace.db","ioc=gpibTest,port=L0,addr=3")

#The following command is for a serial line terminal concentrator
#drvGenericSerialConfigure("L0","164.54.9.93:4003",0,0)

#The following commands are for a Greenspring octal UART and VIP616-01 carrier
ipacAddVIPC616_01("0x6000,B0000000")
tyGSOctalDrv(1)
tyGSOctalModuleInit("RS232", 0x80, 0, 0)
tyGSOctalDevCreate("/tyGS/0/0",0,0,1000,1000)
drvGenericSerialConfigure("L0","/tyGS/0/0",0,0,"9600","cs8","-parenb","-crtscts","clocal")

asynSetTraceMask(echo,1,0xff)
asynSetTraceIOMask(echo,1,0x2)

iocInit()
