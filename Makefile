CFLAGS += -Wall
CFLAGS += -ggdb
CFLAGS += -Itoolbag/dict

INDENT = indent -nut

OBJS=irtoy_tool.o toolbag/dict/dict.o irtoy.o error.o keywords.o mac_actions.o


# Linking
irtoy_tool:	$(OBJS)
		$(CC) -o $@ $(OBJS)

# Old
irtoy_tool.defs: irtoy_tool.c mk_defs.pl
		perl ./mk_defs.pl irtoy_tool.c > irtoy_tool.defs

dict.o:		toolbag/dict/dict.c	toolbag/dict/dict.h
error.o:	error.h
keywords.o:	keywords.h toolbag/dict/dict.h keywords.inc
irtoy_tool.o:	error.h irtoy.h toolbag/dict/dict.h keywords.h keywords.inc mac_actions.h
mac_actions.o:	mac_actions.h

indent:
	$(INDENT) - < mythtv_irtoy.c > indent.tmp
	diff irtoy_tool.c indent.tmp || cat indent.tmp > irtoy_tool.c
	rm indent.tmp

check-indent:
	$(INDENT) - <  irtoy_tool.c | diff -C1 irtoy_tool.c -


keywords.inc.new:	irtoy_tool.c mac_actions.c update_keywords.pl
			perl update_keywords.pl $< > keywords.inc.new 
keywords.inc:	keywords.inc.new
		@if diff keywords.inc.new keywords.inc>/dev/null; then true ; else \
		  echo "Updating keywords.inc" ; \
		  cp keywords.inc.new keywords.inc ; \
		fi

clean:
	rm -f irtoy_tool $(OBJS)

