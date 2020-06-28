#!make

.phony: markdown
.SUFFIXES : .rst .md


.rst.md:
	/usr/local/bin/pandoc -f rst  -t markdown -o $@  $<

markdown: README.md

# README.md: README.rst
# 	/usr/local/bin/pandoc -f rst  -t markdown -o README.md  README.rst
