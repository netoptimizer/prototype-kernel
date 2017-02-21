/* Copyright(c) 2017 Jesper Dangaard Brouer, Red Hat, Inc.
 */
static const char *__doc__=
 " XDP: DDoS protection via IPv4 blacklist\n"
 "\n"
 "This program loads the XDP eBPF program into the kernel.\n"
 "Use the cmdline tool for add/removing source IPs to the blacklist\n"
 "and read statistics.\n"
 ;

#include <linux/bpf.h>

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include <sys/resource.h>
#include <getopt.h>
#include <net/if.h>

#include <sys/statfs.h>
#include <libgen.h>  /* dirname */

#include <arpa/inet.h>

#include "bpf_load.h"
#include "bpf_util.h"
#include "libbpf.h"

#include "xdp_ddos01_blacklist_common.h"

static char ifname_buf[IF_NAMESIZE];
static char *ifname = NULL;
static int ifindex = -1;

static void remove_xdp_program(int ifindex, const char *ifname)
{
	fprintf(stderr, "Removing XDP program on ifindex:%d device:%s\n",
		ifindex, ifname);
	if (ifindex > -1)
		set_link_xdp_fd(ifindex, -1);
	if (unlink(file_blacklist) < 0) {
		printf("WARN: cannot remove map file:%s err(%d):%s\n",
		       file_blacklist, errno, strerror(errno));
	}
	if (unlink(file_verdict) < 0) {
		printf("WARN: cannot remove map file:%s err(%d):%s\n",
		       file_verdict, errno, strerror(errno));
	}
}

static const struct option long_options[] = {
	{"help",	no_argument,		NULL, 'h' },
	{"remove",	no_argument,		NULL, 'r' },
	{"dev",		required_argument,	NULL, 'd' },
	{"quite",	no_argument,		NULL, 'q' },
	{0, 0, NULL,  0 }
};

static void usage(char *argv[])
{
	int i;
	printf("\nDOCUMENTATION:\n%s\n", __doc__);
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

// TODO: change permissions and user for the map file

// TODO: Detect if bpf filesystem is not mounted

#ifndef BPF_FS_MAGIC
# define BPF_FS_MAGIC   0xcafe4a11
#endif

static int bpf_fs_check_path(const char *path)
{
	struct statfs st_fs;
	char *dname, *dir;
	int err = 0;

	if (path == NULL)
		return -EINVAL;

	dname = strdup(path);
	if (dname == NULL)
		return -ENOMEM;

	dir = dirname(dname);
	if (statfs(dir, &st_fs)) {
		fprintf(stderr, "ERR: failed to statfs %s: (%d)%s\n",
			dir, errno, strerror(errno));
		err = -errno;
	}
	free(dname);

	if (!err && st_fs.f_type != BPF_FS_MAGIC) {
		fprintf(stderr,
			"ERR: specified path %s is not on BPF FS\n\n"
			" You need to mount the BPF filesystem type like:\n"
			"  mount -t bpf bpf /sys/fs/bpf/\n\n",
			path);
		err = -EINVAL;
	}

	return err;
}

bool export_map(int fd, const char *file)
{
	int retries = 2;

	if (bpf_fs_check_path(file) < 0) {
		exit(EXIT_FAIL_MAP_FS);
	}
retry:
	if (!retries--)
		return EXIT_FAIL_MAP;

	/* Export map as a file */
	if (bpf_obj_pin(fd, file) != 0) {
		if (errno == 17) {
			/* File exists, remove it as this bpf XDP
			 * program force-fully overwrite/swap existing
			 * XDP prog.
			 */
			if (unlink(file) < 0) {
				fprintf(stderr, "ERR: cannot cleanup old map"
				       "file:%s err(%d):%s\n",
				       file, errno, strerror(errno));
				exit(EXIT_FAIL_MAP);
			}
			fprintf(stderr,
				"WARN: Deleted previous map file: %s\n", file);
			/* FIXME: shouldn't we let an existing
			 * blacklist map "survive", and feed it to the
			 * eBPF program?
			 */
			goto retry;
		} else {
			fprintf(stderr,
				"ERR: Cannot pin map file:%s err(%d):%s\n",
			       file, errno, strerror(errno));
			return EXIT_FAIL_MAP;
		}
	}
	return true;
}

int main(int argc, char **argv)
{
	struct rlimit r = {RLIM_INFINITY, RLIM_INFINITY};
	bool rm_xdp_prog = false;
	char filename[256];
	int longindex = 0;
	int opt;

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);

	/* Parse commands line args */
	while ((opt = getopt_long(argc, argv, "hrqd:",
				  long_options, &longindex)) != -1) {
		switch (opt) {
		case 'q':
			verbose = 0;
			break;
		case 'r':
			rm_xdp_prog = true;
			break;
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
		case 'h':
		error:
		default:
			usage(argv);
			return EXIT_FAIL_OPTION;
		}
	}
	/* Required options */
	if (ifindex == -1) {
		printf("ERR: required option --dev missing");
		usage(argv);
		return EXIT_FAIL_OPTION;
	}
	if (rm_xdp_prog) {
		remove_xdp_program(ifindex, ifname);
		return EXIT_OK;
	}
	if (verbose) {
		printf("Documentation:\n%s\n", __doc__);
		printf(" - Attached to device:%s (ifindex:%d)\n",
		       ifname, ifindex);
	}

	/* Increase resource limits */
	if (setrlimit(RLIMIT_MEMLOCK, &r)) {
		perror("setrlimit(RLIMIT_MEMLOCK, RLIM_INFINITY)");
		return 1;
	}

	if (load_bpf_file(filename)) {
		printf("%s", bpf_log_buf);
		return 1;
	}

	if (!prog_fd[0]) {
		printf("load_bpf_file: %s\n", strerror(errno));
		return 1;
	}

	export_map(map_fd[0], file_blacklist);
	if (verbose)
		printf(" - Blacklist exported to file: %s\n", file_blacklist);

	export_map(map_fd[1], file_verdict);
	if (verbose)
		printf(" - Verdict stats exported to file: %s\n", file_verdict);

	if (set_link_xdp_fd(ifindex, prog_fd[0]) < 0) {
		printf("link set xdp fd failed\n");
		return EXIT_FAIL_XDP;
	}

	/* Add something to the map as a test */
	blacklist_add(map_fd[0], "198.18.50.3");

	return EXIT_OK;
}
