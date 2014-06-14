all: uv_version_check
	@./$<

clean: 
	@make --no-print-directory -f main.mk clean
	rm -f uv_version_check

uv_version_check: uv_version_check.c
	${CC} -o $@ $<

.PHONY: all

