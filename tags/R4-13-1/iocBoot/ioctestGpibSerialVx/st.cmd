# Following must be added for many board support packages
#cd <full path to target bin directory>
#cd "src/EPICS/modules/soft/asyn/iocBoot/ioctestGpibSerialVx"

< cdCommands

cd topbin
ld < testGpibSerialVx.munch
cd startup

dbLoadDatabase("../../dbd/testGpibSerialVx.dbd")
testGpibSerialVx_registerRecordDeviceDriver(pdbbase)

dbLoadRecords("../../db/testGpib.db","P=gpib,R=Test,PORT=L0,A=0")
dbLoadRecords("../../db/asynRecord.db","P=asyn,R=Record,PORT=L0,ADDR=0,OMAX=0,IMAX=0")


#The following command is for a Greenspring VIP616-01 carrier
#ipacAddVIPC616_01("0x6000,B0000000")
#The following commands are for a mvme162 which has two Industry Pack slots
#ipacAddMVME162("A:l=3,3 m=0xe0000000,64;B:l=3,3 m=e0010000,64")
#The following is for a Greenspring TVME200 carrier
ipacAddTVME200("602fb0")

#The following initialize a Greenspring octalUart in the second IP slot
tyGSOctalDrv(1)
tyGSOctalModuleInit("MOD0","232", 0x80, 0, 1)

#tyGSOctalDevCreate("/tyGS/0,0/0","MOD0",0,1000,1000)
tyGSOctalDevCreateAll("/tyGS/0,0/","MOD0",1000,1000)

drvAsynSerialPortConfigure("L0","/tyGS/0,0/0",0,0,0)
drvAsynSerialPortConfigure("L1","/tyGS/0,0/1",0,0,0)
drvAsynSerialPortConfigure("L2","/tyGS/0,0/2",0,0,0)
drvAsynSerialPortConfigure("L3","/tyGS/0,0/3",0,0,0)
drvAsynSerialPortConfigure("L4","/tyGS/0,0/4",0,0,0)
drvAsynSerialPortConfigure("L5","/tyGS/0,0/5",0,0,0)
drvAsynSerialPortConfigure("L6","/tyGS/0,0/6",0,0,0)
drvAsynSerialPortConfigure("L7","/tyGS/0,0/7",0,0,0)

#asynSetTraceMask("L0",1,0xff)

#asynSetOption("L0", 0, "baud", "9600")
#asynSetOption("L0", 0, "bits", "8")
#asynSetOption("L0", 0, "parity", "none")
#asynSetOption("L0", 0, "stop", "1")
#asynSetOption("L0", 0, "clocal", "Y")
#Note that crtscts should be N
asynSetOption("L0", 0, "crtscts", "N")

#following is a way to configure octalUart ports
#tyGSOctalConfig("/tyGS/0,0/0",9600,'N',1,8,'N')

#The following command is for a serial line terminal concentrator
drvAsynIPPortConfigure("netSerial","164.54.9.90:4004",0,0,0)

iocInit()
