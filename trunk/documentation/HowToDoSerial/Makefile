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

html: tutorial.html

tutorial.html: tutorial.pdf Makefile
	sed -e '/\\textbackslash/s//$$\\backslash$$/g' tutorial.tex >htutorial.tex
	hevea -v -o tutorial.html htutorial.tex
	rm htutorial.tex

install: html pdf
	scp `find tutorial -type f -print -depth` \
			$(WEB_BASE)

clean:
	rm -f *.dvi *.log *.aux *.bbl *.blg *.lof *.lot *.toc *.out *.haux *.htoc
	rm -f tutorial.ps tutorial.pdf

realclean: clean
	rm -rf tutorial tutorial.tex tutorial.html tutorial.lyx~
