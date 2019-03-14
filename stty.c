#define _XOPEN_SOURCE 700
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define ASIZEOF(_a) (sizeof(_a) / sizeof(_a[0]))

const int DELETE = 127;

static struct {
	speed_t macro;
	int value;
} speeds[] = {
	{ B0, 0 },		{ B50, 50 },		{ B75, 75 },
	{ B110, 110 },		{ B134, 134 },		{ B150, 150 },
	{ B200, 200 },		{ B300, 300 },		{ B600, 600 },
	{ B1200, 1200 },	{ B1800, 1800 },	{ B2400, 2400 },
	{ B4800, 4800 },	{ B9600, 9600 },	{ B19200, 19200 },
	{ B38400, 38400 },
};

enum FLAG { INPUT, OUTPUT, CONTROL, LOCAL };
enum DISPOSITION { IGNORE, SET, CLEAR } disposition;

static struct {
	char *string;
	enum FLAG flag;
	tcflag_t mask;
	enum DISPOSITION disposition;
} settings[] = {
	{ "parenb",	CONTROL,	PARENB,		IGNORE },
	{ "parodd",	CONTROL,	PARODD,		IGNORE },
	{ "hupcl",	CONTROL,	HUPCL,		IGNORE }, /* alias "hup" */
	{ "cstopb",	CONTROL,	CSTOPB,		IGNORE },
	{ "cread",	CONTROL,	CREAD,		IGNORE },
	{ "clocal",	CONTROL,	CLOCAL,		IGNORE },
	{ "ignbrk",	INPUT,		IGNBRK,		IGNORE },
	{ "brkint",	INPUT,		BRKINT,		IGNORE },
	{ "ignpar",	INPUT,		IGNPAR,		IGNORE },
	{ "parmrk",	INPUT,		PARMRK,		IGNORE },
	{ "inpck",	INPUT,		INPCK,		IGNORE },
	{ "inlcr",	INPUT,		INLCR,		IGNORE },
	{ "igncr",	INPUT,		IGNCR,		IGNORE },
	{ "icrnl",	INPUT,		ICRNL,		IGNORE },
	{ "ixon",	INPUT,		IXON,		IGNORE },
	{ "ixany",	INPUT,		IXANY,		IGNORE },
	{ "ixoff",	INPUT,		IXOFF,		IGNORE },
	{ "opost",	OUTPUT,		OPOST,		IGNORE },
	{ "onlcr",	OUTPUT,		ONLCR,		IGNORE },
	{ "ocrnl",	OUTPUT,		OCRNL,		IGNORE },
	{ "onocr",	OUTPUT,		ONOCR,		IGNORE },
	{ "onlret",	OUTPUT,		ONLRET,		IGNORE },
	{ "ofill",	OUTPUT,		OFILL,		IGNORE },
	{ "ofdel",	OUTPUT,		OFDEL,		IGNORE },
	{ "isig",	LOCAL,		ISIG,		IGNORE },
	{ "icanon",	LOCAL,		ICANON,		IGNORE },
	{ "iexten",	LOCAL,		IEXTEN,		IGNORE },
	{ "echo",	LOCAL,		ECHO,		IGNORE },
	{ "echoe",	LOCAL,		ECHOE,		IGNORE },
	{ "echok",	LOCAL,		ECHOK,		IGNORE },
	{ "echonl",	LOCAL,		ECHONL,		IGNORE },
	{ "noflsh",	LOCAL,		NOFLSH,		IGNORE },
	{ "tostop",	LOCAL,		TOSTOP,		IGNORE },
};

static struct {
	tcflag_t mask;
	enum FLAG flag;
	enum DISPOSITION disposition;
	struct {
		char *string;
		tcflag_t value;
	} options[4];
} fields[] = {
	{
		CSIZE,	CONTROL,	IGNORE,
		{
			{ "cs5", CS5 },
			{ "cs6", CS6 },
			{ "cs7", CS7 },
			{ "cs8", CS8 },
		}
	},

	{
		CRDLY,	OUTPUT,		IGNORE,
		{
			{ "cr0", CR0 },
			{ "cr1", CR1 },
			{ "cr2", CR2 },
			{ "cr3", CR3 },
		}
	},

	{
		NLDLY,	OUTPUT,		IGNORE,
		{
			{ "nl0", NL0 },
			{ "nl1", NL1 },
		}
	},

	{
		TABDLY,	OUTPUT,		IGNORE,
		{
			{ "tab0", TAB0 },	/* alias "tabs" */
			{ "tab1", TAB1 },
			{ "tab2", TAB2 },
			{ "tab3", TAB3 },	/* alias "-tabs" */
		}
	},

	{
		BSDLY,	OUTPUT,		IGNORE,
		{
			{ "bs0", BS0 },
			{ "bs1", BS1 },
		}
	},

	{
		FFDLY,	OUTPUT,		IGNORE,
		{
			{ "ff0", FF0 },
			{ "ff1", FF1 },
		}
	},

	{
		VTDLY,	OUTPUT,		IGNORE,
		{
			{ "vt0", VT0 },
			{ "vt1", VT1 },
		}
	},
};

static char escapes[] = "-ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_";

static struct key {
	char *string;
	enum { CANONICAL, NONCANONICAL, BOTH } mode;
	int n;
	int newvalue;
} keys[] = {
	{ "eof",	CANONICAL,	VEOF,	EOF },
	{ "eol",	CANONICAL,	VEOL,	EOF },
	{ "erase",	CANONICAL,	VERASE,	EOF },
	{ "intr",	BOTH,		VINTR,	EOF },
	{ "kill",	CANONICAL,	VKILL,	EOF },
	{ "quit",	BOTH,		VQUIT,	EOF },
	{ "susp",	BOTH,		VSUSP,	EOF },
	{ "start",	BOTH,		VSTART,	EOF },
	{ "stop",	BOTH,		VSTOP,	EOF },
	{ "min",	NONCANONICAL,	VMIN,	EOF },
	{ "time",	NONCANONICAL,	VTIME,	EOF },
};

static int speed_to_baud(speed_t s)
{
	for (size_t i = 0; i < ASIZEOF(speeds); i++) {
		if (speeds[i].macro == s) {
			return speeds[i].value;
		}
	}
	return 0;
}


static char *cc_to_str(cc_t c, int quote)
{
	static char string[4] = { 0 };
	if (isprint(c)) {
		if (quote && strchr("|&;<>(){}*\\ ", c)) {
			string[0] = '\\';
			string[1] = c;
			string[2] = '\0';
		} else {
			string[0] = c;
			string[1] = '\0';
		}
		return string;
	}

	if ((c != _POSIX_VDISABLE) && (c < sizeof(escapes))) {
		string[0] = '^';
		string[1] = escapes[c];
		string[2] = '\0';
		if (quote && string[1] == '\\') {
			string[2] = '\\';
			string[3] = '\0';
		}
		return string;
	}

	if (c == DELETE) {
		return "^?";
	}

	return quote ? "^-" : "<undef>";
}

static size_t printkey(struct key k, int canon, int format, struct termios *t)
{
	char *eq = format == 'a' ? " = " : " ";
	size_t ret = 0;

	if (k.mode == BOTH || (canon && k.mode == CANONICAL)) {
		ret = printf("%s%s%s", k.string, eq, cc_to_str(t->c_cc[k.n], format == 'g'));
	} else if ((!canon) && k.mode == NONCANONICAL) {
		ret = printf("%s%s%d", k.string, eq, t->c_cc[k.n]);
	}

	if (ret != 0 && format == 'a') {
		putchar(';');
		ret++;
	}

	return ret;
}

static int stty_print(int format)
{
	char *baud = format == 'a' ? " baud;" : " ";
	char eol = format == 'a' ? '\n' : ' ';

	struct termios t;
	if (tcgetattr(STDIN_FILENO, &t) != 0) {
		perror("stty");
		return 1;
	}

	speed_t ispeed = cfgetispeed(&t);
	speed_t ospeed = cfgetospeed(&t);

	/* speed */
	if (ispeed == ospeed) {
		printf("speed %d%s%c", speed_to_baud(ispeed), baud, eol);
	} else {
		printf("ispeed %d%s ospeed %d%s%c", speed_to_baud(ispeed),
			baud, speed_to_baud(ospeed), baud, eol);
	}

	/* control characters */
	size_t column = 0;
	for (size_t i = 0; i < ASIZEOF(keys); i++) {
		size_t c = printkey(keys[i], t.c_lflag & ICANON, format, &t);

		if (c == 0) {
			continue;
		}

		column += c;
		if (column >= 72) {
			putchar(eol);
			column = 0;
		} else {
			putchar(' ');
			column++;
		}
	}

	if (column != 0 && format == 'a') {
		putchar(eol);
		column = 0;
	}

	/* single bit settings */
	for (size_t i = 0; i < ASIZEOF(settings); i++) {
		if ((settings[i].mask &
			(settings[i].flag == INPUT ? t.c_iflag :
			 settings[i].flag == OUTPUT ? t.c_oflag :
			 settings[i].flag == CONTROL ? t.c_cflag :
			 settings[i].flag == LOCAL ? t.c_lflag : 0)) == 0) {
			putchar('-');
			column++;
		}
		column += printf("%s", settings[i].string);

		if (column >= 72) {
			putchar(eol);
			column = 0;
		} else {
			putchar(' ');
			column++;
		}
	}

	/* multiple option fields */
	for (size_t i = 0; i < ASIZEOF(fields); i++) {
		tcflag_t value = fields[i].mask &
			(fields[i].flag == INPUT ? t.c_iflag :
			 fields[i].flag == OUTPUT ? t.c_oflag :
			 fields[i].flag == CONTROL ? t.c_cflag :
			 fields[i].flag == LOCAL ? t.c_lflag : 0);
		for (size_t j = 0; j < ASIZEOF(fields[i].options); j++) {
			if (value == fields[i].options[j].value && fields[i].options[j].string != NULL) {
				column += printf("%s", fields[i].options[j].string);
			}
		}

		if (column > 72) {
			putchar(eol);
			column = 0;
		} else {
			putchar(' ');
			column++;
		}
	}

	if (column != 0) {
		putchar('\n');
	}

	return 0;
}

int main(int argc, char *argv[])
{
	char format = '\0';
	int c;

	opterr = 0;
	while ((c = getopt(argc, argv, "ag")) != -1) {
		switch (c) {
		case 'a':
		case 'g':
			format = c;
			break;

		default:
			/* operands can start with - */
			break;
		}

		/* stop parsing options if we hit a non-option */
		if (c == '?') {
			break;
		}
	}

	if (optind >= argc) {
		return stty_print(format ? format : 'a');
	}

	if (format != '\0') {
		fprintf(stderr, "stty: use -a or -g *OR* specify options\n");
		return 1;
	}

	do {
		int disable = argv[optind][0] == '-' ? 1 : 0;
		char *opt = &(argv[optind][disable]);

		for (size_t i = 0; i < ASIZEOF(settings); i++) {
			if (!strcmp(opt, settings[i].string)) {
				settings[i].disposition = disable ? CLEAR : SET;
				goto loop;
			}
		}

		#if 0	/* stop this crashing */
		for (size_t i = 0; i < ASIZEOF(fields); i++) {
			for (size_t j = 0; fields[i].options[j].string != NULL; j++) {
				if (!strcmp(opt, fields[i].options[j].string)) {
					/* set this */
					goto loop;
				}
			}
		}
		#endif

		/* do keys */

		if (!strcmp(opt, "hup")) {
			/* disable ? -hupcl : hupcl */
		} else if (!strcmp(opt, "tabs")) {
			/* disable ? tab3 : tab0 */

		} else if (!strcmp(opt, "evenp") || !strcmp(opt, "parity")) {
			/* disable ? -parenb cs8 : parenb cs7 -parodd */
		} else if (!strcmp(opt, "oddp")) {
			/* disable ? -parenb cs8 : parenb cs7 parodd */

		} else if (!strcmp(opt, "raw")) {
			/* disable ? "cooked" : cs8 erase ^- kill ^- intr ^- quit ^- eof ^- eol ^- -post -inpck */
		} else if (!strcmp(opt, "cooked")) {
			/* disable ? "raw" : "unspecified" */

		} else if (!strcmp(opt, "nl")) {
			/* disable ? icrnl -inlcr -igncr : icrnl */

		} else if (!strcmp(argv[optind], "ek")) {
			/* erase ^? kill ^U */

		} else if (!strcmp(argv[optind], "sane")) {
			/* unspecified defaults */
		}

		printf("unknown setting '%s'\n", opt);
		return 1;

	loop:	;
	} while (argv[++optind] != NULL);

	return 0;
}
