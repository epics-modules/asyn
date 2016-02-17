HTMLFLAGS = --book --duplex --no-numbered --format pdf14 --title --size letter

%.pdf: %.html
	-htmldoc $(HTMLFLAGS) --outfile $@ $<

all: asynDriver.pdf asynRecord.pdf devGpib.pdf doxygen install

doxygen:
	doxygen

install:
	mkdir -p ../html
	cp *.html ../html
	cp *.jpg ../html
	cp *.png ../html
	cp *.pdf ../html
	cp -r asynDoxygenHTML ../html
	mkdir -p ../html/gpibCoreConversion ; cp gpibCoreConversion/*html gpibCoreConversion/*css ../html/gpibCoreConversion
	mkdir -p ../html/HowToDoSerial ; cp HowToDoSerial/*.html ../html/HowToDoSerial

clean:

realclean: clean
	rm -f *.pdf
