dbLoadDatabase("../../dbd/testManager.dbd")
testManager_registerRecordDeviceDriver(pdbbase)

# following are autoConnect
testManagerDriverInit("cantBlockSingle",0,0,0)
testManagerDriverInit("cantBlockMulti",0,0,1)
testManagerDriverInit("canBlockSingle",1,0,0)
testManagerDriverInit("canBlockMulti",1,0,1)
# following are noAutoConnect
#testManagerDriverInit("cantBlockSingle",0,1,0)
#testManagerDriverInit("cantBlockMulti",0,1,1)
#testManagerDriverInit("canBlockSingle",1,1,0)
#testManagerDriverInit("canBlockMulti",1,1,1)

#asynSetTraceMask("cantBlockSingle",0,0xff)
#asynSetTraceMask("canBlockSingle",-1,0xff)
#asynSetTraceMask("canBlockSingle",1,0xff)

dbLoadRecords("../../db/asynRecord.db","P=asyn,R=Record,PORT=cantBlockSingle,ADDR=0,OMAX=0,IMAX=0")
iocInit()
