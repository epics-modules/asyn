HTMLFLAGS = --book --duplex --no-numbered --format pdf14 --title --size letter

all: asynDriver.pdf asynRecord.pdf devGpib.pdf

%.pdf: %.html
	-htmldoc $(HTMLFLAGS) --outfile $@ $<

clean:

realclean: clean
	rm -f *.pdf
