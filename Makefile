BIN = meshchat
SRC = $(wildcard src/*.c)
SRC += $(wildcard deps/*/*.c)
OBJ = $(SRC:.c=.o)
CFLAGS = -Ideps -Wall -pedantic
CFLAGS += -std=gnu99
LDFLAGS = -luv

all: $(BIN)

uv_version_check: uv_version_check.c
	${CC} -o $@ $<

uv_version_check_thing: uv_version_check
	@./$<

$(BIN): uv_version_check_thing

$(BIN): $(OBJ)
	${CC} -o $@ ${OBJ} ${LDFLAGS}

.c.o:
	${CC} -c ${CFLAGS} $< -o $@

install: all
	install -m 0755 ${BIN} ${DESTDIR}${BINDIR}

uninstall:
	rm -f ${DESTDIR}${BINDIR}/${BIN}

clean:
	rm -f $(BIN) $(OBJ)

.PHONY: all install uninstall uv_version_check_thing

