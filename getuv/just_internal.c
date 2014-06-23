#include "sync.h"

#include <unistd.h> // execlp

int main(void) {
    syncIntervalUV();
    chdir("..");
    puts("Running meshchat make with 'internal libuv' flag");
    setenv("MAKEFLAGS","--no-print-directory",1);
    execlp("make", "make", "-f", "main.mk","INTERNAL_LIBUV=1", NULL);
}
