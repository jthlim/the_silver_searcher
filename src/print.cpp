#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "ignore.h"
#include "log.h"
#include "options.h"
#include "print.h"
#include "util.h"
#ifdef _WIN32
#define fprintf(...) fprintf_w32(__VA_ARGS__)
#endif

int first_file_match = 1;

const char color_reset[] = "\033[m\033[K";
const char color_reset_with_newline[] = "\033[m\033[K\n";
const char truncate_marker[] = " [...]";

void print_path(const char *path, const char sep) {
    if (opts.print_path_resolved == PATH_PRINT_NOTHING && !opts.vimgrep) {
        return;
    }
    path = normalize_path(path);

    if (opts.ackmate) {
        fprintf(out_fd, ":%s%c", path, sep);
    } else if (opts.vimgrep) {
        fprintf(out_fd, "%s%c", path, sep);
    } else {
        if (opts.color) {
            fprintf(out_fd, "%s%s%s%c", opts.color_path, path, color_reset, sep);
        } else {
            fprintf(out_fd, "%s%c", path, sep);
        }
    }
}

void print_path_count(const char *path, const char sep, const size_t count) {
    if (*path) {
        print_path(path, ':');
    }
    if (opts.color) {
        fprintf(out_fd, "%s%lu%s%c", opts.color_line_number, (unsigned long)count, color_reset, sep);
    } else {
        fprintf(out_fd, "%lu%c", (unsigned long)count, sep);
    }
}

void print_line(const char *buf, size_t buf_pos, size_t prev_line_offset) {
    size_t write_chars = buf_pos - prev_line_offset + 1;
    if (opts.width > 0 && opts.width < write_chars) {
        write_chars = opts.width;
    }

    fwrite(buf + prev_line_offset, 1, write_chars, out_fd);
}

void print_binary_file_matches(const char *path) {
    path = normalize_path(path);
    print_file_separator();
    fprintf(out_fd, "Binary file %s matches.\n", path);
}

void print_file_matches(const char *path, const char *buf, const size_t buf_len, const match_t matches[], const size_t matches_len) {
    size_t line = 1;
    const char **context_prev_lines = NULL;
    size_t prev_line = 0;
    size_t last_prev_line = 0;
    size_t prev_line_offset = 0;
    size_t cur_match = 0;
    size_t lines_since_last_match = opts.before + opts.after + 2;
    ssize_t lines_to_print = 0;
    size_t last_printed_match = 0;
    char sep = '-';
    size_t i, j;
    bool in_a_match = false;
    bool printing_a_match = false;

    if (opts.ackmate || opts.vimgrep) {
        sep = ':';
    }

    print_file_separator();
    if (opts.print_path_resolved == PATH_PRINT_TOP) {
        print_path(path, opts.path_sep);
    }

    context_prev_lines = (const char**) ag_calloc(sizeof(char *), (opts.before + 1));
	context_prev_lines[0] = buf;

	size_t search_end = matches[0].start;
	i = 0;
	while(true) {
		// Ugly and not ideal, but better than searching one byte at a time..
		// This whole routine needs to be rewritten
		const char* next_relevant = (const char*) memchr(buf+i, '\n', search_end-i);
		if(next_relevant) i = next_relevant - buf;
		else i = search_end;
		
		if JLIKELY(cur_match < matches_len) {
			if JUNLIKELY(i == matches[cur_match].start) {
			do_next_match:
				in_a_match = TRUE;
				search_end = matches[cur_match].end;
				/* We found the start of a match */
				if (opts.context && cur_match > 0 && lines_since_last_match > (opts.before + opts.after + 1)) {
					fputs("--\n", out_fd);
				}
				
				if (lines_since_last_match > 0 && opts.before > 0) {
					/* TODO: better, but still needs work */
					/* print the previous line(s) */
					lines_to_print = lines_since_last_match - (opts.after + 1);
					if (lines_to_print < 0) {
						lines_to_print = 0;
					} else if ((size_t)lines_to_print > opts.before) {
						lines_to_print = opts.before;
					}
					
					for (j = lines_to_print; j > 0; --j) {
						prev_line = (last_prev_line + opts.before + 1 - j) % (opts.before + 1);
						if (context_prev_lines[prev_line] != NULL) {
							if (opts.print_path_resolved == PATH_PRINT_EACH_LINE) {
								print_path(path, ':');
							}
							print_line_number(line - j, sep);
							size_t next_line = prev_line + 1;
							if(next_line == opts.before+1) next_line = 0;
							fwrite(context_prev_lines[prev_line], 1, context_prev_lines[next_line]-context_prev_lines[prev_line], out_fd);
						}
					}
				}
				lines_since_last_match = 0;
			}
			
			if JUNLIKELY(i == matches[cur_match].end) {
				/* We found the end of a match. */
				cur_match++;
				in_a_match = FALSE;
				if(cur_match < matches_len) {
					search_end = matches[cur_match].start;
					if(search_end == i) goto do_next_match;
				}
				else search_end = buf_len;
			}
		}

        /* We found the end of a line. */
        if JLIKELY(i == buf_len || buf[i] == '\n') {
			if(opts.before > 0) {
				if(last_prev_line == opts.before) last_prev_line = 0;
				else ++last_prev_line;
				context_prev_lines[last_prev_line] = &buf[i]+1;
			}

			if (lines_since_last_match <= opts.after) {
				if(lines_since_last_match == 0)
				{
					if (opts.print_path_resolved == PATH_PRINT_EACH_LINE && !opts.search_stream) {
						print_path(path, ':');
					}
					if (opts.ackmate) {
						/* print headers for ackmate to parse */
						print_line_number(line, ';');
						for (; last_printed_match < cur_match; last_printed_match++) {
							/* Don't print negative offsets. This isn't quite right, but not many people use --ackmate */
							long start = (long)(matches[last_printed_match].start - prev_line_offset);
							if (start < 0) {
								start = 0;
							}
							fprintf(out_fd, "%li %li",
									start,
									(long)(matches[last_printed_match].end - matches[last_printed_match].start));
							last_printed_match == cur_match - 1 ? fputc(':', out_fd) : fputc(',', out_fd);
						}
						print_line(buf, i, prev_line_offset);
					} else if (opts.vimgrep) {
						for (; last_printed_match < cur_match; last_printed_match++) {
							size_t column = matches[last_printed_match].start - prev_line_offset + 1;
							fprintf(out_fd, "%s:%zu:%zu:", path, line, column);
							print_line(buf, i, prev_line_offset);
						}
					} else {
						print_line_number(line, ':');
						int printed_match = FALSE;
						if (opts.column) {
							print_column_number(matches, last_printed_match, prev_line_offset, ':');
						}

						if (printing_a_match && opts.color) {
							fputs(opts.color_match, out_fd);
						}
						j = prev_line_offset;
						
						size_t span_width_end = i;
						if(opts.width != 0) {
							if(prev_line_offset + opts.width < span_width_end) {
								span_width_end = prev_line_offset + opts.width;
							}
						}
						
						for(;;) {
							/* close highlight of match term */
							if (last_printed_match < matches_len && j == matches[last_printed_match].end) {
								if (opts.color) {
									fwrite(color_reset, 1, sizeof(color_reset)-1, out_fd);
								}
								printing_a_match = FALSE;
								last_printed_match++;
								printed_match = TRUE;
								if (opts.only_matching) {
									fputc('\n', out_fd);
								}
							}
							/* skip remaining characters if truncation width exceeded, needs to be done
							 * before highlight opening */
							if (j < i && opts.width > 0 && j - prev_line_offset >= opts.width) {
								fwrite(truncate_marker, 1, sizeof(truncate_marker)-1, out_fd);

								if (printing_a_match && opts.color) {
									fwrite(color_reset_with_newline, 1, sizeof(color_reset_with_newline)-1, out_fd);
								} else {
									fputc('\n', out_fd);
								}
								
								last_printed_match = cur_match;
								printing_a_match = in_a_match;
								break;
							}
							/* open highlight of match term */
							if (last_printed_match < matches_len && j == matches[last_printed_match].start) {
								if (opts.only_matching && printed_match) {
									if (opts.print_path_resolved == PATH_PRINT_EACH_LINE) {
										print_path(path, ':');
									}
									print_line_number(line, ':');
									if (opts.column) {
										print_column_number(matches, last_printed_match, prev_line_offset, ':');
									}
								}
								if (opts.color) {
									fputs(opts.color_match, out_fd);
								}
								printing_a_match = TRUE;
							}

							/* if only_matching is set, print only matches and newlines */
							size_t span_end = span_width_end;
							
							if(last_printed_match < matches_len) {
								if(matches[last_printed_match][printing_a_match] < span_end) {
									span_end = matches[last_printed_match][printing_a_match];
								}
							}

							if(j == span_end) {
								if (printing_a_match && opts.color) {
									fwrite(color_reset_with_newline, 1, sizeof(color_reset_with_newline)-1, out_fd);
								} else {
									fputc('\n', out_fd);
								}
								break;
							}
							
							if (!opts.only_matching || printing_a_match) {
								fwrite(buf+j, 1, span_end-j, out_fd);
							}
							j = span_end;
						}
					}
				} else {
					/* print context after matching line */
					if (opts.print_path_resolved == PATH_PRINT_EACH_LINE) {
						print_path(path, ':');
					}
					print_line_number(line, sep);

					fwrite(buf+prev_line_offset, 1, i-prev_line_offset, out_fd);
					fputc('\n', out_fd);
				}
			}
			
			if(i == buf_len) break;
			
			++i; /* skip the newline */
            prev_line_offset = i;
            line++;
            if (!in_a_match) {
                lines_since_last_match++;
				if(cur_match >= matches_len && lines_since_last_match > opts.after) break;
            }
        }
    }

cleanup:
    free(context_prev_lines);
}

void print_line_number(size_t line, const char sep) {
    if (!opts.print_line_numbers) {
        return;
    }
    if (opts.search_stream && opts.stream_line_num) {
        line = opts.stream_line_num;
    }
    if (opts.color) {
        fprintf(out_fd, "%s%lu%s%c", opts.color_line_number, (unsigned long)line, color_reset, sep);
    } else {
        fprintf(out_fd, "%lu%c", (unsigned long)line, sep);
    }
}

void print_column_number(const match_t matches[], size_t last_printed_match,
                         size_t prev_line_offset, const char sep) {
    size_t column = 0;
    if (prev_line_offset <= matches[last_printed_match].start) {
        column = (matches[last_printed_match].start - prev_line_offset) + 1;
    }
    fprintf(out_fd, "%lu%c", (unsigned long)column, sep);
}

void print_file_separator(void) {
    if (first_file_match == 0 && opts.print_break) {
        fputc('\n', out_fd);
    }
    first_file_match = 0;
}

const char *normalize_path(const char *path) {
    if (strlen(path) < 3) {
        return path;
    }
    if (path[0] == '.' && path[1] == '/') {
        return path + 2;
    }
    if (path[0] == '/' && path[1] == '/') {
        return path + 1;
    }
    return path;
}
