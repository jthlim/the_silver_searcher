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

#include "data.h"
#include "log.h"
#include "options.h"
#include "search.h"
#include "util.h"

ag_stats mainThreadStats = { };

int main(int argc, char **argv) {
    char **base_paths = NULL;
    char **paths = NULL;
    int i;
	int javelin_opts = Javelin::Pattern::MULTILINE;
	struct timeval time_start;
	struct timeval time_end;

#ifdef HAVE_PLEDGE
    if (pledge("stdio rpath proc exec", NULL) == -1) {
        die("pledge: %s", strerror(errno));
    }
#endif

	setvbuf(stdout, nullptr, _IOFBF, 0);
    set_log_level(LOG_LEVEL_WARN);
    root_ignores = init_ignore(NULL, "", 0);
    out_fd = stdout;

    parse_options(argc, argv, &base_paths, &paths);
    if (opts.stats) {
        gettimeofday(&time_start, NULL);
		threadLocalStats = &mainThreadStats;
    }

    if (opts.casing == CASE_SMART) {
        opts.casing = is_lowercase(opts.query) ? CASE_INSENSITIVE : CASE_SENSITIVE;
    }

	if (opts.casing == CASE_INSENSITIVE) {
		javelin_opts |= Javelin::Pattern::IGNORE_CASE;
	}
	if (opts.literal) {
		Javelin::String query{opts.query};
		Javelin::String pattern = Javelin::Pattern::EscapeString(query);
		free(opts.query);
		opts.query = strdup(pattern.AsUtf8String());
	}
	if (opts.word_regexp) {
		char *word_regexp_query;
		ag_asprintf(&word_regexp_query, "\\b%s\\b", opts.query);
		free(opts.query);
		opts.query = word_regexp_query;
	}
	compile_pattern(&opts.pattern, opts.query, javelin_opts);
	
	if(!opts.search_binary_files)
	{
//		opts.binary_ignore_pattern = new Javelin::Pattern(JS("\\.(?:bmp|png|jpg|jpeg|jp2|gif|ico|tiff|tga|pdf|psd|docx|xlsx|pptx|zip|gz|tgz|bz2|wav|ppm|pgm|mp3|mp4|o|a|dll|lib|jar)$"), Javelin::Pattern::IGNORE_CASE | Javelin::Pattern::PREFER_NO_SCAN);
		opts.binary_ignore_pattern = new Javelin::Pattern(BINARY_FILENAME_IGNORE_PATTERN, BINARY_FILE_IGNORE_PATTERN_LENGTH);
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

//    cleanup_options();
//    cleanup_ignore(root_ignores);
//    for (i = 0; paths[i] != NULL; i++) {
//        free(paths[i]);
//        free(base_paths[i]);
//    }
//    free(base_paths);
//    free(paths);
//    if (find_skip_lookup) {
//        free(find_skip_lookup);
//    }
//    return !opts.match_found;
	
	fclose(out_fd);
	_Exit(!opts.match_found);
}
