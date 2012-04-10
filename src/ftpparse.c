/* ftpparse - http://cr.yp.to/ftpparse.html */

struct ftpparse {
	char *name; /* not necessarily 0-terminated */
	int namelen;
	int flagtrycwd; /* 0 if cwd is definitely pointless, 1 otherwise */
	int flagtryretr; /* 0 if retr is definitely pointless, 1 otherwise */
	int sizetype;
	long size; /* number of octets */
	int mtimetype;
	time_t mtime; /* modification time */
	int idtype;
	char *id; /* not necessarily 0-terminated */
	int idlen;
} ;

#define FTPPARSE_SIZE_UNKNOWN 0
#define FTPPARSE_SIZE_BINARY 1 /* size is the number of octets in TYPE I */
#define FTPPARSE_SIZE_ASCII 2 /* size is the number of octets in TYPE A */
#define FTPPARSE_MTIME_UNKNOWN 0
#define FTPPARSE_MTIME_LOCAL 1 /* time is correct */
#define FTPPARSE_MTIME_REMOTEMINUTE 2 /* time zone and secs are unknown */
#define FTPPARSE_MTIME_REMOTEDAY 3 /* time zone and time of day are unknown */
#define FTPPARSE_ID_UNKNOWN 0
#define FTPPARSE_ID_FULL 1 /* unique identifier for files on this FTP server */

static long totai(long year, long month, long mday);
static int flagneedbase = 1;
static time_t base;
static long now;
static int flagneedcurrentyear = 1;
static long currentyear;
static void initbase(void);
static void initnow(void);
static long guesstai(long month, long mday);
static int check(char *buf, char *monthname);
static char *months[12] = { "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec" };
static int getmonth(char *buf, int len);
static long getlong(char *buf, int len);
int ftp_parse(struct ftpparse *fp, char *buf, int len);

static long totai(long year, long month, long mday)
{
	long result;
	if (month >= 2)
		month -= 2;
	else {
		month += 10;
		--year;
	}
	result = (mday - 1) * 10 + 5 + 306 * month;
	result /= 10;
	if (result == 365) {
		year -= 3;
		result = 1460;
	} else
		result += 365 * (year % 4);
	year /= 4;
	result += 1461 * (year % 25);
	year /= 25;
	if (result == 36524) {
		year -= 3;
		result = 146096;
	} else {
		result += 36524 * (year % 4);
	}
	year /= 4;
	result += 146097 * (year - 5);
	result += 11017;
	return result * 86400;
}

static void initbase(void)
{
	struct tm *t;
	if (!flagneedbase)
		return;

	base = 0;
	t = gmtime(&base);
	base =
		-(totai(t->tm_year + 1900, t->tm_mon, t->tm_mday) +
		  t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec);
	flagneedbase = 0;
}

static void initnow(void)
{
	long day;
	long year;

	initbase();
	now = time((time_t *) 0) - base;

	if (flagneedcurrentyear) {
		day = now / 86400;
		if ((now % 86400) < 0)
			--day;
		day -= 11017;
		year = 5 + day / 146097;
		day = day % 146097;
		if (day < 0) {
			day += 146097;
			--year;
		}
		year *= 4;
		if (day == 146096) {
			year += 3;
			day = 36524;
		} else {
			year += day / 36524;
			day %= 36524;
		}
		year *= 25;
		year += day / 1461;
		day %= 1461;
		year *= 4;
		if (day == 1460) {
			year += 3;
			day = 365;
		} else {
			year += day / 365;
			day %= 365;
		}
		day *= 10;
		if ((day + 5) / 306 >= 10)
			++year;
		currentyear = year;
		flagneedcurrentyear = 0;
	}
}

static long guesstai(long month, long mday)
{
	long year;
	long t;

	initnow();

	for (year = currentyear - 1; year < currentyear + 100; ++year) {
		t = totai(year, month, mday);
		if (now - t < 350 * 86400)
			return t;
	}
	return 0;
}

static int check(char *buf, char *monthname)
{
	if ((buf[0] != monthname[0]) && (buf[0] != monthname[0] - 32))
		return 0;
	if ((buf[1] != monthname[1]) && (buf[1] != monthname[1] - 32))
		return 0;
	if ((buf[2] != monthname[2]) && (buf[2] != monthname[2] - 32))
		return 0;
	return 1;
}

static int getmonth(char *buf, int len)
{
	int i;
	if (len == 3)
		for (i = 0; i < 12; ++i)
			if (check(buf, months[i]))
				return i;
	return -1;
}

static long getlong(char *buf, int len)
{
	long u = 0;
	while (len-- > 0)
		u = u * 10 + (*buf++ - '0');
	return u;
}

int ftp_parse(struct ftpparse *fp, char *buf, int len)
{
	int i;
	int j;
	int state;
	long size;
	long year;
	long month;
	long mday;
	long hour;
	long minute;

	fp->name = 0;
	fp->namelen = 0;
	fp->flagtrycwd = 0;
	fp->flagtryretr = 0;
	fp->sizetype = FTPPARSE_SIZE_UNKNOWN;
	fp->size = 0;
	fp->mtimetype = FTPPARSE_MTIME_UNKNOWN;
	fp->mtime = 0;
	fp->idtype = FTPPARSE_ID_UNKNOWN;
	fp->id = 0;
	fp->idlen = 0;

	if (len < 2)
		return 0;

	switch (*buf) {
	case '+':
		i = 1;
		for (j = 1; j < len; ++j) {
			if (buf[j] == 9) {
				fp->name = buf + j + 1;
				fp->namelen = len - j - 1;
				return 1;
			}
			if (buf[j] == ',') {
				switch (buf[i]) {
				case '/':
					fp->flagtrycwd = 1;
					break;
				case 'r':
					fp->flagtryretr = 1;
					break;
				case 's':
					fp->sizetype = FTPPARSE_SIZE_BINARY;
					fp->size = getlong(buf + i + 1, j - i - 1);
					break;
				case 'm':
					fp->mtimetype = FTPPARSE_MTIME_LOCAL;
					initbase();
					fp->mtime = base + getlong(buf + i + 1, j - i - 1);
					break;
				case 'i':
					fp->idtype = FTPPARSE_ID_FULL;
					fp->id = buf + i + 1;
					fp->idlen = j - i - 1;
				}
				i = j + 1;
			}
		}
		return 0;

	case 'b':
	case 'c':
	case 'd':
	case 'l':
	case 'p':
	case 's':
	case '-':

		if (*buf == 'd')
			fp->flagtrycwd = 1;
		if (*buf == '-')
			fp->flagtryretr = 1;
		if (*buf == 'l')
			fp->flagtrycwd = fp->flagtryretr = 1;

		state = 1;
		i = 0;
		for (j = 1; j < len; ++j)
			if ((buf[j] == ' ') && (buf[j - 1] != ' ')) {
				switch (state) {
				case 1:
					state = 2;
					break;
				case 2:
					state = 3;
					if ((j - i == 6) && (buf[i] == 'f'))

						state = 4;
					break;
				case 3:
					state = 4;
					break;
				case 4:
					size = getlong(buf + i, j - i);
					state = 5;
					break;
				case 5:

					month = getmonth(buf + i, j - i);
					if (month >= 0)
						state = 6;
					else
						size = getlong(buf + i, j - i);
					break;
				case 6:
					mday = getlong(buf + i, j - i);
					state = 7;
					break;
				case 7:
					if ((j - i == 4) && (buf[i + 1] == ':')) {
						hour = getlong(buf + i, 1);
						minute = getlong(buf + i + 2, 2);
						fp->mtimetype = FTPPARSE_MTIME_REMOTEMINUTE;
						initbase();
						fp->mtime =
							base + guesstai(month,
											mday) + hour * 3600 +
							minute * 60;
					} else if ((j - i == 5) && (buf[i + 2] == ':')) {
						hour = getlong(buf + i, 2);
						minute = getlong(buf + i + 3, 2);
						fp->mtimetype = FTPPARSE_MTIME_REMOTEMINUTE;
						initbase();
						fp->mtime =
							base + guesstai(month,
											mday) + hour * 3600 +
							minute * 60;
					} else if (j - i >= 4) {
						year = getlong(buf + i, j - i);
						fp->mtimetype = FTPPARSE_MTIME_REMOTEDAY;
						initbase();
						fp->mtime = base + totai(year, month, mday);
					} else
						return 0;
					fp->name = buf + j + 1;
					fp->namelen = len - j - 1;
					state = 8;
					break;
				case 8:
					break;
				}
				i = j + 1;
				while ((i < len) && (buf[i] == ' '))
					++i;
			}

		if (state != 8)
			return 0;

		fp->size = size;
		fp->sizetype = FTPPARSE_SIZE_BINARY;

		if (*buf == 'l')
			for (i = 0; i + 3 < fp->namelen; ++i)
				if (fp->name[i] == ' ')
					if (fp->name[i + 1] == '-')
						if (fp->name[i + 2] == '>')
							if (fp->name[i + 3] == ' ') {
								fp->namelen = i;
								break;
							}
		if ((buf[1] == ' ') || (buf[1] == '['))
			if (fp->namelen > 3)
				if (fp->name[0] == ' ')
					if (fp->name[1] == ' ')
						if (fp->name[2] == ' ') {
							fp->name += 3;
							fp->namelen -= 3;
						}

		return 1;
	}

	for (i = 0; i < len; ++i)
		if (buf[i] == ';')
			break;
	if (i < len) {
		fp->name = buf;
		fp->namelen = i;
		if (i > 4)
			if (buf[i - 4] == '.')
				if (buf[i - 3] == 'D')
					if (buf[i - 2] == 'I')
						if (buf[i - 1] == 'R') {
							fp->namelen -= 4;
							fp->flagtrycwd = 1;
						}
		if (!fp->flagtrycwd)
			fp->flagtryretr = 1;
		while (buf[i] != ' ')
			if (++i == len)
				return 0;
		while (buf[i] == ' ')
			if (++i == len)
				return 0;
		while (buf[i] != ' ')
			if (++i == len)
				return 0;
		while (buf[i] == ' ')
			if (++i == len)
				return 0;
		j = i;
		while (buf[j] != '-')
			if (++j == len)
				return 0;
		mday = getlong(buf + i, j - i);
		while (buf[j] == '-')
			if (++j == len)
				return 0;
		i = j;
		while (buf[j] != '-')
			if (++j == len)
				return 0;
		month = getmonth(buf + i, j - i);
		if (month < 0)
			return 0;
		while (buf[j] == '-')
			if (++j == len)
				return 0;
		i = j;
		while (buf[j] != ' ')
			if (++j == len)
				return 0;
		year = getlong(buf + i, j - i);
		while (buf[j] == ' ')
			if (++j == len)
				return 0;
		i = j;
		while (buf[j] != ':')
			if (++j == len)
				return 0;
		hour = getlong(buf + i, j - i);
		while (buf[j] == ':')
			if (++j == len)
				return 0;
		i = j;
		while ((buf[j] != ':') && (buf[j] != ' '))
			if (++j == len)
				return 0;
		minute = getlong(buf + i, j - i);

		fp->mtimetype = FTPPARSE_MTIME_REMOTEMINUTE;
		initbase();
		fp->mtime =
			base + totai(year, month, mday) + hour * 3600 + minute * 60;

		return 1;
	}

	if ((*buf >= '0') && (*buf <= '9')) {
		i = 0;
		j = 0;
		while (buf[j] != '-')
			if (++j == len)
				return 0;
		month = getlong(buf + i, j - i) - 1;
		while (buf[j] == '-')
			if (++j == len)
				return 0;
		i = j;
		while (buf[j] != '-')
			if (++j == len)
				return 0;
		mday = getlong(buf + i, j - i);
		while (buf[j] == '-')
			if (++j == len)
				return 0;
		i = j;
		while (buf[j] != ' ')
			if (++j == len)
				return 0;
		year = getlong(buf + i, j - i);
		if (year < 50)
			year += 2000;
		if (year < 1000)
			year += 1900;
		while (buf[j] == ' ')
			if (++j == len)
				return 0;
		i = j;
		while (buf[j] != ':')
			if (++j == len)
				return 0;
		hour = getlong(buf + i, j - i);
		while (buf[j] == ':')
			if (++j == len)
				return 0;
		i = j;
		while ((buf[j] != 'A') && (buf[j] != 'P'))
			if (++j == len)
				return 0;
		minute = getlong(buf + i, j - i);
		if (hour == 12)
			hour = 0;
		if (buf[j] == 'A')
			if (++j == len)
				return 0;
		if (buf[j] == 'P') {
			hour += 12;
			if (++j == len)
				return 0;
		}
		if (buf[j] == 'M')
			if (++j == len)
				return 0;

		while (buf[j] == ' ')
			if (++j == len)
				return 0;
		if (buf[j] == '<') {
			fp->flagtrycwd = 1;
			while (buf[j] != ' ')
				if (++j == len)
					return 0;
		} else {
			i = j;
			while (buf[j] != ' ')
				if (++j == len)
					return 0;
			fp->size = getlong(buf + i, j - i);
			fp->sizetype = FTPPARSE_SIZE_BINARY;
			fp->flagtryretr = 1;
		}
		while (buf[j] == ' ')
			if (++j == len)
				return 0;

		fp->name = buf + j;
		fp->namelen = len - j;

		fp->mtimetype = FTPPARSE_MTIME_REMOTEMINUTE;
		initbase();
		fp->mtime =
			base + totai(year, month, mday) + hour * 3600 + minute * 60;

		return 1;
	}
	return 0;
}
