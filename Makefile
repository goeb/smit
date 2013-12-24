# Generate HTML pages with a menu

SRCS += index.md
SRCS += getting_started.md
SRCS += documentation.md
SRCS += downloads.md

BUILD_DIR=.

HTMLS = $(SRCS:%.md=%.html)

DEPENDS = header.html footer.html gen_menu.sh

all: $(HTMLS)

%.html: %.md $(DEPENDS) $(SRCS)
	sh gen_menu.sh --header header.html --footer footer.html --page $< -- $(SRCS) > $@
	
clean:
	rm $(HTMLS)
