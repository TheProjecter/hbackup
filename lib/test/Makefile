AR := ar
RANLIB := ranlib
CXXFLAGS := -Wall -O2 -ansi -I..
LDFLAGS := -lssl -lz

all: test

test:	list.done \
	files.done \
	filters.done \
	parsers.done \
	cvs_parser.done \
	paths.done \
	db.done \
	clients.done

clean:
	@rm -f *.[oa] *~ *.out *.err *.all *.done *_test zcop*
	@../../test_setup clean

# Rules
%_test.o: %_test.cpp
	@echo "CXX	$<"
	@$(CXX) $(CXXFLAGS) -c -o $@ $<

%_test: %_test.o ../libhbackup.a
	@echo "BUILD	$@"
	@$(CXX) $(LDFLAGS) -o $@ $^

%.done: %_test %.exp ../../test_setup
	@echo "RUN	$<"
	@../../test_setup
	@./$< > `basename $@ .done`.all 2>&1
	@diff -q `basename $@ .done`.exp `basename $@ .done`.all
	@touch $@
	@rm -f `basename $@ .done`.all