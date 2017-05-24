/* Copyright(c) 2017 Jesper Dangaard Brouer, Red Hat, Inc.
 */
static const char *__doc__= " Test of bpf_tail_call from XDP program\n\n"
	"Notice: This is a non-functional test program\n"
	"        for exercising different bpf code paths in the kernel\n";

#include <getopt.h>
#include <signal.h>
#include <net/if.h>

#include <linux/if_link.h>

#include "libbpf.h"
#include "bpf_load.h"
#include "bpf_util.h"

static int ifindex = -1;
static char ifname_buf[IF_NAMESIZE];
static char *ifname = NULL;
static __u32 xdp_flags = 0;

/* Exit return codes */
#define EXIT_OK                 0
#define EXIT_FAIL               1
#define EXIT_FAIL_OPTION        2
#define EXIT_FAIL_XDP           3

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

int main(int argc, char **argv)
{
	char filename[256];
	bool debug = false;
	int longindex = 0;
	int opt;

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
		int jmp_table_fd = map_fd[0];
		int xdp_prog1 = prog_fd[1];
		int xdp_prog5 = prog_fd[2];
		int idx; /* index in tail call jmp_table */
		int err;

		if (debug)
			printf("XXX: FDs xdp_prog0:%d xdp_prog5:%d jmp:%d\n",
			       xdp_prog1, xdp_prog5, jmp_table_fd);

		idx = 1;
		err = bpf_map_update_elem(jmp_table_fd, &idx, &xdp_prog1, 0);
		if (err) {
			printf("ERR: Fail add jmp to xdp_prog1 err:%d\n", err);
		}
		idx = 5;
		err = bpf_map_update_elem(jmp_table_fd, &idx, &xdp_prog5, 0);
		if (err) {
			printf("ERR: Fail add jmp to xdp_prog5 err:%d\n", err);
		}
	}
	/* Notice populating jmp_table is done _before_ attaching the
	 * main XDP program to a specific device.
	 *
	 * DEVEL: As I'm working on locking down prog_array features
	 * changes after a XDP program have been associated with a
	 * device.
	 */
	if (0) { /* Notice jmp_table2 (number 2) */
		int jmp_table2 = map_fd[1];
		int prog = prog_fd[3]; /* xdp_another_tail_call */
		int i; /* index in tail call jmp_table2 */
		int err;

		for (i = 40; i < 50; i++) {
			err = bpf_map_update_elem(jmp_table2, &i, &prog, 0);
			if (err)
				printf("ERR: Fail add jmp_table2 err:%d\n",err);
		}
	}

	/* Attach XDP program */
	if (set_link_xdp_fd(ifindex, prog_fd[0], xdp_flags) < 0) {
		fprintf(stderr, "ERR: link set xdp fd failed\n");
		return EXIT_FAIL_XDP;
	}

	/* Notice, after XDP prog have been attached, the features
	 * have been "locked down" (in RFC patch).  Adding something
	 * to a jmp_table will result in runtime validation.
	 */
	if (1) {
		int jmp_table2 = map_fd[1];
		int prog = prog_fd[3]; /* xdp_another_tail_call */
		int i; /* index in tail call jmp_table2 */
		int err;

		for (i = 30; i < 32; i++) {
			err = bpf_map_update_elem(jmp_table2, &i, &prog, 0);
			if (err)
				printf("ERR: Fail add jmp_table2 err:%d\n",err);
		}
	}

	/* Remove XDP program when program is interrupted or killed */
	signal(SIGINT,  int_exit);
	signal(SIGTERM, int_exit);

	if (debug) {
		printf("Debug-mode reading trace pipe (fix #define DEBUG)\n");
		read_trace_pipe();
	}

	printf("Goodbye\n");
	int_exit(SIGSTOP);
}
