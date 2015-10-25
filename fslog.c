/*
 *  exported-global - test exported relay file ops with a global buffer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) 2005 - Tom Zanussi (zanussi@us.ibm.com), IBM Corp
 *
 * This example creates a global relay file in the debugfs filesystem
 * and uses read(2) to read from it.  It amounts to pretty much the
 * simplest possible usage of relayfs for ad hoc kernel logging.
 * Relayfs itself should be insmod'ed or configured in, but doesn't
 * need to be mounted.
 *
 * Usage:
 *
 * modprobe relayfs
 * mount -t debugfs debugfs /debug
 * insmod ./exported-global-mod.ko
 * ./exported-global [-b subbuf-size -n n_subbufs]
 *
 * captured output will appear on stdout
 *
 */
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <linux/types.h>

/* name of global relay file */
static char *relay_file_name = NULL;

/* internal variables */
static int relay_file;
static int parse = 0;
static int verbose = 0;

enum {
	FR_DIR,
	FR_HIT,
	FR_UPTODATE,
	FR_DIRTY,
	FR_REF,
	FR_ACTIVE,
	FR_WRITEBACK,
	FR_RECLAIM
};

struct fslog_record {
	__u32 fr_no;
	__u32 fr_time;
	__u32 fr_dev;
	__u32 fr_ino;
	__u32 fr_gen;
	__u32 fr_index;
	__u16 fr_pid;
	__u8  fr_type;
	__u8  fr_bits;
	__u32 fr_pad;
	char  fr_comm[16];
	char  fr_name[16];
};

#define ARRAY_SIZE(a) (sizeof(a)/sizeof(a)[0])

#define F(mask, bit, ch) ((mask) & (1 << (bit)) ? ch : ".")

static void process_data(const void *buf, int len)
{
	const struct fslog_record *rec;

	for (rec = buf; len > 0; rec++, len -= sizeof *rec) {
		__u8 bits;

		bits = rec->fr_bits;
		printf("%8.8x %8.8x %4.4x %*.*s %8.8x %8.8x %8.8x "
		       "%*.*s %8.8x %c %s%s%s%s%s%s%s%s\n",
		       rec->fr_no, rec->fr_time, rec->fr_pid,
		       ARRAY_SIZE(rec->fr_comm), ARRAY_SIZE(rec->fr_comm),
		       rec->fr_comm, rec->fr_dev, rec->fr_ino, rec->fr_gen,
		       ARRAY_SIZE(rec->fr_name), ARRAY_SIZE(rec->fr_name),
		       rec->fr_name, rec->fr_index, rec->fr_type,
		       F(bits, FR_DIR,       "D"),
		       F(bits, FR_HIT,       "+"),
		       F(bits, FR_UPTODATE,  "u"),
		       F(bits, FR_DIRTY,     "d"),
		       F(bits, FR_REF,       "r"),
		       F(bits, FR_ACTIVE,    "a"),
		       F(bits, FR_WRITEBACK, "w"),
		       F(bits, FR_RECLAIM,   "c"));
	}
}

static void usage(void)
{
}

int main(int argc, char **argv)
{
	char buf[sizeof(struct fslog_record) * 1024];
	int rc;
	int opt;
	int intr;
	int regular = 0;
	int flags;

	intr = 0;
        do {
                opt = getopt(argc, argv, "vf:per");
                switch (opt) {
                case 'v':
                        verbose++;
                case -1:
                        break;
                case 'p':
			parse = 1;
                        break;
                case 'e':
			intr = 1;
                        break;
		case 'r':
			regular = 1;
			break;
                case 'f':
                        relay_file_name = optarg;
                        break;
                case '?':
                default:
                        fprintf(stderr, "Unable to parse options.");
                case 'h':
                        usage();
                        return 0;
                }
        } while (opt != -1);

	flags = regular ? 0 : O_NONBLOCK;

	if (relay_file_name != NULL) {
		relay_file = open(relay_file_name, O_RDONLY | flags);
		if (relay_file < 0) {
			printf("Couldn't open relay file %s: errcode = %s\n",
			       relay_file_name, strerror(errno));
			return -1;
		}
	} else
		relay_file = 0;

	do {
		rc = read(relay_file, buf, sizeof(struct fslog_record) * 1024);
		if (rc < 0) {
			if (errno != EAGAIN)
				perror("read");
			if (intr)
				break;
		} else if (rc > 0) {
			if (parse)
				process_data(buf, rc);
			else
				write(1, buf, rc);
		} else if (regular)
			break;
		else
			usleep(1000);
	} while (1);

	close(relay_file);
	return 0;
}

