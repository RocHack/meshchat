MAKEFLAGS += --no-print-directory

all: 
	cd getuv && . ./make.sh

clean: 
	@$(MAKE) --no-print-directory -f main.mk clean
	cd getuv && make clean

.PHONY: all clean
