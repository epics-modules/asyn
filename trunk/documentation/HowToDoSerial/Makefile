all: pdf html

pdf: tutorial.pdf

tutorial.tex: tutorial.lyx
	lyx --export latex tutorial.lyx

tutorial.pdf: tutorial.tex
	latex tutorial.tex
	latex tutorial.tex
	dvips -t letter -P pdf -G0 -o tutorial.ps tutorial.dvi
	ps2pdf tutorial.ps
	rm -f tutorial.log tutorial.out tutorial.toc tutorial.ps

html: tutorial.html

tutorial.html: tutorial.pdf Makefile
	hevea -v tutorial.tex
	hevea -v tutorial.tex

install: html pdf
	scp `find tutorial -type f -print -depth` \
			$(WEB_BASE)

clean:
	rm -f *.dvi *.log *.aux *.bbl *.blg *.lof *.lot *.toc *.out *.haux *.htoc
	rm -f tutorial.ps tutorial.tex tutorial.lyx~

realclean: clean
	rm -rf tutorial.html tutorial.lyx~ tutorial.pdf
