#include "sync.h"

#include <uv.h> // uv-version.h does not exist for older libuv versions

#include <stdio.h>
#include <unistd.h> // execlp

#ifndef UV_VERSION_PATCH
#define UV_VERSION_PATCH -9999
#endif

#ifndef UV_VERSION_IS_RELEASE
#define UV_VERSION_IS_RELEASE -1337
#endif

int main(void) {
/* libuv version check */

    int internal = 0;

    if(getenv("INTERNAL_LIBUV")) 
        internal = 1;

#if UV_VERSION_MAJOR < 0
    internal = 1;
#elif UV_VERSION_MAJOR == 0
#  if UV_VERSION_MINOR < 10
    internal = 1;
#  elif UV_VERSION_MINOR == 10
#    if UV_VERSION_PATCH < 12
    internal = 1;
#    elif UV_VERSION_PATCH == 12
#      if UV_VERSION_IS_RELEASE == 0
    internal = 1;
#      endif
#    endif
#  endif
#endif
    if(internal == 1) {
        printf("Your libuv is too old. You have libuv version %d.%d.%d\n",UV_VERSION_MAJOR,UV_VERSION_MINOR,UV_VERSION_PATCH);
        if(UV_VERSION_IS_RELEASE == 0) {
            fputs("-rc",stdout);
        } 
        syncIntervalUV();        
        chdir("..");
        puts("Running meshchat make with 'internal libuv' flag");
        execlp("make", "make", "-f", "main.mk","INTERNAL_LIBUV=1", NULL);
    } else {
        chdir("..");
        puts("Running meshchat make using your system libuv (yay!)");
        execlp("make", "make", "-f", "main.mk", NULL);
    }

    fputs("could not exec\n",stderr);
    return 0;
}
