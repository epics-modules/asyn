dbLoadDatabase("../../dbd/testGpib.dbd")
testGpib_registerRecordDeviceDriver(pdbbase)

dbLoadRecords("../../db/testGpib.db","name=gpibTest,L=0,A=3")

#var vxi11Debug 3

#The following two commands are for the E2050
#E2050Reboot("164.54.8.227")
vxi11Configure("L0","164.54.8.227",0,0.0,"hpib",0)

#The following two commands are for the E5810
#E5810Reboot("164.54.8.129",0)
#vxi11Configure("L0","164.54.8.129",0,0.0,"gpib0",0)

#The following command is for an ethernet TSD3014B scope
#vxi11Configure("L0","164.54.8.137",0,0.0,"inst0",0)

iocInit()
