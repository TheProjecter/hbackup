AR := ar
RANLIB := ranlib
CFLAGS := -Wall -O2 -ansi -pedantic
LDFLAGS := -lssl

all: hbackup

install: hbackup
	strip $^

test:	setup \
	params_test.dif \
	list_test.dif \
	tools_test.dif \
	metadata_test.dif \
	filters_test.dif \
	parsers_test.dif \
	cvs_parser_test.dif \
	filelist_test.dif \
	db_test.dif \
	clients_test.dif

clean:
	rm -f *.[oa] *~ *.out *.out.failed *.dif *_test hbackup
	@./test_setup clean
	@echo "Cleaning test environment"

setup:
	@./test_setup
	@echo "Setting up test environment"

# Dependencies
metadata_test: metadata.a
params_test: params.a
list_test: list.a
tools_test: tools.a
filters_test: filters.a list.a
parsers_test: parsers.a list.a
cvs_parser_test: cvs_parser.a parsers.a list.a
filelist_test: filelist.a cvs_parser.a parsers.a filters.a list.a metadata.a \
	params.a
db_test: db.a filelist.a cvs_parser.a parsers.a filters.a list.a metadata.a \
	params.a tools.a
clients_test: clients.a db.a filelist.a cvs_parser.a parsers.a filters.a list.a \
	metadata.a params.a tools.a
hbackup: clients.a db.a filelist.a cvs_parser.a parsers.a filters.a list.a \
	metadata.a params.a tools.a

metadata.a: metadata.h
list.a: list.h
db.a: db.h list.h metadata.h
filters.a: filters.h list.h
parsers.a: parsers.h list.h
cvs_parser.a: cvs_parser.h parsers.h list.h metadata.h
filelist.a: filelist.h parsers.h list.h metadata.h
clients.a: clients.h list.h

# Rules
%.a: %.o
	$(AR) cru $@ $^
	$(RANLIB) $@

%.out: %
	./$< > $@ 2>&1

%.dif: %.out %.exp
	@echo "diff $^ > $@"
	@if ! diff $^ > $@; then \
	  mv -f $< $<.failed; \
	  echo "Failed output file is $<.failed"; \
	  rm -f $@; \
	  false; \
	else \
	  rm -f $<.failed; \
	fi
