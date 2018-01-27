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

#define CMD_MAX 	2048
#define CMD_MAX_TC	256
static char tc_cmd[CMD_MAX_TC] = "tc";

static const struct option long_options[] = {
	{"help",	no_argument,		NULL, 'h' },
	{"ingress",	required_argument,	NULL, 'i' },
	{"egress",	required_argument,	NULL, 'e' },
	{"ifindex-egress", required_argument,	NULL, 'x' },
	/* Allow specifying tc cmd via argument */
	{"tc-cmd",	required_argument,	NULL, 't' },
	/* HINT assign: optional_arguments with '=' */
	{"list",	optional_argument,	NULL, 'l' },
	{"remove",	optional_argument,	NULL, 'r' },
	{"quiet",	no_argument,		NULL, 'q' },
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
 *  $TC qdisc   del dev $DEV clsact
 *  $TC qdisc   add dev $DEV clsact
 *  $TC filter  add dev $DEV ingress bpf da obj $BPF_OBJ sec ingress_redirect
 *  $TC filter show dev $DEV ingress
 *  $TC filter  del dev $DEV ingress
 *
 * (The tc "replace" command does not seem to work as expected)
 */
static int tc_ingress_attach_bpf(const char* dev, const char* bpf_obj)
{
	char cmd[CMD_MAX];
	int ret = 0;

	/* Step-1: Delete clsact, which also remove filters */
	memset(&cmd, 0, CMD_MAX);
	snprintf(cmd, CMD_MAX,
		 "%s qdisc del dev %s clsact 2> /dev/null",
		 tc_cmd, dev);
	if (verbose) printf(" - Run: %s\n", cmd);
	ret = system(cmd);
	if (!WIFEXITED(ret)) {
		fprintf(stderr,
			"ERR(%d): Cannot exec tc cmd\n Cmdline:%s\n",
			WEXITSTATUS(ret), cmd);
		exit(EXIT_FAILURE);
	} else if (WEXITSTATUS(ret) == 2) {
		/* Unfortunately TC use same return code for many errors */
		if (verbose) printf(" - (First time loading clsact?)\n");
	}

	/* Step-2: Attach a new clsact qdisc */
	memset(&cmd, 0, CMD_MAX);
	snprintf(cmd, CMD_MAX,
		 "%s qdisc add dev %s clsact",
		 tc_cmd, dev);
	if (verbose) printf(" - Run: %s\n", cmd);
	ret = system(cmd);
	if (ret) {
		fprintf(stderr,
			"ERR(%d): tc cannot attach qdisc hook\n Cmdline:%s\n",
			WEXITSTATUS(ret), cmd);
		exit(EXIT_FAILURE);
	}

	/* Step-3: Attach BPF program/object as ingress filter */
	memset(&cmd, 0, CMD_MAX);
	snprintf(cmd, CMD_MAX,
		 "%s filter add dev %s "
		 "ingress prio 1 handle 1 bpf da obj %s sec ingress_redirect",
		 tc_cmd, dev, bpf_obj);
	if (verbose) printf(" - Run: %s\n", cmd);
	ret = system(cmd);
	if (ret) {
		fprintf(stderr,
			"ERR(%d): tc cannot attach filter\n Cmdline:%s\n",
			WEXITSTATUS(ret), cmd);
		exit(EXIT_FAILURE);
	}

	return ret;
}

static int tc_list_ingress_filter(const char* dev)
{
	char cmd[CMD_MAX];
	int ret = 0;

	memset(&cmd, 0, CMD_MAX);
	snprintf(cmd, CMD_MAX,
		 "%s filter show dev %s ingress",
		 tc_cmd, dev);
	if (verbose) printf(" - Run: %s\n", cmd);
	ret = system(cmd);
	if (ret) {
		fprintf(stderr,
			"ERR(%d): tc cannot list filters\n Cmdline:%s\n",
			ret, cmd);
		exit(EXIT_FAILURE);
	}
	return ret;
}

static int tc_remove_ingress_filter(const char* dev)
{
	char cmd[CMD_MAX];
	int ret = 0;

	memset(&cmd, 0, CMD_MAX);
	snprintf(cmd, CMD_MAX,
		 /* Remove all ingress filters on dev */
		 "%s filter delete dev %s ingress",
		 /* Alternatively could remove specific filter handle:
		 "%s filter delete dev %s ingress prio 1 handle 1 bpf",
		 */
		 tc_cmd, dev);
	if (verbose) printf(" - Run: %s\n", cmd);
	ret = system(cmd);
	if (ret) {
		fprintf(stderr,
			"ERR(%d): tc cannot remove filters\n Cmdline:%s\n",
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
	bool list_ingress_tc_filter = false;
	bool remove_ingress_tc_filter = false;
	int longindex = 0, opt, fd = -1;
	int egress_ifindex = -1;
	int ingress_ifindex = 0;
	int ret = EXIT_SUCCESS;
	int key = 0;
	size_t len;

	char bpf_obj[256];
	snprintf(bpf_obj, sizeof(bpf_obj), "%s_kern.o", argv[0]);

	memset(ingress_ifname, 0, IF_NAMESIZE); /* Can be used uninitialized */

	/* Parse commands line args */
	while ((opt = getopt_long(argc, argv, "hq",
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
			break;
		case 'l':
			/* --list use --ingress ifname if specified */
			if (optarg &&
			    !validate_ifname(optarg,(char *)&ingress_ifname)) {
				fprintf(stderr,
					"ERR: input --list=ifname invalid\n");
				return EXIT_FAILURE;
			}
			if (strlen(ingress_ifname) == 0) {
				fprintf(stderr,
					"ERR: need input --list=ifname\n");
				return EXIT_FAILURE;
			}
			list_ingress_tc_filter = true;
			break;
		case 'r':
			/* --remove use --ingress ifname if specified */
			if (optarg &&
			    !validate_ifname(optarg,(char *)&ingress_ifname)) {
				fprintf(stderr,
					"ERR: input --remove=ifname invalid\n");
				return EXIT_FAILURE;
			}
			if (strlen(ingress_ifname) == 0) {
				fprintf(stderr,
					"ERR: need input --list=ifname\n");
				return EXIT_FAILURE;
			}
			remove_ingress_tc_filter = true;
			break;
		case 't':
			len = strlen(optarg);
			if (len >= CMD_MAX_TC) {
				return EXIT_FAILURE;
			}
			strncpy(tc_cmd, optarg, len);
			break;
		case 'q':
			verbose = 0;
			break;
		case 'h':
		default:
			usage(argv);
			return EXIT_FAILURE;
		}
	}

	if (ingress_ifindex) {
		if (verbose)
			printf("TC attach BPF object %s to device %s\n",
			       bpf_obj, ingress_ifname);
		if (tc_ingress_attach_bpf(ingress_ifname, bpf_obj)) {
			fprintf(stderr, "ERR: TC attach failed\n");
			exit(EXIT_FAILURE);
		}
	}

	if (list_ingress_tc_filter) {
		if (verbose)
			printf("TC list ingress filters on device %s\n",
			       ingress_ifname);
		tc_list_ingress_filter(ingress_ifname);
	}

	if (remove_ingress_tc_filter) {
		if (verbose)
			printf("TC remove ingress filters on device %s\n",
			       ingress_ifname);
		tc_remove_ingress_filter(ingress_ifname);
		return EXIT_SUCCESS;
	}

	fd = bpf_obj_get(mapfile);
	if (fd < 0) {
		fprintf(stderr, "ERROR: cannot open bpf_obj_get(%s): %s(%d)\n",
			mapfile, strerror(errno), errno);
		usage(argv);
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
