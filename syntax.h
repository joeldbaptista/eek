/* syntax highlighting rules (included by eek.c) */

#define C_TYPES(X) \
	X("void") X("char") X("short") X("int") X("long") X("float") X("double") \
	X("signed") X("unsigned") \
	X("size_t") X("ssize_t") X("ptrdiff_t")

#define C_KEYWORDS(X) \
	X("if") X("else") X("for") X("while") X("do") X("switch") X("case") X("default") \
	X("break") X("continue") X("return") X("goto") \
	X("sizeof") \
	X("static") X("extern") X("const") X("volatile") X("register") X("inline") \
	X("typedef") X("struct") X("union") X("enum") \
	X("_Bool") X("_Complex") X("_Imaginary")

#define C_SPECIALS(X) \
	X("stdin") X("stdout") X("stderr")

static int
synlangfromfname(const char *fname)
{
	const char *dot;

	if (fname == nil)
		return Synnone;
	dot = strrchr(fname, '.');
	if (dot == nil)
		return Synnone;
	if (strcmp(dot, ".c") == 0)
		return Sync;
	if (strcmp(dot, ".h") == 0)
		return Sync;
	return Synnone;
}

static int
synwordkind_lang(int lang, const char *s, long n)
{
	long i;

	if (lang != Sync)
		return Hlnone;

	/* C: types */
	{
		static const char *ty[] = {
#define X(s) s,
			C_TYPES(X)
#undef X
		};
		for (i = 0; i < (long)(sizeof ty / sizeof ty[0]); i++)
			if ((long)strlen(ty[i]) == n && memcmp(s, ty[i], (size_t)n) == 0)
				return Hltype;
	}

	/* C: keywords */
	{
		static const char *kw[] = {
#define X(s) s,
			C_KEYWORDS(X)
#undef X
		};
		for (i = 0; i < (long)(sizeof kw / sizeof kw[0]); i++)
			if ((long)strlen(kw[i]) == n && memcmp(s, kw[i], (size_t)n) == 0)
				return Hlkeyword;
	}

	/* C: special identifiers */
	{
		static const char *sp[] = {
#define X(s) s,
			C_SPECIALS(X)
#undef X
		};
		for (i = 0; i < (long)(sizeof sp / sizeof sp[0]); i++)
			if ((long)strlen(sp[i]) == n && memcmp(s, sp[i], (size_t)n) == 0)
				return Hlspecial;
	}

	return Hlnone;
}
