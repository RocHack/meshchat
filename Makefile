MAKEFLAGS += --no-print-directory

all: 
	@cd getuv && $(MAKE)

clean: 
	@$(MAKE) -f main.mk clean
	@cd getuv && $(MAKE) clean

.PHONY: all clean
