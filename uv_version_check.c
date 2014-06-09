#include <uv-version.h>
#include <stdio.h>

static void printvers(void) {
    printf("You have libuv version %d.%d.%d",UV_VERSION_MAJOR,UV_VERSION_MINOR,UV_VERSION_PATCH);
    if(UV_VERSION_IS_RELEASE == 0) {
        fputs("-rc",stdout);
    } 
    
    puts(". Please update to 0.10.12 or higher.");
}

int main(void) {
/* libuv version check */

#if UV_VERSION_MAJOR < 0
#  warning wtf mate you can not have an earlier version than the first version ever!
    printvers();
    return 1;
#elif UV_VERSION_MAJOR == 0
#  if UV_VERSION_MINOR < 10
#    warning Please update libuv to 0.10.12 or higher
    printvers();
    return 2;
#  elif UV_VERSION_MINOR == 10
#    if UV_VERSION_PATCH < 12
#      warning Please update libuv to 0.10.12 or higher
    printvers();
    return 3;
#    elif UV_VERSION_PATCH == 12
#      if UV_VERSION_IS_RELEASE == 0
#        warning Oh, so close! Update libuv to 0.10.13, or just 0.10.12 post-release!
    printvers();
    return 4;
#      endif
#    endif
#  endif
#endif
    return 0;
}
