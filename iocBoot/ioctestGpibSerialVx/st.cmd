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

#The following command is for a serial line terminal concentrator
#drvAsynIPPortConfigure("L0","164.54.9.90:4004",0,0,0)

#The following commands are for a Greenspring octal UART and VIP616-01 carrier
#ipacAddVIPC616_01("0x6000,B0000000")
#tyGSOctalDrv(1)
#tyGSOctalModuleInit("RS232", 0x80, 0, 0)
#The following commands are for a Greenspring octal UART and MVME162
ipacAddMVME162("A:l=3,3 m=0xe0000000,64;B:l=3,3 m=e0010000,64")
tyGSOctalDrv(1)
tyGSOctalModuleInit("RS232", 0x80, 0, 1)

tyGSOctalDevCreate("/tyGS/0/0",0,0,1000,1000)
drvAsynSerialPortConfigure("L0","/tyGS/0/0",0,0,0)
asynSetOption("L0", 0, "baud", "9600")
asynSetOption("L0", 0, "bits", "8")
asynSetOption("L0", 0, "parity", "none")
asynSetOption("L0", 0, "stop", "1")
asynSetOption("L0", 0, "clocal", "Y")
asynSetOption("L0", 0, "crtscts", "N")

iocInit()
