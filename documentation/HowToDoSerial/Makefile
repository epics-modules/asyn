all: pdf html

pdf: tutorial.pdf

tutorial.pdf: tutorial.tex
	latex tutorial.tex
	latex tutorial.tex
	dvips -t letter -P pdf -G0 -o tutorial.ps tutorial.dvi
	ps2pdf tutorial.ps
	rm -f tutorial.log tutorial.out tutorial.toc tutorial.dvi tutorial.ps

html: tutorial/tutorial.html

tutorial/tutorial.html: tutorial.pdf Makefile init.pl
	latex2html -html_version "4.0,unicode" -init_file init.pl \
	    -noaddress -info 0 -local_icons -transparent -split 0 \
	    -t "How to create EPICS device support for a simple serial device" tutorial.tex
	cp tutorial.pdf tutorial

install: html pdf
	scp `find tutorial -type f -print -depth` \
			$(WEB_BASE)

clean:
	rm -f *.dvi *.log *.aux *.bbl *.blg *.lof *.lot *.toc *.out

realclean: clean
	rm -f tutorial.pdf
	rm -rf tutorial
