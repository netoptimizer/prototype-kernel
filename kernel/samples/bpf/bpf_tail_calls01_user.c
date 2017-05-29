/* Copyright(c) 2017 Jesper Dangaard Brouer, Red Hat, Inc.
 */
static const char *__doc__= " Test of bpf_tail_call from XDP program\n\n"
	"Notice: This is a non-functional test program\n"
	"        for exercising different bpf code paths in the kernel\n";

#include <getopt.h>
#include <signal.h>
#include <net/if.h>
#include <unistd.h>
#include <linux/if_link.h>

#include "libbpf.h"
#include "bpf_load.h"
#include "bpf_util.h"

static int ifindex = -1;
static char ifname_buf[IF_NAMESIZE];
static char *ifname = NULL;
static __u32 xdp_flags = 0;
static bool debug = false;

/* Exit return codes */
#define EXIT_OK                 0
#define EXIT_FAIL               1
#define EXIT_FAIL_OPTION        2
#define EXIT_FAIL_XDP           3
#define EXIT_FAIL_MAP		20

static void int_exit(int sig)
{
	fprintf(stderr,
		"Interrupted: Removing XDP program on ifindex:%d device:%s\n",
		ifindex, ifname);
	if (ifindex > -1)
		set_link_xdp_fd(ifindex, -1, xdp_flags);
	exit(EXIT_OK);
}

static const struct option long_options[] = {
	{"help",	no_argument,		NULL, 'h' },
	{"dev",		required_argument,	NULL, 'd' },
	{"debug",	no_argument,		NULL, 'D' },
	{"skbmode",     no_argument,		NULL, 'S' },
	{0, 0, NULL,  0 }
};

static void usage(char *argv[])
{
	int i;
	printf("\nDOCUMENTATION:\n%s\n", __doc__);
	printf("\n");
	printf(" Usage: %s (options-see-below)\n",
	       argv[0]);
	printf(" Listing options:\n");
	for (i = 0; long_options[i].name != 0; i++) {
		printf(" --%-12s", long_options[i].name);
		if (long_options[i].flag != NULL)
			printf(" flag (internal value:%d)",
			       *long_options[i].flag);
		else
			printf(" short-option: -%c",
			       long_options[i].val);
		printf("\n");
	}
	printf("\n");
}

/* Helper for adding prog to prog_map */
void jmp_table_add_prog(int map_jmp_table_idx, int idx, int prog_idx)
{
	int jmp_table_fd = map_fd[map_jmp_table_idx];
	int prog = prog_fd[prog_idx];
	int err;

	if (prog == 0) {
		printf("ERR: Invalid zero-FD prog_fd[%d]=%d,"
		       " did bpf_load.c fail loading program?!?\n",
		       prog_idx, prog);
		exit(EXIT_FAIL_MAP);
	}

	err = bpf_map_update_elem(jmp_table_fd, &idx, &prog, 0);
	if (err) {
		printf("ERR(%d/%d): Fail add prog_fd[%d]=%d to jmp_table%d i:%d\n",
		       err, errno, prog_idx, prog, map_jmp_table_idx+1, idx);
		exit(EXIT_FAIL_MAP);
	}
	if (debug) {
		printf("Add XDP prog_fd[%d]=%d to jmp_table%d idx:%d\n",
		       prog_idx, prog, map_jmp_table_idx+1, idx);
	}
}

int main(int argc, char **argv)
{
	char filename[256];
	int longindex = 0;
	int opt, i;

	/* Corresponding map_fd[index] for jump tables aka tail calls */
	int jmp_table1 = 0;
	int jmp_table2 = 1;
	int jmp_table3 = 2;

	/*
	 * WARNING: There were an issue in bpf_load.c that caused bpf
	 * prog section order in prog_fd[] to get mixed up (if prog
	 * didn't reference a map)
	 *
	 * Corresponding prog_fd[index] for prog section tail calls.
	 */
	int prog_xdp_1 = 1;
	int prog_xdp_5 = 2;
	int prog_xdp_unrelated = 3;

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);
	/* Parse commands line args */
	while ((opt = getopt_long(argc, argv, "hd:",
				  long_options, &longindex)) != -1) {
		switch (opt) {
		case 'd':
			if (strlen(optarg) >= IF_NAMESIZE) {
				fprintf(stderr, "ERR: --dev name too long\n");
				goto error;
			}
			ifname = (char *)&ifname_buf;
			strncpy(ifname, optarg, IF_NAMESIZE);
			ifindex = if_nametoindex(ifname);
			if (ifindex == 0) {
				fprintf(stderr,
					"ERR: --dev name unknown err(%d):%s\n",
					errno, strerror(errno));
				goto error;
			}
			break;
		case 'S':
			xdp_flags |= XDP_FLAGS_SKB_MODE;
			break;
		case 'D':
			debug = true;
			break;
		case 'h':
		error:
		default:
			usage(argv);
			return EXIT_FAIL_OPTION;
		}
	}
	/* Required options */
	if (ifindex == -1) {
		fprintf(stderr, "ERR: required option --dev missing");
		usage(argv);
		return EXIT_FAIL_OPTION;
	}

	if (load_bpf_file(filename)) {
		fprintf(stderr, "ERR in load_bpf_file(): %s", bpf_log_buf);
		return EXIT_FAIL;
	}
	if (!prog_fd[0]) {
		fprintf(stderr, "ERR: load_bpf_file: %s\n", strerror(errno));
		return EXIT_FAIL;
	}

	printf("INFO: bpf ELF file(%s) contained %d program(s)\n",
	       filename, prog_cnt);

	if (debug) {
		extern int prog_array_fd;

		printf("DEBUG: prog_array_fd:%d\n", prog_array_fd);
	}

	/* For XDP bpf_load.c seems not to implement automatic
	 * populating the prog_array.
	 *
	 * Do this manually.  The prog_array_fd does contain the FD
	 * but it is not default exported.  Thus, instead rely on the
	 * order of SEC map and prog definitions.
	 */
	if (1) {
		jmp_table_add_prog(jmp_table1, 1, prog_xdp_1);
		jmp_table_add_prog(jmp_table1, 5, prog_xdp_5);
	}
	/* Notice populating jmp_table is done _before_ attaching the
	 * main XDP program to a specific device.
	 *
	 * DEVEL: As I'm working on locking down prog_array features
	 * changes after a XDP program have been associated with a
	 * device.
	 */
	if (1) { /* Notice jmp_table2 (number 2) */
		for (i = 40; i < 50; i++)
			jmp_table_add_prog(jmp_table2, i, prog_xdp_unrelated);
	}

	if (debug) {
		printf("map_fd[] jmp_table file descriptor mapping:\n");
		for (i = 0; i < 3; i++)
			printf(" jmp_table map_fd[%d]=fd:%d\n", i, map_fd[i]);
	}

	/* Attach XDP program */
	if (set_link_xdp_fd(ifindex, prog_fd[0], xdp_flags) < 0) {
		fprintf(stderr, "ERR: link set xdp fd failed\n");
		return EXIT_FAIL_XDP;
	}

	/* Remove XDP program when program is interrupted or killed */
	signal(SIGINT,  int_exit);
	signal(SIGTERM, int_exit);

	/* Notice, after XDP prog have been attached, the features
	 * have been "locked down" (in RFC patch).  Adding something
	 * to a jmp_table will result in runtime validation.
	 */

	if (1) {
		/* Populate jmp_table3 with some prog */
		for (i = 30; i < 32; i++) {
			jmp_table_add_prog(jmp_table3, i, prog_xdp_unrelated);
		}
	}

	if (1) {
		/* Take over jmp entry 5 */
		if (debug) {
			int delay = 2;
			printf("Delay: %d sec, before taking over idx 5\n",
			       delay);
			sleep(delay);
		}
		jmp_table_add_prog(jmp_table1, 5, prog_xdp_unrelated);
	}

	if (debug) {
		printf("Debug-mode reading trace pipe (fix #define DEBUG)\n");
		read_trace_pipe();
	}

	printf("Goodbye\n");
	int_exit(SIGSTOP);
}
