all: pdf html

pdf: tutorial.pdf

tutorial.tex: tutorial.lyx
	/Applications/LyX.app//Contents/MacOS/LyX --export latex tutorial.lyx

tutorial.pdf: tutorial.tex
	latex tutorial.tex
	latex tutorial.tex
	dvips -t letter -P pdf -G0 -o tutorial.ps tutorial.dvi
	ps2pdf tutorial.ps
	rm -f tutorial.log tutorial.out tutorial.toc tutorial.ps

html: tutorial/tutorial.html

tutorial/tutorial.html: tutorial.pdf Makefile init.pl
	latex2html -html_version "4.0,unicode" -init_file init.pl \
        -noaddress -info 0 -local_icons -transparent -split 0 \
        -t "Diamond Prometheus EPICS Brick" tutorial.tex
	cp tutorial.pdf tutorial

install: html pdf
	scp `find tutorial -type f -print -depth` \
			$(WEB_BASE)

clean:
	rm -f *.dvi *.log *.aux *.bbl *.blg *.lof *.lot *.toc *.out
	rm -f tutorial.ps tutorial.pdf

realclean: clean
	rm -rf tutorial tutorial.tex tutorial.html tutorial.lyx~
