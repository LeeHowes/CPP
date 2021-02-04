DATADIR = $(dir $(lastword $(MAKEFILE_LIST)))paper_framework
METADATA = $(wildcard metadata.yaml)
OUTDIR = build
SRCDIR = paper_framework_sources

$(OUTDIR)/%.html $(OUTDIR)/%.latex $(OUTDIR)/%.pdf: $(SRCDIR)/%.md
	/usr/local/bin/pandoc $< $(METADATA) $(DATADIR)/references.md \
       --number-sections \
       --self-contained \
       --table-of-contents \
       --bibliography $(DATADIR)/index.yaml \
       --csl $(DATADIR)/cpp.csl \
       --css $(DATADIR)/template/14882.css \
       --citeproc \
       --filter $(DATADIR)/filter/wg21.py \
       --highlight-style $(DATADIR)/syntax/wg21.theme \
       --metadata datadir:$(DATADIR) \
       --metadata-file $(DATADIR)/metadata.yaml \
       --syntax-definition $(DATADIR)/syntax/isocpp.xml \
       --template $(DATADIR)/template/wg21 \
       --output $@

SRC = $(notdir $(wildcard $(SRCDIR)/*.md))

HTML = $(SRC:.md=.html)
LATEX = $(SRC:.md=.latex)
PDF = $(SRC:.md=.pdf)

.PHONY: all
all: setup simple_markdown_html bikeshed_html $(HTML)

setup:
	mkdir -p $(OUTDIR)

simple_markdown_html:
	#find simple_markdown_sources -name "*.md" -type f | xargs basename | sed 's/\.md//' | xargs -I{} -t -n 1 sh -c "grip simple_markdown_sources/{}.md --export $(OUTDIR)/{}.html"

bikeshed_html:
	find bikeshed_sources -name "*.bs" -type f | xargs basename | sed 's/\.bs//' | xargs -I{} -t -n 1 sh -c "curl https://api.csswg.org/bikeshed/ -F file=@bikeshed_sources/{}.bs -F force=1 > $(OUTDIR)/{}.html"

.PHONY: clean
clean:
	rm -rf $(OUTDIR)/*

.PHONY: update
update:
	wget https://wg21.link/index.yaml -O $(DATADIR)/index.yaml

$(DATADIR)/index.yaml:
	wget https://wg21.link/index.yaml -O $@

.PHONY: $(HTML)
$(HTML): %.html: $(DATADIR)/index.yaml $(OUTDIR)/%.html

.PHONY: $(LATEX)
$(LATEX): %.latex: $(DATADIR)/index.yaml $(OUTDIR)/%.latex

.PHONY: $(PDF)
$(PDF): %.pdf: $(DATADIR)/index.yaml $(OUTDIR)/%.pdf
