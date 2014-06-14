#include <uv-version.h>

#include <stdio.h>
#include <stdlib.h> // exit
#include <stdarg.h> // va_*
#include <sys/stat.h>
#include <unistd.h> // execlp
#include <assert.h>

#ifndef UV_VERSION_PATCH
#define UV_VERSION_PATCH -9999
#endif

#ifndef UV_VERSION_IS_RELEASE
#define UV_VERSION_IS_RELEASE -1337
#endif

void die(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr,fmt, args);
    exit(23);
}

int spawnv(int argc, const char** argv) {
    int pid = fork();
    if(pid == 0) {
        execvp((char* const)argv[0],(char** const)argv);
        die("Couldn't exec in runv %s", argv[0]);
    }
    return pid;
}

int run(int argc, ...) {
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

static void syncIntervalUV(void) {
    printf("Your libuv is too old. You have libuv version %d.%d.%d\n",UV_VERSION_MAJOR,UV_VERSION_MINOR,UV_VERSION_PATCH);
    if(UV_VERSION_IS_RELEASE == 0) {
        fputs("-rc",stdout);
    } 
    
    chdir("deps");
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

    chdir("../..");
}

int main(void) {
/* libuv version check */

    int internal = 0;

    if(getenv("HAX")) 
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
        syncIntervalUV();
        puts("Running meshchat make with 'internal libuv' flag");
        execlp("make", "make", "-f", "main.mk","INTERNAL_LIBUV=1", NULL);
    } else {
        execlp("make", "make", "-f", "main.mk", NULL);
    }

    die("could not exec");
    return 0;

}
