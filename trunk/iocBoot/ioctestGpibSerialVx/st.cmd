# Following must be added for many board support packages
#cd <full path to target bin directory>
#cd "src/EPICS/modules/soft/asyn/iocBoot/ioctestGpibSerialVx"

< cdCommands

cd topbin
ld < testGpibSerialVx.munch
cd startup

dbLoadDatabase("../../dbd/testGpibSerialVx.dbd")
testGpibSerialVx_registerRecordDeviceDriver(pdbbase)

dbLoadRecords("../../db/testGpib.db","name=gpibTest,L=0,A=3")
dbLoadRecords("../../db/asynGeneric.db","ioc=gpibTest")
dbLoadRecords("../../db/asynOctetRecord.db","P=mrk,R=Octet,PORT=L0,ADDR=3,OMAX=0,IMAX=0")

#The following command is for a serial line terminal concentrator
drvGenericSerialConfigure("L0","164.54.9.90:4004",0,0)

#The following commands are for a Greenspring octal UART and VIP616-01 carrier
#ipacAddVIPC616_01("0x6000,B0000000")
#tyGSOctalDrv(1)
#tyGSOctalModuleInit("RS232", 0x80, 0, 0)
#tyGSOctalDevCreate("/tyGS/0/0",0,0,1000,1000)
#drvGenericSerialConfigure("L0","/tyGS/0/0",0,0)
#asynSetPortOption("L0", "baud", "9600")
#asynSetPortOption("L0", "bits", "8")
#asynSetPortOption("L0", "parity", "none")
#asynSetPortOption("L0", "stop", "1")
#asynSetPortOption("L0", "clocal", "Y")
#asynSetPortOption("L0", "crtscts", "N")

iocInit()
