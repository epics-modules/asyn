## Example vxWorks startup file

## The following is needed if your vxWorks OS image doesn't
## automatically cd to the directory containing its startup script
#cd "_TOP_/iocBoot/_IOCNAME_"

< cdCommands
# If you're using NFS, edit the nfsCommands file and uncomment this:
#< ../nfsCommands

cd topbin
ld < _NAME_.munch

## This drvTS initializer is needed if the IOC has a hardware event system
#TSinit

## Register all support components
cd top
dbLoadDatabase("dbd/_NAME_.dbd",0,0)
_NAME__registerRecordDeviceDriver(pdbbase)

## Load record instances
dbLoadRecords("db/_NAME_.db","user=_USER_")

cd startup
iocInit()

## Start any sequence programs
#seq &sncExample,"user=_USER_"
