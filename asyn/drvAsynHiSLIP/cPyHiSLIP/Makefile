#!make

.phony: markdown
.SUFFIXES : .rst .md .txt

.rst.md:
	/usr/local/bin/pandoc -f rst  -t markdown -o $@  $<

.rst.txt:
	/usr/local/bin/pandoc -f rst  -t plain -o $@  $<

markdown: README.md README.txt

upload: 
	python3 -m twine upload  dist/PyVXI11-$(shell hg parents --template '{latesttag}.{latesttagdistance}').tar.gz
