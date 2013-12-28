# Generate HTML pages with a menu

SRCS += index.md
SRCS += news.md
SRCS += getting_started.md
SRCS += screenshots.md
SRCS += downloads.md
SRCS += issue_tracking.md
SRCS += documentation.md
SRCS += developers.md

BUILD_DIR=.

HTMLS = $(SRCS:%.md=%.html)

DEPENDS = header.html footer.html gen_menu.sh

all: $(HTMLS)

%.html: %.md $(DEPENDS) $(SRCS)
	sh gen_menu.sh --header header.html --footer footer.html --page $< -- $(SRCS) > $@
	
clean:
	rm $(HTMLS)
