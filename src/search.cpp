#include "search.h"
#include "scandir.h"

size_t alpha_skip_lookup[256];
size_t *find_skip_lookup;

pthread_mutex_t print_mtx;
pthread_mutex_t stats_mtx;

symdir_t *symhash;

void search_buf(const char *buf, const size_t buf_len,
                const char *dir_full_path) {
    int binary = -1; /* 1 = yes, 0 = no, -1 = don't know */
    size_t buf_offset = 0;

    if (opts.search_stream) {
        binary = 0;
    } else if (!opts.search_binary_files) {
        binary = is_binary((const void *)buf, buf_len);
        if (binary) {
            log_debug("File %s is binary. Skipping...", dir_full_path);
//			printf("File %s is binary. Skipping...\n", dir_full_path);
            return;
        }
    }

    size_t matches_len = 0;
    match_t *matches;
    size_t matches_size;
    size_t matches_spare;

    if (opts.invert_match) {
        /* If we are going to invert the set of matches at the end, we will need
         * one extra match struct, even if there are no matches at all. So make
         * sure we have a nonempty array; and make sure we always have spare
         * capacity for one extra.
         */
        matches_size = 100;
        matches = (match_t*) ag_malloc(matches_size * sizeof(match_t));
        matches_spare = 1;
    } else {
        matches_size = 0;
        matches = NULL;
        matches_spare = 0;
    }

    if (!opts.literal && opts.query_len == 1 && opts.query[0] == '.') {
        matches_size = 1;
        matches = (match_t*) ag_malloc(matches_size * sizeof(match_t));
        matches[0].start = 0;
        matches[0].end = buf_len;
        matches_len = 1;
    } else if (opts.literal) {
        const char *match_ptr = buf;
        strncmp_fp ag_strnstr_fp = get_strstr(opts.casing);

        while (buf_offset < buf_len) {
            match_ptr = ag_strnstr_fp(match_ptr, opts.query, buf_len - buf_offset, opts.query_len, alpha_skip_lookup, find_skip_lookup);
            if (match_ptr == NULL) {
                break;
            }

            if (opts.word_regexp) {
                const char *start = match_ptr;
                const char *end = match_ptr + opts.query_len;

                /* Check whether both start and end of the match lie on a word
                 * boundary
                 */
                if ((start == buf ||
                     is_wordchar(*(start - 1)) != opts.literal_starts_wordchar) &&
                    (end == buf + buf_len ||
                     is_wordchar(*end) != opts.literal_ends_wordchar)) {
                    /* It's a match */
                } else {
                    /* It's not a match */
                    match_ptr += opts.query_len;
                    buf_offset = end - buf;
                    continue;
                }
            }

            realloc_matches(&matches, &matches_size, matches_len + matches_spare);

            matches[matches_len].start = match_ptr - buf;
            matches[matches_len].end = matches[matches_len].start + opts.query_len;
            buf_offset = matches[matches_len].end;
            log_debug("Match found. File %s, offset %lu bytes.", dir_full_path, matches[matches_len].start);
            matches_len++;
            match_ptr += opts.query_len;

            if (opts.max_matches_per_file > 0 && matches_len >= opts.max_matches_per_file) {
                log_err("Too many matches in %s. Skipping the rest of this file.", dir_full_path);
                break;
            }
        }
    } else {
		const void* results[opts.pattern->GetNumberOfCaptures()*2];
        if (opts.multiline) {
            while (buf_offset < buf_len &&
				   opts.pattern->PartialMatch(buf, buf_len, results, buf_offset)) {
				int offset_vector[2];
				offset_vector[0] = int(intptr_t(results[0]) - intptr_t(buf));
				offset_vector[1] = int(intptr_t(results[1]) - intptr_t(buf));
                log_debug("Regex match found. File %s, offset %i bytes.", dir_full_path, offset_vector[0]);
                buf_offset = offset_vector[1];
                if (offset_vector[0] == offset_vector[1]) {
                    ++buf_offset;
                    log_debug("Regex match is of length zero. Advancing offset one byte.");
                }

                realloc_matches(&matches, &matches_size, matches_len + matches_spare);

                matches[matches_len].start = offset_vector[0];
                matches[matches_len].end = offset_vector[1];
                matches_len++;

                if (opts.max_matches_per_file > 0 && matches_len >= opts.max_matches_per_file) {
                    log_err("Too many matches in %s. Skipping the rest of this file.", dir_full_path);
                    break;
                }
            }
        } else {
            while (buf_offset < buf_len) {
                const char *line;
                size_t line_len = buf_getline(&line, buf, buf_len, buf_offset);
                if (!line) {
                    break;
                }
                size_t line_offset = 0;
                while (line_offset < line_len) {
					if(!opts.pattern->PartialMatch(line, line_len, results, line_offset)) break;
					int offset_vector[2];
					offset_vector[0] = int(intptr_t(results[0]) - intptr_t(buf));
					offset_vector[1] = int(intptr_t(results[1]) - intptr_t(buf));

					size_t line_to_buf = buf_offset + line_offset;
                    log_debug("Regex match found. File %s, offset %i bytes.", dir_full_path, offset_vector[0]);
                    line_offset = offset_vector[1];
                    if (offset_vector[0] == offset_vector[1]) {
                        ++line_offset;
                        log_debug("Regex match is of length zero. Advancing offset one byte.");
                    }

                    realloc_matches(&matches, &matches_size, matches_len + matches_spare);

                    matches[matches_len].start = offset_vector[0] + line_to_buf;
                    matches[matches_len].end = offset_vector[1] + line_to_buf;
                    matches_len++;

                    if (opts.max_matches_per_file > 0 && matches_len >= opts.max_matches_per_file) {
                        log_err("Too many matches in %s. Skipping the rest of this file.", dir_full_path);
                        goto multiline_done;
                    }
                }
                buf_offset += line_len + 1;
            }
        }
    }

multiline_done:

    if (opts.invert_match) {
        matches_len = invert_matches(buf, buf_len, matches, matches_len);
    }

    if (opts.stats) {
        pthread_mutex_lock(&stats_mtx);
        stats.total_bytes += buf_len;
        stats.total_files++;
        stats.total_matches += matches_len;
        if (matches_len > 0) {
            stats.total_file_matches++;
        }
        pthread_mutex_unlock(&stats_mtx);
    }

    if (matches_len > 0) {
        if (binary == -1 && !opts.print_filename_only) {
            binary = is_binary((const void *)buf, buf_len);
        }
        pthread_mutex_lock(&print_mtx);
        if (opts.print_filename_only) {
            /* If the --files-without-matches or -L option is passed we should
             * not print a matching line. This option currently sets
             * opts.print_filename_only and opts.invert_match. Unfortunately
             * setting the latter has the side effect of making matches.len = 1
             * on a file-without-matches which is not desired behaviour. See
             * GitHub issue 206 for the consequences if this behaviour is not
             * checked. */
            if (!opts.invert_match || matches_len < 2) {
                if (opts.print_count) {
                    print_path_count(dir_full_path, opts.path_sep, (size_t)matches_len);
                } else {
                    print_path(dir_full_path, opts.path_sep);
                }
            }
        } else if (binary) {
            print_binary_file_matches(dir_full_path);
        } else {
            print_file_matches(dir_full_path, buf, buf_len, matches, matches_len);
        }
        pthread_mutex_unlock(&print_mtx);
        opts.match_found = 1;
    } else if (opts.search_stream && opts.passthrough) {
        fprintf(out_fd, "%s", buf);
    } else {
        log_debug("No match in %s", dir_full_path);
    }

    if (matches_size > 0) {
        free(matches);
    }
}

/* TODO: this will only match single lines. multi-line regexes silently don't match */
void search_stream(FILE *stream, const char *path) {
    char *line = NULL;
    ssize_t line_len = 0;
    size_t line_cap = 0;
    size_t i;

    for (i = 1; (line_len = getline(&line, &line_cap, stream)) > 0; i++) {
        opts.stream_line_num = i;
        search_buf(line, line_len, path);
    }

    free(line);
}

void search_file(const char *file_full_path) {
    int fd;
    off_t f_len = 0;
    struct stat statbuf;
    int rv = 0;
    FILE *fp = NULL;
	char stackBuf[2048];
	char *buf = stackBuf;
	const size_t MMAP_THRESHOLD = 256*1024;

    fd = open(file_full_path, O_RDONLY);
    if (fd < 0) {
        /* XXXX: strerror is not thread-safe */
        log_err("Skipping %s: Error opening file: %s", file_full_path, strerror(errno));
        goto cleanup;
    }

    rv = fstat(fd, &statbuf);
    if (rv != 0) {
        log_err("Skipping %s: Error fstat()ing file.", file_full_path);
        goto cleanup;
    }

    if (opts.stdout_inode != 0 && opts.stdout_inode == statbuf.st_ino) {
        log_debug("Skipping %s: stdout is redirected to it", file_full_path);
        goto cleanup;
    }

    if ((statbuf.st_mode & S_IFMT) == 0) {
        log_err("Skipping %s: Mode %u is not a file.", file_full_path, statbuf.st_mode);
        goto cleanup;
    }

    if (statbuf.st_mode & S_IFIFO) {
        log_debug("%s is a named pipe. stream searching", file_full_path);
        fp = fdopen(fd, "r");
        search_stream(fp, file_full_path);
        fclose(fp);
        goto cleanup;
    }

    f_len = statbuf.st_size;

    if (f_len == 0) {
        log_debug("Skipping %s: file is empty.", file_full_path);
        goto cleanup;
    }

    if (!opts.literal && f_len > INT_MAX) {
        log_err("Skipping %s: pcre_exec() can't handle files larger than %i bytes.", file_full_path, INT_MAX);
        goto cleanup;
    }

	if(f_len < MMAP_THRESHOLD)
	{
		if(f_len > sizeof(stackBuf)) buf = (char*) malloc(f_len);
		read(fd, buf, f_len);
	}
	else
	{
	
	#ifdef _WIN32
		{
			HANDLE hmmap = CreateFileMapping(
				(HANDLE)_get_osfhandle(fd), 0, PAGE_READONLY, 0, f_len, NULL);
			buf = (char *)MapViewOfFile(hmmap, FILE_SHARE_READ, 0, 0, f_len);
			if (hmmap != NULL)
				CloseHandle(hmmap);
		}
		if (buf == NULL) {
			FormatMessageA(
				FORMAT_MESSAGE_ALLOCATE_BUFFER |
					FORMAT_MESSAGE_FROM_SYSTEM |
					FORMAT_MESSAGE_IGNORE_INSERTS,
				NULL, GetLastError(), 0, (void *)&buf, 0, NULL);
			log_err("File %s failed to load: %s.", file_full_path, buf);
			LocalFree((void *)buf);
			goto cleanup;
		}
	#else
		buf = (char*) mmap(0, f_len, PROT_READ, MAP_SHARED, fd, 0);
		if (buf == MAP_FAILED) {
			log_err("File %s failed to load: %s.", file_full_path, strerror(errno));
			goto cleanup;
		}
	#if HAVE_MADVISE
		madvise(buf, f_len, MADV_SEQUENTIAL);
	#elif HAVE_POSIX_FADVISE
		posix_fadvise(fd, 0, f_len, POSIX_MADV_SEQUENTIAL);
	#endif
	#endif
	}
	
    if (opts.search_zip_files) {
        ag_compression_type zip_type = is_zipped(buf, f_len);
        if (zip_type != AG_NO_COMPRESSION) {
            int _buf_len = (int)f_len;
            char *_buf = (char*) decompress(zip_type, buf, f_len, file_full_path, &_buf_len);
            if (_buf == NULL || _buf_len == 0) {
                log_err("Cannot decompress zipped file %s", file_full_path);
                goto cleanup;
            }
            search_buf(_buf, _buf_len, file_full_path);
            free(_buf);
            goto cleanup;
        }
    }

    search_buf(buf, f_len, file_full_path);

cleanup:

    if (buf != stackBuf) {
		if(f_len < MMAP_THRESHOLD)
		{
			free(buf);
		}
		else
		{
	#ifdef _WIN32
			UnmapViewOfFile(buf);
	#else
			munmap(buf, f_len);
	#endif
		}
    }
    if (fd != -1) {
        close(fd);
    }
}

class SearchFileTask : public Javelin::Task
{
public:
	SearchFileTask(const char* aPath) : path(aPath) { }
	~SearchFileTask() { delete path; }
	
private:
	virtual void RunTask() override
	{
		search_file(path);
	}
	
	const char* path;
};

static int check_symloop_enter(const char *path, dirkey_t *outkey) {
#ifdef _WIN32
    return SYMLOOP_OK;
#else
    struct stat buf;
    symdir_t *item_found = NULL;
    symdir_t *new_item = NULL;

    memset(outkey, 0, sizeof(dirkey_t));
    outkey->dev = 0;
    outkey->ino = 0;

    int res = stat(path, &buf);
    if (res != 0) {
        log_err("Error stat()ing: %s", path);
        return SYMLOOP_ERROR;
    }

    outkey->dev = buf.st_dev;
    outkey->ino = buf.st_ino;

    HASH_FIND(hh, symhash, outkey, sizeof(dirkey_t), item_found);
    if (item_found) {
        return SYMLOOP_LOOP;
    }

    new_item = (symdir_t *)ag_malloc(sizeof(symdir_t));
    memcpy(&new_item->key, outkey, sizeof(dirkey_t));
    HASH_ADD(hh, symhash, key, sizeof(dirkey_t), new_item);
    return SYMLOOP_OK;
#endif
}

static int check_symloop_leave(dirkey_t *dirkey) {
#ifdef _WIN32
    return SYMLOOP_OK;
#else
    symdir_t *item_found = NULL;

    if (dirkey->dev == 0 && dirkey->ino == 0) {
        return SYMLOOP_ERROR;
    }

    HASH_FIND(hh, symhash, dirkey, sizeof(dirkey_t), item_found);
    if (!item_found) {
        log_err("item not found! weird stuff...\n");
        return SYMLOOP_ERROR;
    }

    HASH_DELETE(hh, symhash, item_found);
    free(item_found);
    return SYMLOOP_OK;
#endif
}

void process_dirent(struct dirent *dir, scandir_baton_t& scandir_baton, const char* path, dev_t original_dev, int depth)
{
	char *dir_full_path = NULL;
	if(!filename_filter(path, dir, &scandir_baton)) goto cleanup;
	
	ag_asprintf(&dir_full_path, "%s/%s", path, dir->d_name);
#ifndef _WIN32
	if (opts.one_dev) {
		struct stat s;
		if (lstat(dir_full_path, &s) != 0) {
			log_err("Failed to get device information for %s. Skipping...", dir->d_name);
			goto cleanup;
		}
		if (s.st_dev != original_dev) {
			log_debug("File %s crosses a device boundary (is probably a mount point.) Skipping...", dir->d_name);
			goto cleanup;
		}
	}
#endif
	
	/* If a link points to a directory then we need to treat it as a directory. */
	if (!opts.follow_symlinks && is_symlink(path, dir)) {
		log_debug("File %s ignored becaused it's a symlink", dir->d_name);
		goto cleanup;
	}
	
	if (!is_directory(path, dir)) {
		if(opts.file_search_pattern) {
			if(opts.file_search_pattern->HasPartialMatch(dir->d_name, dir->d_namlen)) {
				log_debug("match_files: file_search_regex matched for %s.", dir_full_path);
				pthread_mutex_lock(&print_mtx);
				print_path(dir_full_path, opts.path_sep);
				pthread_mutex_unlock(&print_mtx);
				opts.match_found = 1;
				goto cleanup;
			} else {
				log_debug("Skipping %s due to file_search_regex.", dir_full_path);
				goto cleanup;
			}
		}
		
		if(opts.binary_ignore_pattern) {
			if(opts.binary_ignore_pattern->HasPartialMatch(dir->d_name, dir->d_namlen)) {
				goto cleanup;
			}
		}
		
		log_debug("%s adding to work queue", dir_full_path);
		Javelin::ThreadPool::GetSharedThreadPool().AddTask(new SearchFileTask(dir_full_path));
		dir_full_path = NULL;
	} else if (opts.recurse_dirs) {
		if (depth < opts.max_search_depth || opts.max_search_depth == -1) {
			log_debug("Searching dir %s", dir_full_path);
			ignores *child_ig;
#ifdef HAVE_DIRENT_DNAMLEN
			child_ig = init_ignore(scandir_baton.ig, dir->d_name, dir->d_namlen);
#else
			child_ig = init_ignore(scandir_baton.ig, dir->d_name, strlen(dir->d_name));
#endif
			search_dir(child_ig, scandir_baton.base_path, dir_full_path, depth + 1,
					   original_dev);
			cleanup_ignore(child_ig);
		} else {
			if (opts.max_search_depth == DEFAULT_MAX_SEARCH_DEPTH) {
				/*
				 * If the user didn't intentionally specify a particular depth,
				 * this is a warning...
				 */
				log_err("Skipping %s. Use the --depth option to search deeper.", dir_full_path);
			} else {
				/* ... if they did, let's settle for debug. */
				log_debug("Skipping %s. Use the --depth option to search deeper.", dir_full_path);
			}
		}
	}
	
cleanup:
	free(dir);
	dir = NULL;
	free(dir_full_path);
	dir_full_path = NULL;
}

static void update_ignores(ignores* ig, dirent **dir_list, int results, const char* path)
{
	const void* captures[4];
	captures[2] = nullptr;
	captures[3] = nullptr;

	for(int i = 0; i < results; ++i)
	{
		const char* name = dir_list[i]->d_name;
		
		if(opts.vcs_ignore_pattern->FullMatch(name, strlen(name), captures))
		{
			char *dir_full_path;
			if(captures[2] != captures[3])
			{
				// We've found .git. Try to load .git/info/excludes
				ag_asprintf(&dir_full_path, "%s/.git/info/excludes", path);
				captures[2] = nullptr;
				captures[3] = nullptr;
			}
			else
			{
				// We've found an ignore file
				ag_asprintf(&dir_full_path, "%s/%s", path, name);
			}
			
			if (strcmp(SVN_DIR, name) == 0) {
				load_svn_ignore_patterns(ig, dir_full_path);
			} else {
				load_ignore_patterns(ig, dir_full_path);
			}
			
			free(dir_full_path);
		}
	}
	
	if (opts.path_to_agignore) {
		load_ignore_patterns(ig, opts.path_to_agignore);
	}

	build_patterns(ig);
}

/* TODO: Append matches to some data structure instead of just printing them out.
 * Then ag can have sweet summaries of matches/files scanned/time/etc.
 */
void search_dir(ignores *ig, const char *base_path, const char *path, const int depth,
                dev_t original_dev) {
    struct dirent **dir_list = NULL;
    scandir_baton_t scandir_baton;
    int results = 0;

    int i;

    int symres;
    dirkey_t current_dirkey;

    symres = check_symloop_enter(path, &current_dirkey);
    if (symres == SYMLOOP_LOOP) {
        log_err("Recursive directory loop: %s", path);
        return;
    }

	results = ag_scandir_no_filter(path, &dir_list);
	if(results == 0)
	{
		log_debug("No results found in directory %s", path);
		goto search_dir_cleanup;
	}
	if(results == -1)
	{
		if (errno == ENOTDIR) {
			/* Not a directory. Probably a file. */
			if (depth == 0 && opts.paths_len == 1) {
				/* If we're only searching one file, don't print the filename header at the top. */
				if (opts.print_path == PATH_PRINT_DEFAULT || opts.print_path == PATH_PRINT_DEFAULT_EACH_LINE) {
					opts.print_path = PATH_PRINT_NOTHING;
				}
				/* If we're only searching one file and --only-matching is specified, disable line numbers too. */
				if (opts.only_matching && opts.print_path == PATH_PRINT_NOTHING) {
					opts.print_line_numbers = FALSE;
				}
			}
			search_file(path);
		} else {
			log_err("Error opening directory %s: %s", path, strerror(errno));
		}
		goto search_dir_cleanup;
	}
	
	update_ignores(ig, dir_list, results, path);

    scandir_baton.ig = ig;
    scandir_baton.base_path = base_path;
    scandir_baton.base_path_len = base_path ? strlen(base_path) : 0;

	for (i = 0; i < results; i++) {
		process_dirent(dir_list[i], scandir_baton, path, original_dev, depth);
	}

search_dir_cleanup:
    check_symloop_leave(&current_dirkey);
    free(dir_list);
}
