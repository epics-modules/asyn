HTMLFLAGS = --book --duplex --no-numbered --format pdf14 --title --size letter

PDFS = asynDriver.pdf asynRecord.pdf devGpib.pdf

%.pdf: %.html
	-htmldoc $(HTMLFLAGS) --outfile $@ $<

all: install

doxygen:
	doxygen

install: doxygen $(PDFS)
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
