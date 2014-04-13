BIN = meshchat
SRC = $(wildcard src/*.c)
SRC += $(wildcard deps/*/*.c)
OBJ = $(SRC:.c=.o)
CFLAGS = -Ideps -Wall -g
CFLAGS += -std=c99 -D_POSIX_SOURCE
LDFLAGS =

all: $(BIN)

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

.PHONY: all install uninstall

