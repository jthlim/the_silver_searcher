#ifndef OPTIONS_H
#define OPTIONS_H

#include <getopt.h>
#include <sys/stat.h>

#include "Javelin/Javelin.h"

#define DEFAULT_AFTER_LEN 2
#define DEFAULT_BEFORE_LEN 2
#define DEFAULT_CONTEXT_LEN 2
#define DEFAULT_MAX_SEARCH_DEPTH 25
enum case_behavior {
    CASE_DEFAULT, /* Changes to CASE_SMART at the end of option parsing */
    CASE_SENSITIVE,
    CASE_INSENSITIVE,
    CASE_SMART,
    CASE_SENSITIVE_RETRY_INSENSITIVE /* for future use */
};

enum path_print_behavior {
    PATH_PRINT_DEFAULT,           /* PRINT_TOP if > 1 file being searched, else PRINT_NOTHING */
    PATH_PRINT_DEFAULT_EACH_LINE, /* PRINT_EACH_LINE if > 1 file being searched, else PRINT_NOTHING */
    PATH_PRINT_TOP,
    PATH_PRINT_EACH_LINE,
    PATH_PRINT_NOTHING
};

typedef struct {
    int ackmate;
	Javelin::Pattern* ackmate_dir_pattern;
    size_t after;
    size_t before;
    enum case_behavior casing;
    const char *file_search_string;
    int match_files;
	Javelin::Pattern* file_search_pattern;
    int color;
    char *color_line_number;
    char *color_match;
    char *color_path;
    int color_win_ansi;
    int column;
    int context;
    int follow_symlinks;
    int invert_match;
    int literal;
    int literal_starts_wordchar;
    int literal_ends_wordchar;
    size_t max_matches_per_file;
    int max_search_depth;
    int multiline;
    int one_dev;
    int only_matching;
    char path_sep;
    char *path_to_agignore;
    int print_break;
    int print_count;
    int print_filename_only;
    int print_path;
    int print_line_numbers;
    int print_long_lines; /* TODO: support this in print.c */
    int passthrough;
	Javelin::Pattern* pattern;
    int recurse_dirs;
    int search_all_files;
    int skip_vcs_ignores;
	Javelin::Pattern* vcs_ignore_pattern;
    int search_binary_files;
	Javelin::Pattern* binary_ignore_pattern;
    int search_zip_files;
    int search_hidden_files;
    int search_stream; /* true if tail -F blah | ag */
    int stats;
    size_t stream_line_num; /* This should totally not be in here */
    int match_found;        /* This should totally not be in here */
    ino_t stdout_inode;
    char *query;
    char *pager;
    int paths_len;
    int parallel;
    int use_thread_affinity;
    int vimgrep;
    size_t width;
    int word_regexp;
} cli_options;

/* global options. parse_options gives it sane values, everything else reads from it */
extern cli_options opts;

typedef struct option option_t;

void usage(void);
void print_version(void);

void init_options(void);
void parse_options(int argc, char **argv, char **base_paths[], char **paths[]);
void cleanup_options(void);

#endif
