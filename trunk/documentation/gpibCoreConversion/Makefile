HTMLFLAGS = --book --duplex --format pdf14 --title --size letter

all: conversionNotes.pdf

%.pdf: %.html
	-htmldoc $(HTMLFLAGS) --outfile $@ $<

clean:

realclean: clean
	rm -f *.pdf
