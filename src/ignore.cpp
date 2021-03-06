#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "ignore.h"
#include "log.h"
#include "options.h"
#include "scandir.h"
#include "util.h"

/* TODO: build a huge-ass list of files we want to ignore by default (build cache stuff, pyc files, etc) */

ignores *root_ignores;

/* Warning: changing the first string will break skip_vcs_ignores. */
const char *ignore_pattern_files[] = {
    ".agignore",
    ".gitignore",
    ".git/info/exclude",
    ".hgignore",
    ".svn",
    NULL
};

int is_empty(const ignores *ig) {
    return (ig->extensions_len + ig->names_len + ig->slash_names_len + ig->regexes_len + ig->slash_regexes_len == 0);
};

ignores *init_ignore(const ignores *parent, const char *dirname, const size_t dirname_len) {
    ignores *ig = (ignores*) ag_malloc(sizeof(ignores));
    ig->extensions = NULL;
    ig->extensions_len = 0;
    ig->names = NULL;
    ig->names_len = 0;
    ig->slash_names = NULL;
    ig->slash_names_len = 0;
	ig->patterns = nullptr;
    ig->regexes = NULL;
    ig->regexes_len = 0;
	ig->slash_patterns = nullptr;
    ig->slash_regexes = NULL;
    ig->slash_regexes_len = 0;
    ig->dirname = dirname;
    ig->dirname_len = dirname_len;
	ig->reference_count = 1;

    if (parent && is_empty(parent) && parent->parent) {
        ig->parent = parent->parent;
    } else {
        ig->parent = parent;
    }
	
	if(ig->parent)
	{
		ig->parent->reference_count++;
	}

    if (parent && parent->abs_path_len > 0) {
        ag_asprintf(&(ig->abs_path), "%s/%s", parent->abs_path, dirname);
        ig->abs_path_len = parent->abs_path_len + 1 + dirname_len;
    } else if (dirname_len == 1 && dirname[0] == '.') {
        ig->abs_path = (char*) ag_malloc(sizeof(char));
        ig->abs_path[0] = '\0';
        ig->abs_path_len = 0;
    } else {
        ag_asprintf(&(ig->abs_path), "%s", dirname);
        ig->abs_path_len = dirname_len;
    }
    return ig;
}

void cleanup_ignore(ignores *ig) {
    if (ig == NULL) {
        return;
    }
	if(--ig->reference_count == 0)
	{
		free_strings(ig->extensions, ig->extensions_len);
		free_strings(ig->names, ig->names_len);
		free_strings(ig->slash_names, ig->slash_names_len);
		free_strings(ig->regexes, ig->regexes_len);
		free_strings(ig->slash_regexes, ig->slash_regexes_len);
		if (ig->abs_path) {
			free(ig->abs_path);
		}
		
		for(size_t i = 0; i < ig->regexes_len; ++i) delete ig->patterns[i];
		delete [] ig->patterns;

		for(size_t i = 0; i < ig->slash_regexes_len; ++i) delete ig->slash_patterns[i];
		delete [] ig->slash_patterns;

		free(ig);
	}
}

void add_ignore_pattern(ignores *ig, const char *pattern) {
    int i;
    int pattern_len;

    /* Strip off the leading dot so that matches are more likely. */
    if (strncmp(pattern, "./", 2) == 0) {
        pattern++;
    }

    /* Kill trailing whitespace */
    for (pattern_len = strlen(pattern); pattern_len > 0; pattern_len--) {
        if (!isspace(pattern[pattern_len - 1])) {
            break;
        }
    }

    if (pattern_len == 0) {
        log_debug("Pattern is empty. Not adding any ignores.");
        return;
    }

    char ***patterns_p;
    size_t *patterns_len;
    if (is_fnmatch(pattern)) {
        if (pattern[0] == '*' && pattern[1] == '.' && !(is_fnmatch(pattern + 2))) {
            patterns_p = &(ig->extensions);
            patterns_len = &(ig->extensions_len);
            pattern += 2;
        } else if (pattern[0] == '/') {
            patterns_p = &(ig->slash_regexes);
            patterns_len = &(ig->slash_regexes_len);
            pattern++;
            pattern_len--;
        } else {
            patterns_p = &(ig->regexes);
            patterns_len = &(ig->regexes_len);
        }
    } else {
        if (pattern[0] == '/') {
            patterns_p = &(ig->slash_names);
            patterns_len = &(ig->slash_names_len);
            pattern++;
            pattern_len--;
        } else {
            patterns_p = &(ig->names);
            patterns_len = &(ig->names_len);
        }
    }

    ++*patterns_len;

    char **patterns;

    /* a balanced binary tree is best for performance, but I'm lazy */
    *patterns_p = patterns = (char**) ag_realloc(*patterns_p, (*patterns_len) * sizeof(char *));
    /* TODO: de-dupe these patterns */
    for (i = *patterns_len - 1; i > 0; i--) {
        if (strcmp(pattern, patterns[i - 1]) > 0) {
            break;
        }
        patterns[i] = patterns[i - 1];
    }
    patterns[i] = ag_strndup(pattern, pattern_len);
    log_debug("added ignore pattern %s to %s", pattern,
              ig == root_ignores ? "root ignores" : ig->abs_path);
}

/* For loading git/hg ignore patterns */
void load_ignore_patterns(ignores *ig, const char *path) {
    FILE *fp = NULL;
    fp = fopen(path, "r");
    if (fp == NULL) {
        log_debug("Skipping ignore file %s: not readable", path);
        return;
    }
    log_debug("Loading ignore file %s.", path);

    char *line = NULL;
    ssize_t line_len = 0;
    size_t line_cap = 0;

    while ((line_len = getline(&line, &line_cap, fp)) > 0) {
        if (line_len == 0 || line[0] == '\n' || line[0] == '#') {
            continue;
        }
        if (line[line_len - 1] == '\n') {
            line[line_len - 1] = '\0'; /* kill the \n */
        }
        add_ignore_pattern(ig, line);
    }

    free(line);
    fclose(fp);
}

void load_svn_ignore_patterns(ignores *ig, const char *path) {
    FILE *fp = NULL;
    char *dir_prop_base;
    ag_asprintf(&dir_prop_base, "%s/%s", path, SVN_DIR_PROP_BASE);

    fp = fopen(dir_prop_base, "r");
    if (fp == NULL) {
        log_debug("Skipping svn ignore file %s", dir_prop_base);
        free(dir_prop_base);
        return;
    }

    char *entry = NULL;
    size_t entry_len = 0;
    char *key = (char*) ag_malloc(32); /* Sane start for max key length. */
    size_t key_len = 0;
    size_t bytes_read = 0;
    char *entry_line;
    size_t line_len;
    int matches;

	{
		while (fscanf(fp, "K %zu\n", &key_len) == 1) {
			key = (char*) ag_realloc(key, key_len + 1);
			bytes_read = fread(key, 1, key_len, fp);
			key[key_len] = '\0';
			matches = fscanf(fp, "\nV %zu\n", &entry_len);
			if (matches != 1) {
				log_debug("Unable to parse svnignore file %s: fscanf() got %i matches, expected 1.", dir_prop_base, matches);
				goto cleanup;
			}

			if (strncmp(SVN_PROP_IGNORE, key, bytes_read) != 0) {
				log_debug("key is %s, not %s. skipping %u bytes", key, SVN_PROP_IGNORE, entry_len);
				/* Not the key we care about. fseek and repeat */
				fseek(fp, entry_len + 1, SEEK_CUR); /* +1 to account for newline. yes I know this is hacky */
				continue;
			}
			/* Aww yeah. Time to ignore stuff */
			entry = (char*) ag_malloc(entry_len + 1);
			bytes_read = fread(entry, 1, entry_len, fp);
			entry[bytes_read] = '\0';
			log_debug("entry: %s", entry);
			break;
		}
		if (entry == NULL) {
			goto cleanup;
		}
		char *patterns = entry;
		size_t patterns_len = strlen(patterns);
		while (*patterns != '\0' && patterns < (entry + bytes_read)) {
			for (line_len = 0; line_len < patterns_len; line_len++) {
				if (patterns[line_len] == '\n') {
					break;
				}
			}
			if (line_len > 0) {
				entry_line = ag_strndup(patterns, line_len);
				add_ignore_pattern(ig, entry_line);
				free(entry_line);
			}
			patterns += line_len + 1;
			patterns_len -= line_len + 1;
		}
		free(entry);
	}
cleanup:
    free(dir_prop_base);
    free(key);
    fclose(fp);
}

static int ackmate_dir_match(const char *dir_name) {
    if (opts.ackmate_dir_pattern == NULL) {
        return 0;
    }
    /* we just care about the match, not where the matches are */
    //return pcre_exec(opts.ackmate_dir_filter, NULL, dir_name, strlen(dir_name), 0, 0, NULL, 0);
	return opts.ackmate_dir_pattern->HasPartialMatch(dir_name, strlen(dir_name));
}

/* This is the hottest code in Ag. 10-15% of all execution time is spent here */
static int path_ignore_search(const ignores *ig, const char *path, const char *filename) {
//    char *temp;
    size_t i;
    int match_pos;

    match_pos = binary_search(filename, ig->names, 0, ig->names_len);
    if (match_pos >= 0) {
        log_debug("file %s ignored because name matches static pattern %s", filename, ig->names[match_pos]);
        return 1;
    }

	if(path[0] == '.') ++path;
	size_t path_len = strlen(path);
	char temp[path_len+2048+2];
	memcpy(temp, path, path_len);
	temp[path_len] = '/';
	strcpy(temp+path_len+1, filename);

    if (strncmp(temp, ig->abs_path, ig->abs_path_len) == 0) {
        char *slash_filename = temp + ig->abs_path_len;
        if (slash_filename[0] == '/') {
            slash_filename++;
        }
        match_pos = binary_search(slash_filename, ig->names, 0, ig->names_len);
        if (match_pos >= 0) {
            log_debug("file %s ignored because name matches static pattern %s", temp, ig->names[match_pos]);
            return 1;
        }

        match_pos = binary_search(slash_filename, ig->slash_names, 0, ig->slash_names_len);
        if (match_pos >= 0) {
            log_debug("file %s ignored because name matches slash static pattern %s", slash_filename, ig->slash_names[match_pos]);
            return 1;
        }

		if(ig->slash_regexes_len) {
			size_t slash_filename_len = strlen(slash_filename);
			for (i = 0; i < ig->slash_regexes_len; i++) {
				if (ig->slash_patterns[i]->HasFullMatch(slash_filename, slash_filename_len)) {
					log_debug("file %s ignored because name matches slash regex pattern %s", slash_filename, ig->slash_regexes[i]);
					return 1;
				}
//				if (fnmatch(ig->slash_regexes[i], slash_filename, fnmatch_flags) == 0) {
//					log_debug("file %s ignored because name matches slash regex pattern %s", slash_filename, ig->slash_regexes[i]);
//					return 1;
//				}
//				log_debug("pattern %s doesn't match slash file %s", ig->slash_regexes[i], slash_filename);
			}
		}
    }

	if(ig->regexes_len) {
		size_t filename_len = strlen(filename);
		for (i = 0; i < ig->regexes_len; i++) {
			if (ig->patterns[i]->HasFullMatch(filename, filename_len)) {
				log_debug("file %s ignored because name matches regex pattern %s", filename, ig->regexes[i]);
				return 1;
			}
//			if (fnmatch(ig->regexes[i], filename, fnmatch_flags) == 0) {
//				log_debug("file %s ignored because name matches regex pattern %s", filename, ig->regexes[i]);
//				return 1;
//			}
//			log_debug("pattern %s doesn't match file %s", ig->regexes[i], filename);
		}
	}

    return ackmate_dir_match(temp);
}

/* This function is REALLY HOT. It gets called for every file */
int filename_filter(const char *path, const struct dirent *dir, void *baton) {
    const char *filename = dir->d_name;
    if (!opts.search_hidden_files && filename[0] == '.') {
        return 0;
    }

    size_t i;

    if (!opts.follow_symlinks && is_symlink(path, dir)) {
        log_debug("File %s ignored becaused it's a symlink", dir->d_name);
        return 0;
    }

    if (is_named_pipe(path, dir)) {
        log_debug("%s ignored because it's a named pipe", path);
        return 0;
    }

    if (opts.search_all_files && !opts.path_to_agignore) {
        return 1;
    }

    scandir_baton_t *scandir_baton = (scandir_baton_t *)baton;
    const char *base_path = scandir_baton->base_path;
    const size_t base_path_len = scandir_baton->base_path_len;
    const char *path_start = path;

    for (i = 0; base_path[i] == path[i] && i < base_path_len; i++) {
        /* base_path always ends with "/\0" while path doesn't, so this is safe */
        path_start = path + i + 2;
    }
    log_debug("path_start %s filename %s", path_start, filename);

    const char *extension = strchr(filename, '.');
    if (extension) {
        if (extension[1]) {
            // The dot is not the last character, extension starts at the next one
            ++extension;
        } else {
            // No extension
            extension = NULL;
        }
    }

/* TODO: don't call strlen on filename every time we call filename_filter() */
#ifdef HAVE_DIRENT_DNAMLEN
    size_t filename_len = dir->d_namlen;
#else
    size_t filename_len = strlen(filename);
#endif
    const ignores *ig = scandir_baton->ig;

    while (ig != NULL) {
		if (filename[0] == '.' && filename[1] == '/') { // strncmp(filename, "./", 2) == 0) {
            filename++;
            filename_len--;
        }

        if (extension) {
            int match_pos = binary_search(extension, ig->extensions, 0, ig->extensions_len);
            if (match_pos >= 0) {
                log_debug("file %s ignored because name matches extension %s", filename, ig->extensions[match_pos]);
                return 0;
            }
        }

        if (path_ignore_search(ig, path_start, filename)) {
            return 0;
        }

        if (is_directory(path, dir) && filename[filename_len - 1] != '/') {
            char *temp;
            ag_asprintf(&temp, "%s/", filename);
            int rv = path_ignore_search(ig, path_start, temp);
            free(temp);
            if (rv) {
                return 0;
            }
        }
        ig = ig->parent;
    }

    log_debug("%s not ignored", filename);
    return 1;
}

void build_patterns(ignores *ig)
{
	if(ig->regexes_len)
	{
		ig->patterns = new Javelin::Pattern*[ig->regexes_len];
		for(size_t i = 0; i < ig->regexes_len; ++i)
		{
			try {
				ig->patterns[i] = new Javelin::Pattern(Javelin::String{ig->regexes[i]}, Javelin::Pattern::GLOB_SYNTAX | Javelin::Pattern::ANCHORED);
			} catch (...) {
				ig->patterns[i] = new Javelin::Pattern(Javelin::String::EMPTY_STRING, Javelin::Pattern::GLOB_SYNTAX | Javelin::Pattern::ANCHORED);
			}
		}
	}
	
	if(ig->slash_regexes_len)
	{
		ig->slash_patterns = new Javelin::Pattern*[ig->slash_regexes_len];
		for(size_t i = 0; i < ig->slash_regexes_len; ++i)
		{
			try {
				ig->slash_patterns[i] = new Javelin::Pattern(Javelin::String{ig->slash_regexes[i]}, Javelin::Pattern::GLOB_SYNTAX | Javelin::Pattern::ANCHORED);
			} catch(...) {
				ig->slash_patterns[i] = new Javelin::Pattern(Javelin::String::EMPTY_STRING, Javelin::Pattern::GLOB_SYNTAX | Javelin::Pattern::ANCHORED);
			}
		}
	}	
}
