// The directory the harness window serves files from: mayhem/www next to the binaries.
#ifndef MAYHEM_SERVER_ROOT_H
#define MAYHEM_SERVER_ROOT_H
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char* mayhem_server_root(void) {
    static char root[PATH_MAX + 32];
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0) {
        exe[n] = 0;
        char* slash = strrchr(exe, '/');
        if (slash != NULL) {
            *slash = 0;
            snprintf(root, sizeof(root), "%s/mayhem/www", exe);
            if (access(root, R_OK) == 0)
                return root;
        }
    }
    return "mayhem/www";
}
#endif
