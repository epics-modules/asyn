dbLoadDatabase("../../dbd/test.dbd")
test_registerRecordDeviceDriver(pdbbase)

#drvAsynIPPortConfigure("A","164.54.9.90:4001",0,0,0)
echoDriverInit("A",0.05,0,0)
interposeInterfaceInit("interpose","A",0)
echoDriverInit("B",0.05,0,1)
addrChangeDriverInit("MA","A",0)

dbLoadRecords("../../db/asynRecord.db","P=asyn,R=Record_PA_A0,PORT=A,ADDR=0,OMAX=0,IMAX=0")
dbLoadRecords("../../db/test.db","P=test,R=Client,PORT=A,A=0")

dbLoadRecords("../../db/asynRecord.db","P=asyn,R=Record_PB_A-1,PORT=B,ADDR=-1,OMAX=0,IMAX=0")
dbLoadRecords("../../db/asynRecord.db","P=asyn,R=Record_PB_A0,PORT=B,ADDR=0,OMAX=0,IMAX=0")
dbLoadRecords("../../db/test.db","P=test,R=Client,PORT=B,A=0")
dbLoadRecords("../../db/asynRecord.db","P=asyn,R=Record_PB_A1,PORT=B,ADDR=1,OMAX=0,IMAX=0")
dbLoadRecords("../../db/test.db","P=test,R=Client,PORT=B,A=1")
dbLoadRecords("../../db/asynRecord.db","P=asyn,R=Record,PORT=A,ADDR=0,OMAX=0,IMAX=0")

dbLoadRecords("../../db/asynRecord.db","P=asyn,R=Record_PMA_A-1,PORT=MA,ADDR=-1,OMAX=0,IMAX=0")
dbLoadRecords("../../db/asynRecord.db","P=asyn,R=Record_PMA_A0,PORT=MA,ADDR=0,OMAX=0,IMAX=0")
dbLoadRecords("../../db/test.db","P=test,R=Client,PORT=MA,A=0")
dbLoadRecords("../../db/asynRecord.db","P=asyn,R=Record_PMA_A1,PORT=MA,ADDR=1,OMAX=0,IMAX=0")
dbLoadRecords("../../db/test.db","P=test,R=Client,PORT=MA,A=1")

dbLoadRecords("../../db/asynRecord.db","P=asyn,R=Record,PORT=A,ADDR=0,OMAX=0,IMAX=0")
dbLoadRecords("../../db/testBlock.db","P=test,R=Block,PORT=B,A=0,VAL= ")
dbLoadRecords("../../db/testBlock.db","P=test,R=Block,PORT=B,A=1,VAL= ")
dbLoadRecords("../../db/testBlock.db","P=test,R=BlockAll,PORT=B,A=0,VAL=blockAll")
dbLoadRecords("../../db/testBlock.db","P=test,R=BlockAll,PORT=B,A=1,VAL=blockAll")
#asynSetTraceMask("B",-1,0xff)
#asynSetTraceIOMask("B",0,0x2)
iocInit()
asynReport 4
