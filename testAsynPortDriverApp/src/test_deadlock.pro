pro test
  PV = 'testAPD:scope1:NoiseAmplitude'
  while 1 do begin
     value = 0.1 + randomu(seed)*.01
     t = caput(PV, value)
     wait, .01
  endwhile
end

