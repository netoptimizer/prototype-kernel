/* Copyright(c) 2017 Jesper Dangaard Brouer, Red Hat, Inc.
 */
static const char *__doc__=
 " TC redirect benchmark\n\n"
 "  The bpf-object gets attached via TC cmdline tool\n"
;

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <locale.h>

#include <getopt.h>
#include <net/if.h>
#include <time.h>

#include "libbpf.h"

static int verbose = 1;
static const char *mapfile = "/sys/fs/bpf/tc/globals/egress_ifindex";

static const char *tc_cmd = "tc";
#define CMD_MAX 2048

static const struct option long_options[] = {
	{"help",	no_argument,		NULL, 'h' },
	{"ingress",	required_argument,	NULL, 'i' },
	{"egress",	required_argument,	NULL, 'e' },
	{"ifindex-egress", required_argument,	NULL, 'x' },
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
		printf(" --%-15s", long_options[i].name);
		if (long_options[i].flag != NULL)
			printf(" flag (internal value:%d)",
			       *long_options[i].flag);
		else
			printf("(internal short-option: -%c)",
			       long_options[i].val);
		printf("\n");
	}
	printf("\n");
}

/*
 * TC require attaching the bpf-object via the TC cmdline tool.
 *
 * Manually like:
 *  $TC qdisc   add dev $DEV clsact
 *  $TC filter  add dev $DEV ingress bpf da obj $BPF_OBJ sec ingress_redirect
 *  $TC filter show dev $DEV ingress
 *  $TC filter  del dev $DEV ingress
 *
 * (Trick: tc takes a "replace" command)
 */
static int tc_ingress_attach_bpf(const char* dev, const char* bpf_obj)
{
	char cmd[CMD_MAX];
	int ret = 0;

	memset(&cmd, 0, CMD_MAX);
	snprintf(cmd, CMD_MAX,
		 "%s qdisc replace dev %s clsact",
		 tc_cmd, dev);
	if (verbose) printf(" - Run: %s\n", cmd);
	ret = system(cmd);
	if (ret) {
		fprintf(stderr,
			"ERR(%d): tc cannot attach qdisc hook\n Cmdline:%s\n",
			ret, cmd);
		exit(EXIT_FAILURE);
	}

	memset(&cmd, 0, CMD_MAX);
	snprintf(cmd, CMD_MAX,
		 "%s filter replace dev %s "
		 "ingress bpf da obj %s sec ingress_redirect",
		 tc_cmd, dev, bpf_obj);
	if (verbose) printf(" - Run: %s\n", cmd);
	ret = system(cmd);
	if (ret) {
		fprintf(stderr,
			"ERR(%d): tc cannot attach filter\n Cmdline:%s\n",
			ret, cmd);
		exit(EXIT_FAILURE);
	}

	return ret;
}

static char ingress_ifname[IF_NAMESIZE];
static char egress_ifname[IF_NAMESIZE];
static char buf_ifname[IF_NAMESIZE] = "(unknown-dev)";

bool validate_ifname(const char* input_ifname, char *output_ifname)
{
	size_t len;
	int i;

	len = strlen(input_ifname);
	if (len >= IF_NAMESIZE) {
		return false;
	}
	for (i = 0; i < len; i++) {
		char c = input_ifname[i];

		if (!(isalpha(c) || isdigit(c)))
			return false;
	}
	strncpy(output_ifname, input_ifname, len);
	return true;
}

int main(int argc, char **argv)
{
	int longindex = 0, opt, fd = -1;
	int egress_ifindex = -1;
	int ingress_ifindex = 0;
	int ret = EXIT_SUCCESS;
	int key = 0;

	char bpf_obj[256];
	snprintf(bpf_obj, sizeof(bpf_obj), "%s_kern.o", argv[0]);

	/* Parse commands line args */
	while ((opt = getopt_long(argc, argv, "h",
				  long_options, &longindex)) != -1) {
		switch (opt) {
		case 'x':
			egress_ifindex = atoi(optarg);
			break;
		case 'e':
			if (!validate_ifname(optarg, (char *)&egress_ifname)) {
				fprintf(stderr,
				  "ERR: input --egress ifname invalid\n");
			}
			egress_ifindex = if_nametoindex(egress_ifname);
			if (!(egress_ifindex)){
				fprintf(stderr,
					"ERR: --egress \"%s\" not real dev\n",
					egress_ifname);
				return EXIT_FAILURE;
			}
			break;
		case 'i':
			if (!validate_ifname(optarg, (char *)&ingress_ifname)) {
				fprintf(stderr,
				  "ERR: input --ingress ifname invalid\n");
			}
			if (!(ingress_ifindex= if_nametoindex(ingress_ifname))){
				fprintf(stderr,
					"ERR: --ingress \"%s\" not real dev\n",
					ingress_ifname);
				return EXIT_FAILURE;
			}
			if (verbose)
				printf("TC attach BPF object %s to device %s\n",
				       bpf_obj, ingress_ifname);
			if (tc_ingress_attach_bpf(ingress_ifname, bpf_obj)) {
				fprintf(stderr, "ERR: TC attach failed\n");
				exit(EXIT_FAILURE);
			}

			break;
		case 'h':
		default:
			usage(argv);
			return EXIT_FAILURE;
		}
	}

	fd = bpf_obj_get(mapfile);
	if (fd < 0) {
		fprintf(stderr, "ERROR: cannot open bpf_obj_get(%s): %s(%d)\n",
			mapfile, strerror(errno), errno);
		ret = EXIT_FAILURE;
		goto out;
	}

	/* Only update/set egress port when set via cmdline */
	if (egress_ifindex != -1) {
		ret = bpf_map_update_elem(fd, &key, &egress_ifindex, 0);
		if (ret) {
			perror("ERROR: bpf_map_update_elem");
			ret = EXIT_FAILURE;
			goto out;
		}
		if (verbose)
			printf("Change egress redirect ifindex to: %d\n",
			       egress_ifindex);
	} else {
		/* Read info from map */
		ret = bpf_map_lookup_elem(fd, &key, &egress_ifindex);
		if (ret) {
			perror("ERROR: bpf_map_lookup_elem");
			ret = EXIT_FAILURE;
			goto out;
		}
		if (verbose) {
			if_indextoname(egress_ifindex, buf_ifname);
			printf("Current egress redirect dev: %s ifindex: %d\n",
			       buf_ifname, egress_ifindex);
		}
	}
out:
	if (fd != -1)
		close(fd);
	return ret;
}
