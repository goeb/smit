# Generate HTML pages with a menu

SRCS = 
SRCS += overview.md
SRCS += install.md
SRCS += admin.md
SRCS += user_guide.md
SRCS += api.md
SRCS += developers.md

BUILD_DIR=.

HTMLS = $(SRCS:%.md=%.html)

DEPENDS = header.html footer.html ../gen_menu.sh

all: $(HTMLS)

%.html: %.md $(DEPENDS) $(SRCS)
	sh ../gen_menu.sh --header header.html --footer footer.html --page $< -- $(SRCS) > $@
	
clean:
	rm $(HTMLS)
