/* Should be included after dryopt.h. No protections for double-includes
   here -- this shouldn't be used outside of internal use */

extern void err_(const char *restrict const fmt, ...) __attribute__((cold, format(__printf__, 1, 2)));
extern char const *enum_type2str(enum dryarg_tag) __attribute__((__const__, returns_nonnull));
extern struct found_longopt { size_t opti; char * arg; } find_longopt(char*, struct dryopt[], size_t);
extern void write_optarg(struct dryopt const *restrict, union dryoptarg);
extern char *parse_optarg(struct dryopt const *restrict, char *restrict, union dryoptarg *restrict) __attribute__((nonnull));
extern char *eat_space(char const*) __attribute__((nonnull, pure, returns_nonnull));
