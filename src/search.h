#ifndef SEARCH_H
#define SEARCH_H

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pcre.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include <sys/stat.h>
#include <unistd.h>

#include "config.h"

#ifdef HAVE_PTHREAD_H
#include <pthread.h>
#endif

#include "ignore.h"
#include "log.h"
#include "options.h"
#include "print.h"
#include "util.h"
#include "decompress.h"

extern size_t alpha_skip_lookup[256];
extern size_t *find_skip_lookup;

extern pthread_mutex_t print_mtx;

class SearchThreadPool : public Javelin::ThreadPool
{
public:
	SearchThreadPool();
	
	void AccumulateStats(ag_stats& stats) const;
	
	static SearchThreadPool instance;
};

/* For symlink loop detection */
#define SYMLOOP_ERROR (-1)
#define SYMLOOP_OK (0)
#define SYMLOOP_LOOP (1)

typedef struct dirkey {
    dev_t dev;
    ino_t ino;
	
	bool operator==(const dirkey& a) const { return dev == a.dev && ino == a.ino; }
	friend size_t GetHash(const dirkey& a) {
		return Javelin::Crc32(&a, sizeof(a));
	}
} dirkey_t;

void search_buf(const char *buf, const size_t buf_len,
                const char *dir_full_path);
void search_stream(FILE *stream, const char *path);
void search_file(const char *file_full_path);

void search_dir(ignores *ig, const char *base_path, const char *path, const int depth, dev_t original_dev);

#endif
