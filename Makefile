# Generate HTML pages with a menu

SRCS += about.md
SRCS += index.md
SRCS += downloads.md

BUILD_DIR=.

HTMLS = $(SRCS:%.md=%.html)

DEPENDS = header.html footer.html

all: $(HTMLS)

%.html: %.md $(DEPENDS) $(SRCS)
	@TITLE=`pandoc < $< | grep "^<h1" | sed -e "s;</h1>;;" -e "s;.*>;;"`; \
		sed -e "s;__TITLE__;$$TITLE;g" header.html > $@; 
	sh gen_menu.sh $(SRCS) >> $@
	echo "<div class='contents'>" >> $@
	pandoc < $< >> $@
	echo "</div>" >> $@
	@cat footer.html >> $@
	
clean:
	rm $(HTMLS)
