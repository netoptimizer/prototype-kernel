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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pwd.h>

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
	{"owner",	required_argument,	NULL, 'o' },
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

#ifndef BPF_FS_MAGIC
# define BPF_FS_MAGIC   0xcafe4a11
#endif

/* Verify BPF-filesystem is mounted on given file path */
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

/* Export and potentially remap map_fd[]
 *
 * map_idx is the corresponding map_fd[index]
 */
int export_map_fd(int map_idx, const char *file, uid_t owner, gid_t group)
{
	int fd_existing;

	/* Verify input map_fd[map_idx] */
	if (map_idx>= MAX_MAPS)
		return EXIT_FAIL_MAP;
	if (map_fd[map_idx] <= 0)
		return EXIT_FAIL_MAP;

	if (bpf_fs_check_path(file) < 0) {
		exit(EXIT_FAIL_MAP_FS);
	}

	/*
	 * Load existing maps via filesystem, if possible first.
	 */
	fd_existing = bpf_obj_get(file);
	if (fd_existing > 0) { /* Great: map file already existed use it */
		// FIXME: Verify map size etc is the same
		close(map_fd[map_idx]); /* is this enough to cleanup map??? */
		map_fd[map_idx] = fd_existing;
		goto out;
	}

	/* Export map as a file */
	if (bpf_obj_pin(map_fd[map_idx], file) != 0) {
		fprintf(stderr, "ERR: Cannot pin map file:%s err(%d):%s\n",
			file, errno, strerror(errno));
		return EXIT_FAIL_MAP;
	}

out:
	/* Change permissions and user for the map file, as this allow
	 * an unpriviliged user to operate the cmdline tool.
	 */
	if (chown(file, owner, group) < 0)
		fprintf(stderr, "WARN: Cannot chown file:%s err(%d):%s\n",
			file, errno, strerror(errno));
	return 0;
}

int main(int argc, char **argv)
{
	struct rlimit r = {RLIM_INFINITY, RLIM_INFINITY};
	bool rm_xdp_prog = false;
	char filename[256];
	int longindex = 0;
	int fd_bpf_prog;
	uid_t owner = -1; /* -1 result in now change of owner */
	gid_t group = -1;
	struct passwd *pwd = NULL;
	int res;
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
		case 'o': /* extract owner and group from username */
			if (!(pwd = getpwnam(optarg))) {
				fprintf(stderr,
					"ERR: unknown owner:%s err(%d):%s\n",
					optarg, errno, strerror(errno));
				goto error;
			}
			owner = pwd->pw_uid;
			group = pwd->pw_gid;
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

	/* Splitup load_bpf_file() */
	fd_bpf_prog = open(filename, O_RDONLY, 0);
	if (fd_bpf_prog < 0) {
		fprintf(stderr,
			"ERR: cannot load eBPF file %s err(%d):%s\n",
			filename, errno, strerror(errno));
		return EXIT_FAIL_BPF;
	}
	if (load_bpf_elf_sections(fd_bpf_prog)) {
		fprintf(stderr, "ERR: %s\n", bpf_log_buf);
		return EXIT_FAIL_BPF_ELF;
	}

	if ((res = export_map_fd(0, file_blacklist, owner, group)))
	    return res;
	if (verbose)
		printf(" - Blacklist     map file: %s\n", file_blacklist);

	if ((res = export_map_fd(1, file_verdict, owner, group)))
		return res;
	if (verbose)
		printf(" - Verdict stats map file: %s\n", file_verdict);

	/* Notice: updated map_fd[i] takes effect now */
	if (load_bpf_relocate_maps_and_attach(fd_bpf_prog)) {
		fprintf(stderr, "ERR: %s\n", bpf_log_buf);
		return EXIT_FAIL_BPF_RELOCATE;
	}
	close(fd_bpf_prog);

	if (!prog_fd[0]) {
		printf("load_bpf_file: %s\n", strerror(errno));
		return 1;
	}

	if (set_link_xdp_fd(ifindex, prog_fd[0]) < 0) {
		printf("link set xdp fd failed\n");
		return EXIT_FAIL_XDP;
	}

	/* Add something to the map as a test */
	blacklist_modify(map_fd[0], "198.18.50.3", ACTION_ADD);

	return EXIT_OK;
}
