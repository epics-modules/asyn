dbLoadDatabase("../../dbd/testGpib.dbd")
testGpib_registerRecordDeviceDriver(pdbbase)

dbLoadRecords("../../db/testGpib.db","P=gpib,R=Test,PORT=L0,A=3")
dbLoadRecords("../../db/asynRecord.db","P=asyn,R=Record,PORT=L0,ADDR=3,OMAX=0,IMAX=200")

#For TDS5054B addr must be 0 ir SRQs will not work
#dbLoadRecords("../../db/testGpib.db","P=gpib,R=Test,PORT=L0,A=0")
#dbLoadRecords("../../db/asynRecord.db","P=asyn,R=Record,PORT=L0,ADDR=0,OMAX=0,IMAX=200")

#The following two commands are for the E2050
#E2050Reboot("164.54.8.227")
#vxi11Configure("L0","164.54.8.227",0,"0.0","hpib",0,0)

#The following two commands are for the E5810
#E5810Reboot("164.54.8.129",0)
#vxi11Configure("L0","164.54.8.129",0,"0.0","gpib0",0,0)

#The following is for a TDS5054B
vxi11Configure("L0","164.54.9.21",0,"0.0","inst0",0,0)

#The following two commands are for the E5810 serial port
#E5810Reboot("164.54.8.129",0)
#vxi11Configure("L1","164.54.8.129",0,"0.0","COM1,1",0,0)

#The following command is for an ethernet TSD3014B scope
#vxi11Configure("L0","164.54.8.137",0,"0.0","inst0",0,0)
#vxi11Configure("L0","164.54.9.32",0,"0.0","inst0",0,0)

#The following is for the Greensprings IP488
#     Choose one carrier here and in drvIpac.dbd.
#ipacAddXy9660("0x0,1 B=1,D00000")
#ipacAddVIPC616_01("0x6000,0xD00000,128")
#gsIP488Configure("L0",0,0,0x61,0,0)

#asynSetTraceMask("L0",1,0xff)
#asynSetTraceIOMask("L0",1,0x2)
#asynSetTraceMask("L0",-1,0xff)

iocInit()
