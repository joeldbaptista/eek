typedef struct Line Line;
typedef struct Buf Buf;

struct Line {
	char *s;
	long n;
	long cap;
};

struct Buf {
	Line *line;
	long nline;
	long cap;
};

void bufinit(Buf *b);
void buffree(Buf *b);

int bufcopy(Buf *dst, Buf *src);

int bufload(Buf *b, const char *path);
int bufsave(Buf *b, const char *path);

Line *bufgetline(Buf *b, long i);

int bufinsertline(Buf *b, long at, const char *s, long n);
int bufdelline(Buf *b, long at);

int lineinsert(Line *l, long at, const char *s, long n);
int linedelrange(Line *l, long at, long n);
