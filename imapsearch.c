/*
 * imapsearch.c
 *
 * search in an IMAP folder
 *
 * -DSSLFT for SSL transfers
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <term.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <signal.h>

/*
 * accounts
 */
struct {
	char name[100];
	char srv[100];
	char usr[100];
	char psw[100];
	char dir[100];
} *accts;

/*
 * type of files and port
 */
#ifdef SSLFT
#define FD SSL *
#define FD_read SSL_read
#define FD_write SSL_write
#define PORT 993
#else
#define FD int
#define FD_read read
#define FD_write write
#define PORT 143
#endif

/*
 * line reading 
 * 
 * read a line if balanced == 0
 *
 * otherwise, read until the next newline where parentheses are balanced and
 * the quote count is even
 *	1. reset the quote count on an open or closed parentesis
 *	2. do not count parenthesis if the quote count is uneven
 *	3. if {n} is encountered, skip n bytes (not including \r or \n)
 */
char *fdgets(char *buf, int size, FD sl, int balanced) {
	int pos;
	char c;
	int parent = 0, quote = 0, backslash = 0;
	int skip = 0;

	for (pos = 0; pos < size - 1; pos++) {

				/* read char */

		FD_read(sl, &c, 1);
		buf[pos] = c;
		// fputc(c, stdout); fflush(stdout);

				/* skip because of a previous {n} */

		if (skip != 0) {
			if (c == '\n' || c == '\r')
				pos--;
			else {
				buf[pos] = c;
				skip--;
				if (skip == 0)
					buf[++pos] = '"';
			}
			continue;
		}

				/* previous char was a backslash */

		if (backslash) {
			backslash = 0;
			continue;
		}

		switch (c) {

				/* current character is a backslash */
		case '\\':
			backslash = 1;
			break;

				/* parenthesis and quote */
		case '(':
			if (quote % 2)
				break;
			parent++;
			quote = 0;
			break;
		case ')':
			if (quote % 2)
				break;
			parent--;
			quote = 0;
			break;
		case '"':
			quote++;
			break;

				/* {n} means: skip n chars */
		case '{':
			if (! balanced || (quote % 2))
				break;
			skip = 0;
			while (1) {
				FD_read(sl, &c, 1);
				// printf("%c", c); fflush(stdout);
				if (c == '}')
					break;
				skip = skip * 10 + (c - '0');
			}
			buf[pos] = '"';
			break;

				/* \n ends line */
		case '\n':
			// printf("%d %d\n", parent, quote);
			if (! balanced || (parent <= 0 && quote % 2 == 0)) {
				buf[pos + 1] = '\0';
				return buf;
			}
			break;
		}
	}

	return buf;
}


/*
 * print a line on file/screen/command/nowhere
 */
void (*printstring) (char *);

FILE *outfile;
char *external;

void printstringtofile(char *s) {
	fprintf(outfile, "%s", s);
}

void printstringonscreen(char *s) {
	printf("%s", s);
	fflush(stdout);
}

void printstringtocommand(char *s) {
	FILE *pipe;
	pipe = popen(external, "w");
	if (pipe == NULL) {
		perror(external);
		return;
	}
	fwrite(s, 1, strlen(s), pipe);
	pclose(pipe);
}

void printstringnowhere(char *s) {
	(void) s;
}

/*
 * functions for sending command and receiving answers;
 * the protocol is:
 * 
 * send: tag command
 * recv: * line
 *       * line
 *       * line
 *       tag OK
 * 
 * where the tag is an arbitrary string, unique for each command.
 */

int cnum = 0;
char tag[10];

/*
 * send a command
 */
void sendcommand(FD sl, char *comm) {
	char *c;
	char *res;

	c = (char *) malloc(strlen(tag) + strlen(comm) + 10);
	sprintf(c, "%s%s\r\n", tag, comm);
	if (strstr(comm, "LOGIN") != comm)
		printstring(c);
	else {
		res = (char *) malloc(strlen(comm) + 10);
		sprintf(res, "%sLOGIN ...\n", tag);
		printstring(res);
		free(res);
	}

	FD_write(sl, c, strlen(c));
	free(c);
}

/*
 * receive answer
 */
char *recvanswer(FD sl) {
	char buf[10000];
	char *res, *r, *ok;

	res = strdup("");
	do {
		r = fdgets(buf, 9999, sl, 1);
		if (r == NULL)
			break;
		printstring(buf);
		res = (char *) realloc(res, strlen(res) + strlen(buf) + 2);
		strcat(res, buf);
	} while (buf[0] == '*');

	ok = (char *) malloc(strlen(tag) + 4);
	sprintf(ok, "%sOK ", tag);
	if (strstr(buf, ok) != buf) {
		free(ok);
		free(res);
		return NULL;
	}
	free(ok);

	return res;
}
void logout(int ret);

/*
 * send command and receive answer
 */
char *sendrecv(FD fd, char *comm) {
	char *res;

	sprintf(tag, "A%03d ", cnum);

	sendcommand(fd, comm);
	res = recvanswer(fd);
	if (res == NULL) {
		printf("Received non-OK answer, exiting\n");
		if (! strcmp(comm, "LOGOUT"))
			exit(EXIT_FAILURE);
		else
			logout(EXIT_FAILURE);
	}

	cnum++;

	return res;
}

/*
 * receive an email
 */
void recvemail(FD sl, FILE* dest) {
	char buf[1000];
	char *res;
	int size, nr;

	res = fdgets(buf, 999, sl, 0);
	printstring(res);
	while (*res != '{')
		res++;
	if (sscanf(res, "{%d}", &size) != 1) {
		printf("cannot read mail size\n");
		logout(EXIT_FAILURE);
	}

	res = (char *) malloc(1024);

	while (size > 0) {
		nr = FD_read(sl, res, size < 1024 ? size : 1024);
		fwrite(res, 1, nr, dest);
		size -= nr;
	}
	free(res);

	fdgets(buf, 999, sl, 0);
	printstring(buf);
}

/*
 * download an email: this is different from the other
 * commands, as the email is returned as:
 * * n FETCH (BODY[] {size}
 * email
 * )
 * TAG OK
*/
char *fetch(FD sl, char *comm, FILE *dest) {
	char *res;

	sprintf(tag, "A%03d ", cnum);

	sendcommand(sl, comm);
	recvemail(sl, dest);
	res = recvanswer(sl);
	if (res == NULL) {
		printf("Received non-OK answer, exiting\n");
		logout(EXIT_FAILURE);
	}

	cnum++;

	return res;
}

/*
 * prints all errors in the ssl error queue
 */
#ifdef SSLFT
void printsslerrors() {
	char buf[120];
	u_long err;

	while (0 != (err = ERR_get_error())) {
		ERR_error_string(err, buf);
		printf("*** %s\n", buf);
	}
}
#endif

/*
 * open a socket to hostname and port, return file descritor
 */
FD openfdsocket(char *hostname, int port) {
	int sock;
	struct sockaddr_in server;
	struct hostent *hp, *gethostbyname();
	FD sl;
	int res;

	(void) sl;
	(void) res;

#ifdef SSLFT
	SSL_CTX *sc;
#endif

			/* open socket */

	printstring("opening socket\n") ;
	sock = socket(AF_INET , SOCK_STREAM , 0);
	if (sock == -1) {
		perror("Opening socket stream");
		exit(2) ;
	}

			/* get host */

	server.sin_family = AF_INET;
	hp = gethostbyname(hostname);

	if (hp == (struct hostent *) 0) {
		fprintf(stderr , "Unknow host: %s\n" , hostname);
		exit(2);
	}

			/* connect */
	
	printstring("connecting\n") ;
	memcpy((char *) &server.sin_addr , (char *) hp->h_addr, hp->h_length);
	server.sin_port = htons(port);
	if (connect(sock, (struct sockaddr *) &server , sizeof server) == -1) {
		perror("Connecting stream socket");
		exit(1);
	}

#ifdef SSLFT
			/* SSL init */

	ERR_load_crypto_strings();
	SSL_load_error_strings();
	SSL_library_init();

	// sc = SSL_CTX_new(SSLv3_client_method());
	sc = SSL_CTX_new(SSLv23_client_method());
	if (sc == NULL) {
		printf("Error creating SSL_CTX object\n");
		exit(1);
	}

	sl = SSL_new(sc);
	if (sl == NULL) {
		printf("Error creating SSL object\n");
		exit(1);
	}

  			/* SSL connect */

	res = SSL_set_fd(sl, sock);
	if (res != 1) {
		printf("Error on SSL_set_fd\n");
		exit(1);
	}

	res = SSL_connect(sl);
	if (res != 1) {
		printf("Error on SSL_connect: res=%d, SSL_get_error=%d\n",
		       res, SSL_get_error(sl, res));
		printf("%s\n", ERR_error_string(SSL_get_error(sl, res), NULL));
		printsslerrors();
		exit(EXIT_FAILURE);
	}

	return sl;
#else
	return sock;
#endif
}

/*
 * logout
 */
FD sl;
void logout(int ret) {
	char *res;
	printf("logout\n");
	res = sendrecv(sl, "LOGOUT");
	free(res);
#ifdef SSLFT
	SSL_shutdown(sl);
#endif
	exit(ret);
}

/*
 * read a character
 */
char readchar() {
	struct termios original, charbychar;
	char c;

	tcgetattr(STDIN_FILENO, &original);

	charbychar = original;
	charbychar.c_lflag &= ~(ICANON | ISIG | IEXTEN);
	charbychar.c_cc[VMIN] = 1;
	charbychar.c_cc[VTIME] = 0;

	tcsetattr(STDIN_FILENO, TCSANOW, &charbychar);

	fread(&c, 1, 1, stdin);

	tcsetattr(STDIN_FILENO, TCSANOW, &original);

	return c;
}

/*
 * signal hander
 */
int breakloop = 0;
void interrupt(int sig) {
	printf("received signal %d\n", sig);
	logout(EXIT_FAILURE);
}

/*
 * string to int array
 */
int *stringtointarray(char *s, int *size) {
	int alloc = 100, *idx;
	char *cur;

	idx = malloc(alloc * sizeof(int));

	*size = 0;
	cur = strtok(s, " ");
	while (cur != NULL) {
		if (atoi(cur) != 0) {
			if (*size >= alloc) {
				alloc += 100;
				idx = realloc(idx, alloc * sizeof(int));
			}
			idx[*size] = atoi(cur);
			(*size)++;
		}
		cur = strtok(NULL, " ");
	}

	return idx;
}

/*
 * int array to string
 */
char *intarraytostring(int *a, int n, char *sep) {
	char *r, buf[20];
	int len = 0, alloc = 0, inc = 1024;
	int i;

	r = strdup("");

	for (i = 0; i < n; i++) {
		sprintf(buf, "%d", a[i]);
		while (len + 1 + (int) strlen(buf) > alloc)
			r = realloc(r, alloc += inc);
		len += 1 + strlen(buf);
		strcat(r, i != 0 ? sep : "");
		strcat(r, buf);
	}

	return r;
}

/*
 * usage
 */
void usage(int ret) {
	int i;
	printf("imapsearch [options] -- account [first [last]]\n");
	printf("\t-f email\tonly from this sender\n");
	printf("\t-o email\tonly to this recipient\n");
	printf("\t-s word\t\tonly with subject containing this whole word\n");
	printf("\t-t text\t\tonly containing this string, anywhere\n");
	printf("\t-a date\t\tonly after this date, like 12-Dec-2022\n");
	printf("\t-r range\tonly in this range, like 9120:*\n");
	printf("\t-c string\texecute this IMAP command\n");
	printf("\t-v viewer\tshow envelopes with this external command\n");
	printf("\t-w\t\topen mailbox read-write (default is read-only)\n");
	printf("\t-e\t\tonly execute search or user-given command\n");
	printf("\t-l\t\tonly list emails\n");
	printf("\t-x\t\tenvelope, flags and structure of each email\n");
	printf("\t-b\t\tbody of the each email\n");
	printf("\t-p prefix\tsave each email n to prefix.n\n");
	printf("\t-d\t\tdelete emails found (with -w), after confirm\n");
	printf("\t-i\t\tlist of inboxes in the server\n");
	printf("\t-z\t\tprint account file name list of accounts\n");
	printf("\t-V\t\tverbose\n");
	printf("\t-h\t\tinline help\n");
	printf("\taccount\t\taccount number\n");
	printf("\tfirst\t\tfirst email, negative means from last\n");
	printf("\tlast\t\tlast email, negative means from last\n");
	if (accts == NULL)
		exit(ret);
	printf("accounts:\n");
	for (i = 0; accts[i].srv[0] != '\0'; i++)
		printf("%d: user %s, mail in %s/%s\n",
		       i, accts[i].usr, accts[i].srv, accts[i].dir);
	exit(ret);
}

/*
 * main
 */
#define BUFLEN 1000

int main(int argn, char *argv[]) {
	int opt;
	char *search, *searchadd, *command, *uid;
	char *viewer;
	int searchsize;
	int inboxes, commandonly, listonly, structure, body, restore, delete;
	int accounts, verbose;
	char *prefix, *openas;
	int accno;
	char *accname;
	char buf[BUFLEN], fname[BUFLEN] = "";
	char *res, *cur, *next;
	int i, j;
	int n, m;
	char c;
	char *seen;
	FILE *file;
	int first, last;
	int begin, end;
	int *idx;

			/* args */

	inboxes = 0;
	search = NULL;
	searchsize = 0;
	command = NULL;
	viewer = NULL;
	commandonly = 0;
	openas = "EXAMINE";
	listonly = 0;
	structure = 0;
	body = 0;
	prefix = NULL;
	restore = 1;
	delete = 0;
	verbose = 0;
	accts = NULL;
	accounts = 0;
	n = -1;
	while (-1 != (opt = getopt(argn, argv, "f:o:s:t:a:r:c:v:ewlxbp:dizVh"))) {
		searchadd = NULL;
		switch(opt) {
			case 'f':
				searchadd = " FROM ";
				break;
			case 'o':
				searchadd = " TO ";
				break;
			case 's':
				searchadd = " SUBJECT ";
				break;
			case 't':
				searchadd = " TEXT ";
				break;
			case 'a':
				searchadd = " SINCE ";
				break;
			case 'r':
				searchadd = " ";
				break;
			case 'c':
				command = optarg;
				free(search);
				search = NULL;
				break;
			case 'v':
				viewer = optarg;
				break;
			case 'w':
				openas = "SELECT";
				break;
			case 'e':
				commandonly = 1;
				break;
			case 'l':
				listonly = 1;
				break;
			case 'x':
				structure = 1;
				break;
			case 'b':
				body = 1;
				break;
			case 'p':
				body = 1;
				prefix = optarg;
				break;
			case 'd':
				delete = 1;
				break;
			case 'i':
				inboxes = 1;
				break;
			case 'z':
				accounts = 1;
				break;
			case 'V':
				verbose = 1;
				break;
			case 'h':
				usage(EXIT_SUCCESS);
				break;
			default:
				usage(EXIT_FAILURE);
		}
		if (searchadd) {
			searchsize += 3 + strlen(searchadd) + strlen(optarg);
			search = realloc(search, searchsize);
			strcat(search, searchadd);
			strcat(search, optarg);
		}
	}
	if (argn - 1 < optind && ! accounts) {
		printf("too few arguments\n");
		usage(EXIT_FAILURE);
	}

			/* accounts */

	if (getenv("HOME") == NULL) {
		printf("no home directory\n");
		usage(EXIT_FAILURE);
	}
	strcpy(buf, getenv("HOME"));
	strcat(buf, "/.config/imapsearch/accounts.txt");
	file = fopen(buf, "r");
	if (file == NULL) {
		printf("cannot open account file %s\n", buf);
		usage(EXIT_FAILURE);
	}
	i = 0;
	j = 10;
	accts = realloc(accts, j * sizeof(*accts));
	while (5 == fscanf(file, "%99s	%99s	%99s	%99s	%99s\n",
			accts[i].name, accts[i].srv, accts[i].usr,
			accts[i].psw, accts[i].dir)) {
		i++;
		if (i > j) {
			j += 10;
			accts = realloc(accts, j * sizeof(*accts));
		}

	}
	fclose(file);
	if (accounts) {
		printf("accounts in %s\n", buf);
		for (i = 0; accts[i].srv[0] != '\0'; i++)
			printf("%d: user %s, mail in %s/%s\n",
			       i, accts[i].usr, accts[i].srv, accts[i].dir);
		return EXIT_SUCCESS;
	}

			/* account number, first and last mail */

	accno = strtol(argv[optind], &res, 10);
	if (*res != '\0')
		accname = argv[optind];
	else {
		accname = NULL;
		if (accno < 0) {
			printf("account number cannot be negative\n");
			usage(EXIT_FAILURE);
		}
	}
	first = argn - 1 <= optind + 0 ? 1 : atoi(argv[optind + 1]);
	last =  argn - 1 <= optind + 1 ? 0 : atoi(argv[optind + 2]);

			/* account and password */

	if (accname != NULL)
		for (accno = 0; accno < i; accno++)
			if (! strcmp(accts[accno].name, accname))
				break;
	if (accno >= i) {
		printf("no such account\n");
		usage(EXIT_FAILURE);
	}
	printf("reading mail for %s in %s/%s ",
	       accts[accno].usr, accts[accno].srv, accts[accno].dir);
	printf("from %d to %d\n", first, last);
	printf("search string: %s\n", search);

	if (! strcmp(accts[accno].psw, "NULL")) {
		sprintf(buf, "password for %s on %s: ",
		        accts[accno].usr, accts[accno].srv);
		strcpy(accts[accno].psw, getpass(buf));
	}

			/* set verbosity */

	printstring = verbose ? printstringonscreen : printstringnowhere;

			/* open socket */

	sl = openfdsocket(accts[accno].srv, PORT);

			/* read a line */

	fdgets(buf, BUFLEN - 1, sl, 0);
	printstring(buf);

			/* test connection */

	strcpy(buf, "NOOP");
	SIMULATE_ERROR("noop", buf);
	res = sendrecv(sl, buf);
	free(res);

			/* login */

	sprintf(buf, "LOGIN %s %s", accts[accno].usr, accts[accno].psw);
	res = sendrecv(sl, buf);
	free(res);

			/* list inboxes */

	if (inboxes) {
		res = sendrecv(sl, "LIST \"\" *");
		if (! verbose)
			fputs(res, stdout);
		free(res);
		logout(EXIT_SUCCESS);
	}

			/* signal handler: exit from loop */

	signal(SIGINT, interrupt);
	signal(SIGTERM, interrupt);

			/* select inbox; OPENAS = SELECT or EXAMINE */

	if (accts[accno].dir[0] == '\0')
		sprintf(buf, "%s INBOX", openas);
	else
		sprintf(buf, "%s %s", openas, accts[accno].dir);
	res = sendrecv(sl, buf);
	for (cur = res;
	     cur != NULL && (cur == res || ++cur != NULL);
	     cur = index(cur, '\n'))
		if (2 == sscanf(cur, "* %d EXISTS%c", &i, &c)) {
			n = i;
			break;
		}
	free(res);

	if (n == -1)
		logout(EXIT_FAILURE);
	printf("[%d emails]\n", n);
	fflush(stdout);

	first = first > 0 ? first : n + first + 1;
	last  =  last > 0 ?  last : n + last;

			/* search */

	if (search == NULL && command == NULL && ! delete) {
		uid = "";
		idx = NULL;
		begin = first;
		end = last;
		printf("mails from %d to %d\n", begin, end);
		sprintf(buf, "%sFETCH %d:%d ENVELOPE", uid, begin, end);
	}
	else {
		if (command != NULL)
			strcpy(buf, command);
		else
			sprintf(buf, "UID SEARCH %d:%d%s", first, last,
				search == NULL ? "" : search);
		printf("command: %s\n", buf);
		uid = strstr(buf, "UID ") == buf ? "UID " : "";
		res = sendrecv(sl, buf);
		if (strstr(res, "* SEARCH ") != res)
			commandonly = 1;
		else
			idx = stringtointarray(res, &m);
		if (commandonly && ! verbose)
			fputs(res, stdout);
		free(res);
		if (commandonly)
			logout(EXIT_SUCCESS);

		printf("emails found: %d\n", m);
		if (m == 0)
			logout(EXIT_FAILURE);
		for (i = 0; i < m; i++)
			printf("%d ", idx[i]);
		printf("\n");

		begin = 0;
		end = m - 1;
		printf("emails from %d to %d\n", idx[begin], idx[end]);
		printf("between %d and %d\n", first, last);
		breakloop = 0;

		cur = intarraytostring(idx, end + 1, ",");
		// alternative: (ENVELOPE FLAGS)
		sprintf(buf, "%sFETCH %s ENVELOPE", uid, cur);
		free(cur);
	}

			/* load all envelopes at once if possible */

	if (! listonly && ! structure && ! body && ! (delete && idx != NULL)) {
		res = sendrecv(sl, buf);
		if (res == NULL) {
		}
		else if (viewer)
			for (cur = res; *cur == '*'; cur = next + 2) {
				next = strchr(cur, '\r');
				if (next == NULL)
					break;
				*next = '\0';
				external = viewer;
				printstringtocommand(cur);
			}
		else if (! verbose)
			printstringonscreen(res);
		free(res);
		logout(EXIT_SUCCESS);
	}

			/* loop over emails */

	for (i = begin; i <= end && ! breakloop; i++) {
					/* email index */
		j = idx == NULL ? i : idx[i];
		printf("email %d\n", j);
		if (listonly)
			continue;

					/* fetch envelope */
		sprintf(buf, "%sFETCH %d ENVELOPE", uid, j);
		res = sendrecv(sl, buf);
		if (! verbose) {
			cur = strchr(res, '\n');
			if (cur && *cur != '\0')
				*(cur + 1) = '\0';
			if (! viewer)
				printstringonscreen(res);
			else {
				external = viewer;
				printstringtocommand(res);
			}
		}
		free(res);

					/* get flags */
		if (structure || body) {
			sprintf(buf, "%sFETCH %d FLAGS", uid, j);
			res = sendrecv(sl, buf);
			seen = strstr(res, "\\Seen");
			if (! verbose)
				fputs(res, stdout);
			free(res);
		}

					/* get body structure */
		if (structure) {
			sprintf(buf, "%sFETCH %d BODYSTRUCTURE", uid, j);
			res = sendrecv(sl, buf);
			if (! verbose)
				fputs(res, stdout);
			free(res);
		}

					/* get body */
		if (body) {
			sprintf(buf, "%sFETCH %d BODY.PEEK[]", uid, j);
			if (prefix == NULL)
				res = fetch(sl, buf, stdout);
			else {
				snprintf(fname, BUFLEN, "%s.%d", prefix, j);
				printf("content -> %s\n", fname);
				file = fopen(fname, "w");
				if (file == NULL) {
					printf("cannot open file %s\n", fname);
					continue;
				}
				res = fetch(sl, buf, file);
				fclose(file);
			}
			free(res);
		}

					/* delete email */
		if (delete && idx != NULL) {
			printf("delete email [yes/No/exit]? ");
			c = readchar();
			printf("\n");
			switch(c) {
			case 'y':
				sprintf(buf,
					"%sSTORE %d:%d +FLAGS (\\Deleted)",
					uid, j, j);
				res = sendrecv(sl, buf);
				free(res);
				break;
			case 'e':
				logout(EXIT_SUCCESS);
			}
		}

					/* get flags again, just to be sure */
		if (structure || body) {
			sprintf(buf, "%sFETCH %d FLAGS", uid, j);
			res = sendrecv(sl, buf);
			free(res);
		}

					/* set flags to previous ones */
		if (restore && ! ! strcmp(openas, "EXAMINE") &&
		   (body || structure) && ! seen && ! delete) {
			/* not really necessary because of FETCH BODY.PEEK */
			sprintf(buf, "%sSTORE %d:%d -FLAGS (\\SEEN)",
				uid, j, j);
			res = sendrecv(sl, buf);
			free(res);
		}
	}

	if (prefix && fname[0] != '\0') {
		printf("*** emails saved\n");
		printf("*** uncompress with munpack (unpack package)\n");
	}

	logout(EXIT_SUCCESS);
}
