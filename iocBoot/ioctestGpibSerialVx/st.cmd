# Following must be added for many board support packages
#cd <full path to target bin directory>
cd "src/EPICS/modules/soft/asyn/iocBoot/ioctestGpibSerialVx"

< cdCommands

cd topbin
ld < testGpibSerialVx.munch
cd startup

dbLoadDatabase("../../dbd/testGpibSerialVx.dbd")
testGpibSerialVx_registerRecordDeviceDriver(pdbbase)

dbLoadRecords("../../db/testGpibSerial.db","name=gpibTest,L=0,A=3")

drvGenericSerialDebug = 3
#The following command is for a serial line terminal concentrator
#drvGenericSerialConfigure("L0","164.54.9.93:4003",0,0)

#The following commands are for a Greenspring octal UART and VIP16-01 carrier
ipacAddVIPC616_01("0x6000,B0000000")
tyGSOctalDrv 1
octalUart0 = tyGSOctalModuleInit("GSIP_OCTAL232", 0x80, 0, 0)
port0 = tyGSOctalDevCreate("/tyGS/0/0",octalUart0,0,1000,1000)
tyGSOctalConfig(port0,9600,'N',1,8,'N')
drvGenericSerialConfigure("L0","/tyGS/0/0")

iocInit()
