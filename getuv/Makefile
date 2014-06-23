MAKEFLAGS += --no-print-directory

all:
	@echo either check_version or check_exists

check_version: check_version.o sync.o
	gcc -o $@ $^ -luv

just_internal: just_internal.o sync.o
	gcc -o $@ $^

clean:
	rm -f check_version just_internal *.o
	if [ -d ../deps/libuv ]; then cd ../deps/libuv && make clean; fi

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $^
