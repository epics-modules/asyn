#!make
#HGVERSION:= $(shell hg parents --template 'HGTagShort = \\\"{latesttag}.{latesttagdistance}\\\"')
VERSIONGIT:= $(shell git describe --tags)
VERSION:= $(shell python3 setup.py --version)

.phony: markdown
.SUFFIXES : .rst .md .txt

.rst.md:
	/usr/local/bin/pandoc -f rst  -t markdown -o $@  $<

.rst.txt:
	/usr/local/bin/pandoc -f rst  -t plain -o $@  $<

markdown: README.md README.txt

upload:
	python3 -m twine upload  dist/cPyHiSLIP-$(VERSION).tar.gz 
