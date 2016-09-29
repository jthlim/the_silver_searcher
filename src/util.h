#ifndef UTIL_H
#define UTIL_H

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>

#include "config.h"
#include "log.h"
#include "options.h"

extern FILE *out_fd;

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

void *ag_malloc(size_t size);
void *ag_realloc(void *ptr, size_t size);
void *ag_calloc(size_t nelem, size_t elsize);
char *ag_strdup(const char *s);
char *ag_strndup(const char *s, size_t size);

typedef struct {
    size_t start; /* Byte at which the match starts */
    size_t end;   /* and where it ends */
} match_t;

struct ag_stats {
	ag_stats() : total_bytes(0), total_files(0), total_matches(0), total_file_matches(0) { }
	
    long total_bytes;
    long total_files;
    long total_matches;
    long total_file_matches;
	
	void AccumulateStats(ag_stats& other)
	{
		total_bytes += other.total_bytes;
		total_files += other.total_files;
		total_matches += other.total_matches;
		total_file_matches += other.total_file_matches;
	}
};

extern Javelin::ThreadLocal<ag_stats*> threadLocalStats;

void free_strings(char **strs, const size_t strs_len);

/* max is already defined on spec-violating compilers such as MinGW */
size_t ag_max(size_t a, size_t b);

size_t invert_matches(const char *buf, const size_t buf_len, match_t matches[], size_t matches_len);
void realloc_matches(match_t **matches, size_t *matches_size, size_t matches_len);
void compile_pattern(Javelin::Pattern **re, char *q, const int options);


int is_binary(const void *buf, const size_t buf_len);
int is_fnmatch(const char *filename);
int binary_search(const char *needle, char **haystack, int start, int end);

int is_lowercase(const char *s);

int is_directory(const char *path, const struct dirent *d);
int is_symlink(const char *path, const struct dirent *d);
int is_named_pipe(const char *path, const struct dirent *d);

void die(const char *fmt, ...);

void ag_asprintf(char **ret, const char *fmt, ...);

ssize_t buf_getline(const char **line, const char *buf, const size_t buf_len, const size_t buf_offset);

#ifndef HAVE_FGETLN
char *fgetln(FILE *fp, size_t *lenp);
#endif
#ifndef HAVE_GETLINE
ssize_t getline(char **lineptr, size_t *n, FILE *stream);
#endif
#ifndef HAVE_REALPATH
char *realpath(const char *path, char *resolved_path);
#endif
#ifndef HAVE_STRLCPY
size_t strlcpy(char *dest, const char *src, size_t size);
#endif
#ifndef HAVE_VASPRINTF
int vasprintf(char **ret, const char *fmt, va_list args);
#endif

#endif
