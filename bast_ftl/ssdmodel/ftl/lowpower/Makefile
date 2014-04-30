# DiskSim SSD support
# 2008 Microsoft Corporation. All Rights Reserved

include .paths

LDFLAGS = -lm -L. -ldisksim $(DISKMODEL_LDFLAGS) $(MEMSMODEL_LDFLAGS) \
                            $(LIBPARAM_LDFLAGS) $(LIBDDBG_LDFLAGS) 

HP_FAST_OFLAGS = +O4
NCR_FAST_OFLAGS = -O4 -Hoff=BEHAVED 
FREEBLOCKS_OFLAGS =
DEBUG_OFLAGS = -g -DASSERTS # -DDEBUG=1
PROF_OFLAGS = -g -DASSERTS -p
GPROF_OFLAGS = -g -DASSERTS -pg
CFLAGS = $(DEBUG_OFLAGS) -I../../ $(DISKMODEL_CFLAGS) $(DISKSIM_CFLAGS) $(LIBPARAM_CFLAGS) $(LIBDDBG_CFLAGS)

#CC = cc
CC = gcc -Wall -Wno-unused -MD
# because purify spits out warnings on stdout...
CC-DEP = gcc $(LIBPARAM_CFLAGS) $(DISKMODEL_CFLAGS)

# The following lines create a dependency target based on the state of 
# the modules files. If the .c and .h files are not created, the dependency
# target is modules which invokes "make -C modules". If the files are already
# created, the target are the files themselves. 
# This expression is to avoid remaaking of the libdisksim.a with ar and ranlib
# even if no files have changed. 

all: libftl.a

clean:
	rm -f TAGS *.o libftl.a

realclean: clean
	rm -f *.d .depend

distclean: realclean
	rm -f *~ *.a
	rm -rf lib 

.PHONY: modules

-include *.d

DISKSIM_FTL_SRC = ssd_timing.c ssd_clean.c 

DISKSIM_FTL_OBJ = $(DISKSIM_FTL_SRC:.c=.o) 

$(DISKSIM_FTL_OBJ): %.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

libftl.a: $(DISKSIM_FTL_OBJ) 
	ar cru $@ $(DISKSIM_FTL_OBJ) 
	ranlib $@
	mkdir -p lib
	cp libftl.a lib/libftl.a

########################################################################
# rule to automatically generate dependencies from source files
#%.d: %.c
#	set -e; $(CC-DEP) -M $(CPPFLAGS) $<  \
#		| sed 's/\($*\)\.o[ :]*/\1.o $@ : /g' > $@; \
#		[ -s $@ ] 2>/dev/null >/dev/null || rm -f $@


# this is a little less aggressive and annoying than the above
depend: .depend 

.depend: *.c *.h
	rm -f .depend
	$(foreach file, $(DISKSIM_FTL_SRC), \
		$(CC-DEP) $(CFLAGS) -M $(file) >> .depend; )

