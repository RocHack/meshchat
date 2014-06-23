#include "sync.h"

#include <stdarg.h> // va_*
#include <stdlib.h> // exit
#include <stdio.h>
#include <unistd.h> // exec*
#include <sys/stat.h> // stat
#include <assert.h>

static void die(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr,fmt, args);
    exit(23);
}

static int spawnv(int argc, const char** argv) {
    int pid = fork();
    if(pid == 0) {
        execvp((char* const)argv[0],(char** const)argv);
        die("Couldn't exec in runv %s", argv[0]);
    }
    return pid;
}

static int run(int argc, ...) {
    const char** argv = (const char**) alloca((1 + argc) * sizeof(const char*));
    assert(argv);
    int i;
    va_list args;
    va_start(args,argc);
    for(i=0;i<argc;++i) {
        argv[i] = va_arg(args,const char*);
    }
    argv[argc] = NULL;

    int pid = spawnv(argc,argv);
    int status = -1;
    waitpid(pid, &status, 0);
    return status;
}

void syncIntervalUV(void) {
    chdir("../deps/");
    struct stat buf;
    if(stat("libuv",&buf) == 0) {
        chdir("libuv");
#ifdef NETSPAMMY
        puts("Found a local libuv. Updating...");
        run(3, "git","pull","--ff-only");
#else
        puts("Found a local libuv");
#endif
    } else {
        puts("Attempting to download a local libuv");

        if(0 != run(4, "git","clone","https://github.com/joyent/libuv.git","libuv")) {
            die("Couldn't download!");
        }
        puts("Trying to compile local libuv");
        chdir("libuv");
    }
    int autogend = (stat("configure",&buf) == 0);
    if(!autogend) {
        puts("Autogen...");
        assert(0==run(2, "sh","./autogen.sh"));
    }
    if(!autogend || (stat("Makefile",&buf) != 0)) {
        puts("Configure...");
        assert(0==run(3, "./configure","--disable-shared"));
    }

    puts("Making libuv...");
    assert(0==run(1,"make"));

    chdir("../../getuv/");
}
