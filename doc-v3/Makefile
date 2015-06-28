# Generate HTML pages with a menu

SRCS += index.md
SRCS += install.md
SRCS += admin.md
SRCS += experts.md
SRCS += web_user_guide.md
SRCS += html.md
SRCS += api.md
SRCS += developers.md

BUILD_DIR=.

HTMLS = $(SRCS:%.md=%.html)

DEPENDS = header.html footer.html ../gen_menu.sh

all: $(HTMLS)

%.html: %.md $(DEPENDS)
	sh ../gen_menu.sh --header header.html --footer footer.html --page $< -- $(SRCS) > $@
	
clean:
	rm $(HTMLS)
