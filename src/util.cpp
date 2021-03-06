#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "util.h"
#include "config.h"

#ifdef _WIN32
#include <windows.h>
#define flockfile(x)
#define funlockfile(x)
#define getc_unlocked(x) getc(x)
#endif

#define CHECK_AND_RETURN(ptr)             \
    if (ptr == NULL) {                    \
        die("Memory allocation failed."); \
    }                                     \
    return ptr;

FILE *out_fd;

void *ag_malloc(size_t size) {
    void *ptr = malloc(size);
    CHECK_AND_RETURN(ptr)
}

void *ag_realloc(void *ptr, size_t size) {
    void *new_ptr = realloc(ptr, size);
    CHECK_AND_RETURN(new_ptr)
}

void *ag_calloc(size_t count, size_t size) {
    void *ptr = calloc(count, size);
    CHECK_AND_RETURN(ptr)
}

char *ag_strdup(const char *s) {
    char *str = strdup(s);
    CHECK_AND_RETURN(str)
}

char *ag_strndup(const char *s, size_t size) {
    char *str = NULL;
#ifdef HAVE_STRNDUP
    str = strndup(s, size);
    CHECK_AND_RETURN(str)
#else
    str = (char *)ag_malloc(size + 1);
    strlcpy(str, s, size + 1);
    return str;
#endif
}

void free_strings(char **strs, const size_t strs_len) {
    if (strs == NULL) {
        return;
    }
    size_t i;
    for (i = 0; i < strs_len; i++) {
        free(strs[i]);
    }
    free(strs);
}

size_t invert_matches(const char *buf, const size_t buf_len, match_t matches[], size_t matches_len) {
    size_t i;
    size_t match_read_index = 0;
    size_t inverted_match_count = 0;
    size_t inverted_match_start = 0;
    size_t last_line_end = 0;
    int in_inverted_match = TRUE;
    match_t next_match;

    log_debug("Inverting %u matches.", matches_len);

    if (matches_len > 0) {
        next_match = matches[0];
    } else {
        next_match.start = buf_len + 1;
    }

    /* No matches, so the whole buffer is now a match. */
    if (matches_len == 0) {
        matches[0].start = 0;
        matches[0].end = buf_len - 1;
        return 1;
    }

    for (i = 0; i < buf_len; i++) {
        if (i == next_match.start) {
            i = next_match.end - 1;

            match_read_index++;

            if (match_read_index < matches_len) {
                next_match = matches[match_read_index];
            }

            if (in_inverted_match && last_line_end > inverted_match_start) {
                matches[inverted_match_count].start = inverted_match_start;
                matches[inverted_match_count].end = last_line_end - 1;

                inverted_match_count++;
            }

            in_inverted_match = FALSE;
        } else if (i == buf_len - 1 && in_inverted_match) {
            matches[inverted_match_count].start = inverted_match_start;
            matches[inverted_match_count].end = i;

            inverted_match_count++;
        } else if (buf[i] == '\n') {
            last_line_end = i + 1;

            if (!in_inverted_match) {
                inverted_match_start = last_line_end;
            }

            in_inverted_match = TRUE;
        }
    }

    for (i = 0; i < matches_len; i++) {
        log_debug("Inverted match %i start %i end %i.", i, matches[i].start, matches[i].end);
    }

    return inverted_match_count;
}

void realloc_matches(match_t **matches, size_t *matches_size, size_t matches_len) {
    if (matches_len < *matches_size) {
        return;
    }
    /* TODO: benchmark initial size of matches. 100 may be too small/big */
    *matches_size = *matches ? *matches_size * 2 : 100;
    *matches = (match_t*) ag_realloc(*matches, *matches_size * sizeof(match_t));
}

void compile_pattern(Javelin::Pattern **re, char *q, const int options) {
	int javelin_options = Javelin::Pattern::AUTO_CLUSTER | options;
	
	Javelin::String s{q};
	try {
		*re = new Javelin::Pattern(s, javelin_options);
	}
	catch(const Javelin::PatternException& exception) {
		static const char *const PATTERN_EXCEPTION_TEXT[] =
		{
			"None",
			"InternalError",
			"Missing ')'",
			"InvalidBackReference",
			"InvalidOptions",
			"LookBehindNotConstantByteLength",
			"MalformedConditional",
			"MaximumRepetitionCountExceeded",
			"MinimumCountExceedsMaximumCount",
			"TooManyByteCodeInstructions",
			"TooManyCaptures",
			"TooManyProgressCheckInstructions",
			"UnableToParseGroupType",
			"UnableToParseRepetition",
			"UnableToResolveRecurseTarget",
			"UnexpectedControlCharacter",
			"UnexpectedEndOfPattern",
			"UnexpectedGroupOptions",
			"UnexpectedHexCharacter",
			"UnexpectedLookBehindType",
			"UnexpectedToken",
			"UnknownEscape",
			"UnknownPosixCharacterClass",
		};
		
		const void* context = exception.GetContext();
		if(context != nullptr)
		{
			die("Bad regex! Javelin::Pattern failed: %s before position %zu\nIf you meant to search for a literal string, run ag with -Q", PATTERN_EXCEPTION_TEXT[(int) exception.GetType()], uintptr_t(context) - uintptr_t(s.GetData()));
		}
		else
		{
			die("Bad regex! Javelin::Pattern failed: %s\nIf you meant to search for a literal string, run ag with -Q", PATTERN_EXCEPTION_TEXT[(int) exception.GetType()]);
		}
	}
	catch(const Javelin::Exception& exception) {
		die("Bad regex! Javelin::Pattern failed\nIf you meant to search for a literal string, run ag with -Q");
	}
}

/* This function is very hot. It's called on every file. */
static const bool DO_UTF8_CHECK[256] =
{
	true, true, true, true, true, true, false, false,
	false, false, false, false, false, false, false, true,
	true, true, true, true, true, true, true, true,			// 16
	true, true, true, true, true, true, true, true,
	false, false, false, false, false, false, false, false,	// 32
	false, false, false, false, false, false, false, false,
	false, false, false, false, false, false, false, false, // 48
	false, false, false, false, false, false, false, false,
	false, false, false, false, false, false, false, false, // 64
	false, false, false, false, false, false, false, false,
	false, false, false, false, false, false, false, false, // 80
	false, false, false, false, false, false, false, false,
	false, false, false, false, false, false, false, false, // 96
	false, false, false, false, false, false, false, false,
	false, false, false, false, false, false, false, false, // 112
	false, false, false, false, false, false, false, false,
	true, true, true, true, true, true, true, true,			// 128
	true, true, true, true, true, true, true, true,
	true, true, true, true, true, true, true, true,
	true, true, true, true, true, true, true, true,
	true, true, true, true, true, true, true, true,
	true, true, true, true, true, true, true, true,
	true, true, true, true, true, true, true, true,
	true, true, true, true, true, true, true, true,
	true, true, true, true, true, true, true, true,
	true, true, true, true, true, true, true, true,
	true, true, true, true, true, true, true, true,
	true, true, true, true, true, true, true, true,
	true, true, true, true, true, true, true, true,
	true, true, true, true, true, true, true, true,
	true, true, true, true, true, true, true, true,
	true, true, true, true, true, true, true, true,
};

int is_binary(const void *buf, const size_t buf_len) {
    size_t suspicious_bytes = 0;
    size_t total_bytes = buf_len > 512 ? 512 : buf_len;
    const unsigned char *buf_c = (const unsigned char*) buf;
    size_t i;

    if (buf_len == 0) {
        return 0;
    }

    if (buf_len >= 3 && buf_c[0] == 0xEF && buf_c[1] == 0xBB && buf_c[2] == 0xBF) {
        /* UTF-8 BOM. This isn't binary. */
        return 0;
    }

    if (buf_len >= 5 && memcmp(buf, "%PDF-", 5) == 0) {
        /* PDF. This is binary. */
        return 1;
    }

	if(memchr(buf, '\0', total_bytes) != nullptr)
	{
		return 1;
	}
	
    for (i = 0; i < total_bytes; i++) {
        if (DO_UTF8_CHECK[buf_c[i]]) {
            /* UTF-8 detection */
            if (192 <= buf_c[i] && buf_c[i] < 224) {
				i++;
				if(i >= total_bytes) continue;
				if (128 <= buf_c[i] && buf_c[i] < 192) {
                    continue;
                }
            } else if (224 <= buf_c[i] && buf_c[i] < 240) {
				i += 2;
				if(i >= total_bytes) continue;
				
				// This could be one 16-bit check, but the compiler is not
				// optimizing other code when I write:
				// if ((*(uint16_t*) &buf_c[i-1]) & 0xc0c0) == 0x8080) {
                if ((128 <= buf_c[i-1] && buf_c[i-1] < 192)
					&& 128 <= buf_c[i] && buf_c[i] < 192) {
                    continue;
                }
            }
            suspicious_bytes++;
        }
    }
	
    if (suspicious_bytes * 10 > total_bytes) {
        return 1;
    }

    return 0;
}

int is_fnmatch(const char *filename) {
    char fnmatch_chars[] = {
        '!',
        '*',
        '?',
        '[',
        ']',
        '\0'
    };

    return (strpbrk(filename, fnmatch_chars) != NULL);
}

int binary_search(const char *needle, char **haystack, int start, int end) {
    int mid;
    int rc;

    if (start == end) {
        return -1;
    }

    mid = (start + end) / 2; /* can screw up on arrays with > 2 billion elements */

    rc = strcmp(needle, haystack[mid]);
    if (rc < 0) {
        return binary_search(needle, haystack, start, mid);
    } else if (rc > 0) {
        return binary_search(needle, haystack, mid + 1, end);
    }

    return mid;
}

int is_lowercase(const char *s) {
    int i;
    for (i = 0; s[i] != '\0'; i++) {
        if (!isascii(s[i]) || isupper(s[i])) {
            return FALSE;
        }
    }
    return TRUE;
}

int is_directory(const char *path, const struct dirent *d) {
#ifdef HAVE_DIRENT_DTYPE
    /* Some filesystems, e.g. ReiserFS, always return a type DT_UNKNOWN from readdir or scandir. */
    /* Call stat if we don't find DT_DIR to get the information we need. */
    /* Also works for symbolic links to directories. */
    if (d->d_type != DT_UNKNOWN && d->d_type != DT_LNK) {
        return d->d_type == DT_DIR;
    }
#endif
    char *full_path;
    struct stat s;
    ag_asprintf(&full_path, "%s/%s", path, d->d_name);
    if (stat(full_path, &s) != 0) {
        free(full_path);
        return FALSE;
    }
#ifdef _WIN32
    int is_dir = GetFileAttributesA(full_path) & FILE_ATTRIBUTE_DIRECTORY;
#else
    int is_dir = S_ISDIR(s.st_mode);
#endif
    free(full_path);
    return is_dir;
}

int is_symlink(const char *path, const struct dirent *d) {
#ifdef _WIN32
    char full_path[MAX_PATH + 1] = { 0 };
    sprintf(full_path, "%s\\%s", path, d->d_name);
    return (GetFileAttributesA(full_path) & FILE_ATTRIBUTE_REPARSE_POINT);
#else
#ifdef HAVE_DIRENT_DTYPE
    /* Some filesystems, e.g. ReiserFS, always return a type DT_UNKNOWN from readdir or scandir. */
    /* Call lstat if we find DT_UNKNOWN to get the information we need. */
    if (d->d_type != DT_UNKNOWN) {
        return (d->d_type == DT_LNK);
    }
#endif
    char *full_path;
    struct stat s;
    ag_asprintf(&full_path, "%s/%s", path, d->d_name);
    if (lstat(full_path, &s) != 0) {
        free(full_path);
        return FALSE;
    }
    free(full_path);
    return S_ISLNK(s.st_mode);
#endif
}

int is_named_pipe(const char *path, const struct dirent *d) {
#ifdef HAVE_DIRENT_DTYPE
    if (d->d_type != DT_UNKNOWN) {
        return d->d_type == DT_FIFO;
    }
#endif
    char *full_path;
    struct stat s;
    ag_asprintf(&full_path, "%s/%s", path, d->d_name);
    if (stat(full_path, &s) != 0) {
        free(full_path);
        return FALSE;
    }
    free(full_path);
    return S_ISFIFO(s.st_mode);
}

void ag_asprintf(char **ret, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    if (vasprintf(ret, fmt, args) == -1) {
        die("vasprintf returned -1");
    }
    va_end(args);
}

void die(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vplog(LOG_LEVEL_ERR, fmt, args);
    va_end(args);
    exit(2);
}

#ifndef HAVE_FGETLN
char *fgetln(FILE *fp, size_t *lenp) {
    char *buf = NULL;
    int c, used = 0, len = 0;

    flockfile(fp);
    while ((c = getc_unlocked(fp)) != EOF) {
        if (!buf || len >= used) {
            size_t nsize;
            char *newbuf;
            nsize = used + BUFSIZ;
            if (!(newbuf = realloc(buf, nsize))) {
                funlockfile(fp);
                if (buf)
                    free(buf);
                return NULL;
            }
            buf = newbuf;
            used = nsize;
        }
        buf[len++] = c;
        if (c == '\n') {
            break;
        }
    }
    funlockfile(fp);
    *lenp = len;
    return buf;
}
#endif

#ifndef HAVE_GETLINE
/*
 * Do it yourself getline() implementation
 */
ssize_t getline(char **lineptr, size_t *n, FILE *stream) {
    size_t len = 0;
    char *srcln = NULL;
    char *newlnptr = NULL;

    /* get line, bail on error */
    if (!(srcln = fgetln(stream, &len))) {
        return -1;
    }

    if (len >= *n) {
        /* line is too big for buffer, must realloc */
        /* double the buffer, bail on error */
        if (!(newlnptr = realloc(*lineptr, len * 2))) {
            return -1;
        }
        *lineptr = newlnptr;
        *n = len * 2;
    }

    memcpy(*lineptr, srcln, len);

#ifndef HAVE_FGETLN
    /* Our own implementation of fgetln() returns a malloc()d buffer that we
     * must free
     */
    free(srcln);
#endif

    (*lineptr)[len] = '\0';
    return len;
}
#endif

ssize_t buf_getline(const char **line, const char *buf, const size_t buf_len, const size_t buf_offset) {
	const char* search = buf + buf_offset;
	const char* found = (const char*) memchr(search, '\n', buf_len-buf_offset);
	*line = search;
	if(!found) return buf_len - buf_offset;
	else return found - search;
}

#ifndef HAVE_REALPATH
/*
 * realpath() for Windows. Turns slashes into backslashes and calls _fullpath
 */
char *realpath(const char *path, char *resolved_path) {
    char *p;
    char tmp[_MAX_PATH + 1];
    strlcpy(tmp, path, sizeof(tmp));
    p = tmp;
    while (*p) {
        if (*p == '/') {
            *p = '\\';
        }
        p++;
    }
    return _fullpath(resolved_path, tmp, _MAX_PATH);
}
#endif

#ifndef HAVE_STRLCPY
size_t strlcpy(char *dst, const char *src, size_t size) {
    char *d = dst;
    const char *s = src;
    size_t n = size;

    /* Copy as many bytes as will fit */
    if (n != 0) {
        while (--n != 0) {
            if ((*d++ = *s++) == '\0') {
                break;
            }
        }
    }

    /* Not enough room in dst, add NUL and traverse rest of src */
    if (n == 0) {
        if (size != 0) {
            *d = '\0'; /* NUL-terminate dst */
        }

        while (*s++) {
        }
    }

    return (s - src - 1); /* count does not include NUL */
}
#endif

#ifndef HAVE_VASPRINTF
int vasprintf(char **ret, const char *fmt, va_list args) {
    int rv;
    *ret = NULL;
    va_list args2;
/* vsnprintf can destroy args, so we need to copy it for the second call */
#ifdef __va_copy
    /* non-standard macro, but usually exists */
    __va_copy(args2, args);
#elif va_copy
    /* C99 macro. We compile with -std=c89 but you never know */
    va_copy(args2, args);
#else
    /* Ancient compiler. This usually works but there are no guarantees. */
    memcpy(args2, args, sizeof(va_list));
#endif
    rv = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (rv < 0) {
        return rv;
    }
    *ret = malloc(++rv); /* vsnprintf doesn't count \0 */
    if (*ret == NULL) {
        return -1;
    }
    rv = vsnprintf(*ret, rv, fmt, args2);
    va_end(args2);
    if (rv < 0) {
        free(*ret);
    }
    return rv;
}
#endif
