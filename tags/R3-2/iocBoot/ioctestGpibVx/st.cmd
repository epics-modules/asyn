# Following must be added for many board support packages
#cd <full path to target bin directory>
#cd "src/EPICS/modules/soft/asyn/iocBoot/ioctestGpibVx"

< cdCommands

cd topbin
ld < testGpibVx.munch
cd startup

dbLoadDatabase("../../dbd/testGpibVx.dbd")
testGpibVx_registerRecordDeviceDriver(pdbbase)

dbLoadRecords("../../db/testGpib.db","name=gpibTest,L=0,A=3")
dbLoadRecords("../../db/asynRecord.db","P=asyn,R=Test,PORT=L0,ADDR=3,OMAX=0,IMAX=0")

#vxi11Debug = 10

#The following command is for the E2050
#E2050Reboot("164.54.8.227")
#vxi11Configure("L0","164.54.8.227",0,0.0,"hpib",0,0)

#The following two commands are for the E5810
#E5810Reboot("164.54.8.129",0)
#vxi11Configure("L0","164.54.8.129",0,0.0,"gpib0",0,0)

#The following command is for an ethernet TSD3014B scope
#vxi11Configure("L0","164.54.8.137",0,0.0,"inst0",0,0)

#The following is for the Greensprings IP488
#ipacAddMVME162("A:l=3,3 m=0xe0000000,64;B:m=e0000000,64 l=3,2")
#gsIP488Configure("L0",0,0,0x61,0,0)

#The following command is for the National Instruments 1014
ni1014Config("L0","L1",0x5000,0x64,5,0,0)
#asynSetTraceMask("L0",-1,0x15)
#asynSetTraceMask("L0",3,0x15)
#asynSetTraceFile("L0",-1,"temp")
#asynSetTraceFile("L0",3,"temp")
#asynSetTraceIOMask("L0",3,0x2)
#asynSetTraceMask("L0",15,0x15)
#asynSetTraceIOMask("L0",15,0x2)
#asynSetTraceFile("L0",15,"stdout")
iocInit()
