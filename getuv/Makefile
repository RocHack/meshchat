ifndef DEBUGGING_CHECK_VERSION 
MAKEFLAGS += -s
endif

all:
	@if $(MAKE) check_version $(if $(DEBUGGING_CHECK_VERSION), ,2>/dev/null) ; then \
		./check_version ; \
	else \
		$(MAKE) just_internal ; \
		./just_internal ; \
	fi

check_version: check_version.o sync.o
	gcc -o $@ $^ -luv

just_internal: just_internal.o sync.o
	gcc -o $@ $^

clean:
	rm -f check_version just_internal *.o
	[ -d ../deps/libuv ] && cd ../deps/libuv && make clean

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $^
