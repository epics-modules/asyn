; This program tests deadlocks in asyn:REABACK callbacks 

PREFIX = 'testOutputCallback:'

counter = 0L
while 1 do begin
    t = caput(PREFIX + 'LongoutInt32', 0)
    t = caput(PREFIX + 'BoInt32', 0)
    t = caput(PREFIX + 'LongoutUInt32D', 0)
    t = caput(PREFIX + 'AoFloat64', 0)
    t = caput(PREFIX + 'Stringout', "test")
    t = caput(PREFIX + 'TriggerCallbacks', 1)
    
    print, counter
    counter += 1
endwhile
end
