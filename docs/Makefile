SPHINX_MULTIBUILD=sphinx-multibuild

DOCS_PATHS += -i .

BUILD = _build
SOURCE = source
DOCS = _docs
#OPTS = -v -v -v
OPTS =

.PHONY: all
all: sphinx


.PHONY: all
sphinx:
	$(SPHINX_MULTIBUILD) -i $(SOURCE) -s $(DOCS) -o $(BUILD)/html $(OPTS) -b html

.PHONY: clean
clean:
	rm -rf _docs
	rm -rf _build
