typedef unsigned int uint;

typedef struct Term Term;

typedef struct Key Key;
struct Key {
	int kind;
	long value;
};

enum {
	Keynone,
	Keyrune,
	Keyesc,
	Keybackspace,
	Keyenter,
	Keyup,
	Keydown,
	Keyleft,
	Keyright,
	Keyhome,
	Keyend,
	Keypgup,
	Keypgdown,
};

struct Term {
	int fdin;
	int fdout;
	int row;
	int col;
};

void terminit(Term *t);
void termrestore(void);
void termgetwinsz(Term *t);
int termresized(void);

void termclear(Term *t);
void termmoveto(Term *t, int r, int c);
void termflush(Term *t);

int keyread(Term *t, Key *k);

void die(const char *fmt, ...);
