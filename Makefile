OUTDIR = build
SRCDIR = paper_framework_sources
include ../mpark-wg21/Makefile


#SRC = $(notdir $(wildcard $(SRCDIR)/*.md))

#HTML = $(SRC:.md=.html)
#LATEX = $(SRC:.md=.latex)
#PDF = $(SRC:.md=.pdf)

.PHONY: html
html: $(HTML)

simple_markdown_html:
	#find simple_markdown_sources -name "*.md" -type f | xargs basename | sed 's/\.md//' | xargs -I{} -t -n 1 sh -c "grip simple_markdown_sources/{}.md --export $(OUTDIR)/{}.html"

bikeshed_html:
	find bikeshed_sources -name "*.bs" -type f | xargs basename | sed 's/\.bs//' | xargs -I{} -t -n 1 sh -c "curl https://api.csswg.org/bikeshed/ -F file=@bikeshed_sources/{}.bs -F force=1 > $(OUTDIR)/{}.html"
