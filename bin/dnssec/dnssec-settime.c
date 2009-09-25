/*
 * Copyright (C) 2009  Internet Systems Consortium, Inc. ("ISC")
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id: dnssec-settime.c,v 1.13 2009/09/23 16:01:56 each Exp $ */

/*! \file */

#include <config.h>

#include <libgen.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#include <isc/buffer.h>
#include <isc/commandline.h>
#include <isc/entropy.h>
#include <isc/file.h>
#include <isc/hash.h>
#include <isc/mem.h>
#include <isc/print.h>
#include <isc/string.h>
#include <isc/util.h>

#include <dns/keyvalues.h>
#include <dns/result.h>

#include <dst/dst.h>

#include "dnssectool.h"

const char *program = "dnssec-settime";
int verbose;

static isc_mem_t	*mctx = NULL;

static void
usage(void) {
	fprintf(stderr, "Usage:\n");
	fprintf(stderr,	"    %s [options] keyfile\n\n", program);
	fprintf(stderr, "Version: %s\n", VERSION);
	fprintf(stderr, "General options:\n");
	fprintf(stderr, "    -f:                 force update of old-style "
						 "keys\n");
	fprintf(stderr, "    -K directory:       set key file location\n");
	fprintf(stderr, "    -v level:           set level of verbosity\n");
	fprintf(stderr, "    -h:                 help\n");
	fprintf(stderr, "Timing options:\n");
	fprintf(stderr, "    -P date/[+-]offset/none: set/unset key "
						     "publication date\n");
	fprintf(stderr, "    -A date/[+-]offset/none: set key "
						     "activation date\n");
	fprintf(stderr, "    -R date/[+-]offset/none: set key "
						     "revocation date\n");
	fprintf(stderr, "    -I date/[+-]offset/none: set key "
						     "inactivation date\n");
	fprintf(stderr, "    -D date/[+-]offset/none: set key "
						     "deletion date\n");
	fprintf(stderr, "Printing options:\n");
	fprintf(stderr, "    -p C/P/A/R/U/D/all: print a particular time "
						"value or values "
						"[default: all]\n");
	fprintf(stderr, "    -u:                 print times in unix epoch "
						"format\n");
	fprintf(stderr, "Output:\n");
	fprintf(stderr, "     K<name>+<alg>+<new id>.key, "
			     "K<name>+<alg>+<new id>.private\n");

	exit (-1);
}

static void
printtime(dst_key_t *key, int type, const char *tag, isc_boolean_t epoch,
	  FILE *stream)
{
	isc_result_t result;
	const char *output = NULL;
	isc_stdtime_t when;

	if (tag != NULL)
		fprintf(stream, "%s: ", tag);

	result = dst_key_gettime(key, type, &when);
	if (result == ISC_R_NOTFOUND) {
		fprintf(stream, "UNSET\n");
	} else if (epoch) {
		fprintf(stream, "%d\n", (int) when);
	} else {
		time_t time = when;
		output = ctime(&time);
		fprintf(stream, "%s", output);
	}
}

int
main(int argc, char **argv) {
	isc_result_t result;
	char *filename = NULL, *directory = NULL;
	char newname[1024];
	char keystr[KEY_FORMATSIZE];
	char *endp, *p;
	int ch;
	isc_entropy_t *ectx = NULL;
	dst_key_t *key = NULL;
	isc_buffer_t buf;
	int major, minor;
	isc_stdtime_t	now;
	isc_stdtime_t	pub = 0, act = 0, rev = 0, inact = 0, del = 0;
	isc_boolean_t	setpub = ISC_FALSE, setact = ISC_FALSE;
	isc_boolean_t	setrev = ISC_FALSE, setinact = ISC_FALSE;
	isc_boolean_t	setdel = ISC_FALSE;
	isc_boolean_t	unsetpub = ISC_FALSE, unsetact = ISC_FALSE;
	isc_boolean_t	unsetrev = ISC_FALSE, unsetinact = ISC_FALSE;
	isc_boolean_t	unsetdel = ISC_FALSE;
	isc_boolean_t	printcreate = ISC_FALSE, printpub = ISC_FALSE;
	isc_boolean_t	printact = ISC_FALSE,  printrev = ISC_FALSE;
	isc_boolean_t	printinact = ISC_FALSE, printdel = ISC_FALSE;
	isc_boolean_t	forceupdate = ISC_FALSE;
	isc_boolean_t   epoch = ISC_FALSE;
	isc_boolean_t   changed = ISC_FALSE;

	if (argc == 1)
		usage();

	result = isc_mem_create(0, 0, &mctx);
	if (result != ISC_R_SUCCESS)
		fatal("Out of memory");

	dns_result_register();

	isc_commandline_errprint = ISC_FALSE;

	isc_stdtime_get(&now);

	while ((ch = isc_commandline_parse(argc, argv,
					   "fK:uhp:v:P:A:R:I:D:")) != -1) {
		switch (ch) {
		case 'f':
			forceupdate = ISC_TRUE;
			break;
		case 'p':
			p = isc_commandline_argument;
			if (!strcasecmp(p, "all")) {
				printcreate = ISC_TRUE;
				printpub = ISC_TRUE;
				printact = ISC_TRUE;
				printrev = ISC_TRUE;
				printinact = ISC_TRUE;
				printdel = ISC_TRUE;
				break;
			}

			do {
				switch (*p++) {
				case 'C':
					printcreate = ISC_TRUE;
					break;
				case 'P':
					printpub = ISC_TRUE;
					break;
				case 'A':
					printact = ISC_TRUE;
					break;
				case 'R':
					printrev = ISC_TRUE;
					break;
				case 'I':
					printinact = ISC_TRUE;
					break;
				case 'D':
					printdel = ISC_TRUE;
					break;
				case ' ':
					break;
				default:
					usage();
					break;
				}
			} while (*p != '\0');
			break;
		case 'u':
			epoch = ISC_TRUE;
			break;
		case 'K':
			/*
			 * We don't have to copy it here, but do it to
			 * simplify cleanup later
			 */
			directory = isc_mem_strdup(mctx,
						   isc_commandline_argument);
			if (directory == NULL) {
				fatal("Failed to allocate memory for "
				      "directory");
			}
			break;
		case 'v':
			verbose = strtol(isc_commandline_argument, &endp, 0);
			if (*endp != '\0')
				fatal("-v must be followed by a number");
			break;
		case 'P':
			if (setpub || unsetpub)
				fatal("-P specified more than once");

			changed = ISC_TRUE;
			if (!strcasecmp(isc_commandline_argument, "none")) {
				unsetpub = ISC_TRUE;
			} else {
				setpub = ISC_TRUE;
				pub = strtotime(isc_commandline_argument,
						now, now);
			}
			break;
		case 'A':
			if (setact || unsetact)
				fatal("-A specified more than once");

			changed = ISC_TRUE;
			if (!strcasecmp(isc_commandline_argument, "none")) {
				unsetact = ISC_TRUE;
			} else {
				setact = ISC_TRUE;
				act = strtotime(isc_commandline_argument,
						now, now);
			}
			break;
		case 'R':
			if (setrev || unsetrev)
				fatal("-R specified more than once");

			changed = ISC_TRUE;
			if (!strcasecmp(isc_commandline_argument, "none")) {
				unsetrev = ISC_TRUE;
			} else {
				setrev = ISC_TRUE;
				rev = strtotime(isc_commandline_argument,
						now, now);
			}
			break;
		case 'I':
			if (setinact || unsetinact)
				fatal("-I specified more than once");

			changed = ISC_TRUE;
			if (!strcasecmp(isc_commandline_argument, "none")) {
				unsetinact = ISC_TRUE;
			} else {
				setinact = ISC_TRUE;
				inact = strtotime(isc_commandline_argument,
						now, now);
			}
			break;
		case 'D':
			if (setdel || unsetdel)
				fatal("-D specified more than once");

			changed = ISC_TRUE;
			if (!strcasecmp(isc_commandline_argument, "none")) {
				unsetdel = ISC_TRUE;
			} else {
				setdel = ISC_TRUE;
				del = strtotime(isc_commandline_argument,
						now, now);
			}
			break;
		case '?':
			if (isc_commandline_option != '?')
				fprintf(stderr, "%s: invalid argument -%c\n",
					program, isc_commandline_option);
			/* Falls into */
		case 'h':
			usage();

		default:
			fprintf(stderr, "%s: unhandled option -%c\n",
				program, isc_commandline_option);
			exit(1);
		}
	}

	if (argc < isc_commandline_index + 1 ||
	    argv[isc_commandline_index] == NULL)
		fatal("The key file name was not specified");
	if (argc > isc_commandline_index + 1)
		fatal("Extraneous arguments");

	if (directory != NULL) {
		filename = argv[isc_commandline_index];
	} else {
		isc_file_splitpath(mctx, argv[isc_commandline_index],
				   &directory, &filename);
	}

	if (ectx == NULL)
		setup_entropy(mctx, NULL, &ectx);
	result = isc_hash_create(mctx, ectx, DNS_NAME_MAXWIRE);
	if (result != ISC_R_SUCCESS)
		fatal("Could not initialize hash");
	result = dst_lib_init(mctx, ectx,
			      ISC_ENTROPY_BLOCKING | ISC_ENTROPY_GOODONLY);
	if (result != ISC_R_SUCCESS)
		fatal("Could not initialize dst");
	isc_entropy_stopcallbacksources(ectx);

	result = dst_key_fromnamedfile(filename, directory,
				       DST_TYPE_PUBLIC | DST_TYPE_PRIVATE,
				       mctx, &key);
	if (result != ISC_R_SUCCESS)
		fatal("Invalid keyfile %s: %s",
		      filename, isc_result_totext(result));

	if (!dst_key_isprivate(key))
		fatal("%s is not a private key", filename);

	key_format(key, keystr, sizeof(keystr));

	/* Is this an old-style key? */
	dst_key_getprivateformat(key, &major, &minor);
	if (major <= 1 && minor <= 2) {
		if (forceupdate) {
			/*
			 * Updating to new-style key: set
			 * Private-key-format to 1.3
			 */
			dst_key_setprivateformat(key, 1, 3);
			dst_key_settime(key, DST_TIME_CREATED, now);
		} else
			fatal("Incompatible key %s, "
			      "use -f to force update.", keystr);
	}

	if (verbose > 2)
		fprintf(stderr, "%s: %s\n", program, keystr);

	/*
	 * Set time values.
	 */
	if (setpub)
		dst_key_settime(key, DST_TIME_PUBLISH, pub);
	else if (unsetpub)
		dst_key_unsettime(key, DST_TIME_PUBLISH);

	if (setact)
		dst_key_settime(key, DST_TIME_ACTIVATE, act);
	else if (unsetact)
		dst_key_unsettime(key, DST_TIME_ACTIVATE);

	if (setrev) {
		if ((dst_key_flags(key) & DNS_KEYFLAG_REVOKE) != 0)
			fprintf(stderr, "%s: warning: Key %s is already "
					"revoked; changing the revocation date "
					"will not affect this.\n",
					program, keystr);
		if ((dst_key_flags(key) & DNS_KEYFLAG_KSK) == 0)
			fprintf(stderr, "%s: warning: Key %s is not flagged as "
					"a KSK, but -R was used.  Revoking a "
					"ZSK is legal, but undefined.\n",
					program, keystr);
		dst_key_settime(key, DST_TIME_REVOKE, rev);
	} else if (unsetrev) {
		if ((dst_key_flags(key) & DNS_KEYFLAG_REVOKE) != 0)
			fprintf(stderr, "%s: warning: Key %s is already "
					"revoked; removing the revocation date "
					"will not affect this.\n",
					program, keystr);
		dst_key_unsettime(key, DST_TIME_REVOKE);
	}

	if (setinact)
		dst_key_settime(key, DST_TIME_INACTIVE, inact);
	else if (unsetinact)
		dst_key_unsettime(key, DST_TIME_INACTIVE);

	if (setdel)
		dst_key_settime(key, DST_TIME_DELETE, del);
	else if (unsetdel)
		dst_key_unsettime(key, DST_TIME_DELETE);

	/*
	 * Print out time values, if -p was used.
	 */
	if (printcreate)
		printtime(key, DST_TIME_CREATED, "Created", epoch, stdout);

	if (printpub)
		printtime(key, DST_TIME_PUBLISH, "Publish", epoch, stdout);

	if (printact)
		printtime(key, DST_TIME_ACTIVATE, "Activate", epoch, stdout);

	if (printrev)
		printtime(key, DST_TIME_REVOKE, "Revoke", epoch, stdout);

	if (printinact)
		printtime(key, DST_TIME_INACTIVE, "Inactive", epoch, stdout);

	if (printdel)
		printtime(key, DST_TIME_DELETE, "Delete", epoch, stdout);

	if (changed) {
		isc_buffer_init(&buf, newname, sizeof(newname));
		result = dst_key_buildfilename(key, DST_TYPE_PUBLIC, directory,
					       &buf);
		if (result != ISC_R_SUCCESS) {
			fatal("Failed to build public key filename: %s",
			      isc_result_totext(result));
		}

		result = dst_key_tofile(key, DST_TYPE_PUBLIC|DST_TYPE_PRIVATE,
					directory);
		if (result != ISC_R_SUCCESS) {
			key_format(key, keystr, sizeof(keystr));
			fatal("Failed to write key %s: %s", keystr,
			      isc_result_totext(result));
		}

		printf("%s\n", newname);

		isc_buffer_clear(&buf);
		result = dst_key_buildfilename(key, DST_TYPE_PRIVATE, directory,
					       &buf);
		if (result != ISC_R_SUCCESS) {
			fatal("Failed to build private key filename: %s",
			      isc_result_totext(result));
		}
		printf("%s\n", newname);
	}

	dst_key_free(&key);
	dst_lib_destroy();
	isc_hash_destroy();
	cleanup_entropy(&ectx);
	if (verbose > 10)
		isc_mem_stats(mctx, stdout);
	isc_mem_free(mctx, directory);
	isc_mem_destroy(&mctx);

	return (0);
}