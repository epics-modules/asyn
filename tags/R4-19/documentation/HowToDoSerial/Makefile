all: pdf

pdf: tutorial.pdf

tutorial.tex: tutorial.lyx
	lyx --export pdflatex tutorial.lyx

tutorial.pdf: tutorial.tex
	pdflatex tutorial.tex
	pdflatex tutorial.tex
	rm -f tutorial.log tutorial.out tutorial.toc tutorial.ps

install: pdf
	scp tutorial.pdf $(WEB_BASE)

clean:
	rm -f *.dvi *.log *.aux *.bbl *.blg *.lof *.lot *.toc *.out *.haux *.htoc
	rm -f tutorial.ps tutorial.tex tutorial.lyx~

realclean: clean
	rm -rf tutorial.lyx~ tutorial.pdf
