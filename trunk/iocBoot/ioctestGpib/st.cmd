dbLoadDatabase("../../dbd/testGpib.dbd")
registerRecordDeviceDriver(pdbbase)

dbLoadRecords("../../db/testGpib.db","name=gpibTest,L=0,A=3")
echoDriverInit(gpibL0,1.0)
iocInit()
