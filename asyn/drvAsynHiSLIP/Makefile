#!make

.phony: markdown
.SUFFIXES : .rst .md .txt

.rst.md:
	/usr/local/bin/pandoc -f rst  -t markdown -o $@  $<

.rst.txt:
	/usr/local/bin/pandoc -f rst  -t plain -o $@  $<

markdown: README.md README.txt
