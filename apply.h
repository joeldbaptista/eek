#ifndef APPLY_H
#define APPLY_H

/*
 * :apply registry + executor
 *
 * Users register apply functions by defining applytab[] in config.h.
 */

struct Eek;

typedef int (*ApplyFn)(const char *in, long inlen, int argc, char **argv,
			char **out, long *outlen);

typedef struct Apply Apply;
struct Apply {
	const char *name;
	ApplyFn fn;
};

/*
 * applyexec executes ":apply ..." with argline being everything after the
 * "apply" token.
 */
int applyexec(struct Eek *e, const char *argline);

#endif /* APPLY_H */
