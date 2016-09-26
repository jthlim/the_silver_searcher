#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/time.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include "config.h"

#ifdef HAVE_PTHREAD_H
#include <pthread.h>
#endif

#include "log.h"
#include "options.h"
#include "search.h"
#include "util.h"

typedef struct {
    pthread_t thread;
    int id;
} worker_t;

ag_stats mainThreadStats = { };

int main(int argc, char **argv) {
    char **base_paths = NULL;
    char **paths = NULL;
    int i;
	int javelin_opts = Javelin::Pattern::MULTILINE;
    int study_opts = 0;
    worker_t *workers = NULL;
	struct timeval time_start;
	struct timeval time_end;

#ifdef HAVE_PLEDGE
    if (pledge("stdio rpath proc exec", NULL) == -1) {
        die("pledge: %s", strerror(errno));
    }
#endif

    set_log_level(LOG_LEVEL_WARN);

    root_ignores = init_ignore(NULL, "", 0);
    out_fd = stdout;

    parse_options(argc, argv, &base_paths, &paths);
    if (opts.stats) {
        gettimeofday(&time_start, NULL);
		threadLocalStats = &mainThreadStats;
    }

    if (pthread_mutex_init(&print_mtx, NULL)) {
        die("pthread_mutex_init failed!");
    }

    if (opts.casing == CASE_SMART) {
        opts.casing = is_lowercase(opts.query) ? CASE_INSENSITIVE : CASE_SENSITIVE;
    }

    if (opts.literal) {
        if (opts.casing == CASE_INSENSITIVE) {
            /* Search routine needs the query to be lowercase */
            char *c = opts.query;
            for (; *c != '\0'; ++c) {
                *c = (char)tolower(*c);
            }
        }
        generate_alpha_skip(opts.query, opts.query_len, alpha_skip_lookup, opts.casing == CASE_SENSITIVE);
        find_skip_lookup = NULL;
        generate_find_skip(opts.query, opts.query_len, &find_skip_lookup, opts.casing == CASE_SENSITIVE);
        if (opts.word_regexp) {
            init_wordchar_table();
            opts.literal_starts_wordchar = is_wordchar(opts.query[0]);
            opts.literal_ends_wordchar = is_wordchar(opts.query[opts.query_len - 1]);
        }
    } else {
        if (opts.casing == CASE_INSENSITIVE) {
			javelin_opts |= Javelin::Pattern::IGNORE_CASE;
        }
        if (opts.word_regexp) {
            char *word_regexp_query;
            ag_asprintf(&word_regexp_query, "\\b%s\\b", opts.query);
            free(opts.query);
            opts.query = word_regexp_query;
            opts.query_len = strlen(opts.query);
        }
        compile_study(&opts.pattern, opts.query, javelin_opts, study_opts);
    }
	
	if(!opts.search_binary_files)
	{
		opts.binary_ignore_pattern = new Javelin::Pattern(JS("\\.(?:bmp|png|jpg|jpeg|jp2|gif|ico|tiff|tga|pdf|psd|docx|xlsx|pptx|zip|gz|tgz|bz2|wav|ppm|pgm|mp3|mp4|o|a|dll|lib|jar)$"), Javelin::Pattern::IGNORE_CASE | Javelin::Pattern::PREFER_NO_SCAN);
	}

	build_patterns(root_ignores);
    if (opts.search_stream) {
        search_stream(stdin, "");
    } else {
#ifdef HAVE_PLEDGE
        if (pledge("stdio rpath", NULL) == -1) {
            die("pledge: %s", strerror(errno));
        }
#endif
        for (i = 0; paths[i] != NULL; i++) {
            log_debug("searching path %s for %s", paths[i], opts.query);
            ignores *ig = init_ignore(root_ignores, "", 0);
            struct stat s = {.st_dev = 0 };
#ifndef _WIN32
            /* The device is ignored if opts.one_dev is false, so it's fine
             * to leave it at the default 0
             */
            if (opts.one_dev && lstat(paths[i], &s) == -1) {
                log_err("Failed to get device information for path %s. Skipping...", paths[i]);
            }
#endif
            search_dir(ig, base_paths[i], paths[i], 0, s.st_dev);
            cleanup_ignore(ig);
        }
		
		SearchThreadPool::instance.WaitForAllTasksToComplete();
    }

    if (opts.stats) {
		ag_stats& stats = *threadLocalStats;
		SearchThreadPool::instance.AccumulateStats(stats);
		
        gettimeofday(&time_end, NULL);
        double time_diff = ((long)time_end.tv_sec * 1000000 + time_end.tv_usec) -
                           ((long)time_start.tv_sec * 1000000 + time_start.tv_usec);
        time_diff /= 1000000;
        printf("%ld matches\n"
			   "%ld files contained matches\n"
			   "%ld files searched\n"
			   "%ld bytes searched\n"
			   "%f seconds\n",
               stats.total_matches, stats.total_file_matches, stats.total_files, stats.total_bytes, time_diff);
    }

    if (opts.pager) {
        pclose(out_fd);
    }
    cleanup_options();
    pthread_mutex_destroy(&print_mtx);
    cleanup_ignore(root_ignores);
    free(workers);
    for (i = 0; paths[i] != NULL; i++) {
        free(paths[i]);
        free(base_paths[i]);
    }
    free(base_paths);
    free(paths);
    if (find_skip_lookup) {
        free(find_skip_lookup);
    }
    return !opts.match_found;
}
