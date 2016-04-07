#ifndef SCANDIR_H
#define SCANDIR_H

#include "ignore.h"

typedef struct {
    const ignores *ig;
    const char *base_path;
    size_t base_path_len;
} scandir_baton_t;

int ag_scandir_no_filter(const char *dirname, struct dirent ***namelist);

#endif
