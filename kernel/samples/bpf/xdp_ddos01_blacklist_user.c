/* Copyright(c) 2017 Jesper Dangaard Brouer, Red Hat, Inc.
 *  Copyright(c) 2017 Andy Gospodarek, Broadcom Limited, Inc.
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
#include <linux/if_link.h>

#include "bpf_load.h"
#include "bpf_util.h"
#include "libbpf.h"

#include "xdp_ddos01_blacklist_common.h"

static char ifname_buf[IF_NAMESIZE];
static char *ifname = NULL;
static int ifindex = -1;

#define NR_MAPS 5
int maps_marked_for_export[MAX_MAPS] = { 0 };

static void remove_xdp_program(int ifindex, const char *ifname, __u32 xdp_flags)
{
	int i;
	fprintf(stderr, "Removing XDP program on ifindex:%d device:%s\n",
		ifindex, ifname);
	if (ifindex > -1)
		set_link_xdp_fd(ifindex, -1, xdp_flags);
	if (unlink(file_blacklist) < 0) {
		printf("WARN: cannot remove map file:%s err(%d):%s\n",
		       file_blacklist, errno, strerror(errno));
	}
	if (unlink(file_verdict) < 0) {
		printf("WARN: cannot remove map file:%s err(%d):%s\n",
		       file_verdict, errno, strerror(errno));
	}
	if (unlink(file_port_blacklist) < 0) {
		printf("WARN: cannot remove map file:%s err(%d):%s\n",
		       file_port_blacklist, errno, strerror(errno));
	}
	for (i = 0; i < DDOS_FILTER_MAX; i++) {
		if (unlink(file_port_blacklist_count[i]) < 0) {
			printf("WARN: cannot remove map file:%s err(%d):%s\n",
			       file_port_blacklist_count[i], errno, strerror(errno));
		}
	}
}

static const struct option long_options[] = {
	{"help",	no_argument,		NULL, 'h' },
	{"remove",	no_argument,		NULL, 'r' },
	{"dev",		required_argument,	NULL, 'd' },
	{"quite",	no_argument,		NULL, 'q' },
	{"owner",	required_argument,	NULL, 'o' },
	{"skb-mode",	no_argument,		NULL, 'S' },
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

/* Load existing map via filesystem, if possible */
int load_map_file(const char *file, struct bpf_map_data *map_data)
{
	int fd;

	if (bpf_fs_check_path(file) < 0) {
		exit(EXIT_FAIL_MAP_FS);
	}

	fd = bpf_obj_get(file);
	if (fd > 0) { /* Great: map file already existed use it */
		// FIXME: Verify map size etc is the same before returning it!
		// data available via map->def.XXX and fdinfo
		if (verbose)
			printf(" - Loaded bpf-map:%-30s from file:%s\n",
			       map_data->name, file);
		return fd;
	}
	return -1;
}

static const char* map_idx_to_export_filename(int idx)
{
	const char *file = NULL;

	/* Mapping map_fd[idx] to export filenames */
	switch (idx) {
	case 0: /* map_fd[0]: blacklist */
		file =   file_blacklist;
		break;
	case 1: /* map_fd[1]: verdict_cnt */
		file =   file_verdict;
		break;
	case 2: /* map_fd[2]: port_blacklist */
		file =   file_port_blacklist;
		break;
	case 3: /* map_fd[3]: port_blacklist_drop_count_tcp */
		file =   file_port_blacklist_count[DDOS_FILTER_TCP];
		break;
	case 4: /* map_fd[4]: port_blacklist_drop_count_udp */
		file =   file_port_blacklist_count[DDOS_FILTER_UDP];
		break;
	default:
		break;
	}
	return file;
}

/* Map callback
 * ------------
 * The bpf-ELF loader (bpf_load.c) got support[1] for a callback, just
 * before creating the map (via bpf_create_map()).  It allow assigning
 * another FD and skips map creation.
 *
 * Using this to load map FD from via filesystem, if possible.  One
 * problem, cannot handle exporting the map here, as creation happens
 * after this step.
 *
 * [1] kernel commit 6979bcc731f9 ("samples/bpf: load_bpf.c make
 * callback fixup more flexible")
 */
void pre_load_maps_via_fs(struct bpf_map_data *map_data, int idx)
{
	/* This callback gets invoked for every map in ELF file */
	const char *file;
	int fd;

	file = map_idx_to_export_filename(idx);
	fd = load_map_file(file, map_data);

	if (fd > 0) {
		/* Makes bpf_load.c skip creating map */
		map_data->fd = fd;
	} else {
		/* When map was NOT loaded from filesystem, then
		 * bpf_load.c will create it. Mark map idx to get
		 * it exported later
		 */
		maps_marked_for_export[idx] = 1;
	}
}

int export_map_idx(int map_idx)
{
	const char* file;

	file = map_idx_to_export_filename(map_idx);

	/* Export map as a file */
	if (bpf_obj_pin(map_fd[map_idx], file) != 0) {
		fprintf(stderr, "ERR: Cannot pin map(%s) file:%s err(%d):%s\n",
			map_data[map_idx].name, file, errno, strerror(errno));
		return EXIT_FAIL_MAP;
	}
	if (verbose)
		printf(" - Export bpf-map:%-30s to   file:%s\n",
		       map_data[map_idx].name, file);
	return 0;
}

void export_maps(void)
{
	int i;

	for (i = 0; i < NR_MAPS; i++) {
		if (maps_marked_for_export[i] == 1)
			export_map_idx(i);
	}
}

void chown_maps(uid_t owner, gid_t group)
{
	const char* file;
	int i;

	for (i = 0; i < NR_MAPS; i++) {
		file = map_idx_to_export_filename(i);

		/* Change permissions and user for the map file, as this allow
		 * an unpriviliged user to operate the cmdline tool.
		 */
		if (chown(file, owner, group) < 0)
			fprintf(stderr,
				"WARN: Cannot chown file:%s err(%d):%s\n",
				file, errno, strerror(errno));
	}
}

int main(int argc, char **argv)
{
	struct rlimit r = {RLIM_INFINITY, RLIM_INFINITY};
	bool rm_xdp_prog = false;
	struct passwd *pwd = NULL;
	__u32 xdp_flags = 0;
	char filename[256];
	int longindex = 0;
	uid_t owner = -1; /* -1 result in no-change of owner */
	gid_t group = -1;
	int opt;

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);

	/* Parse commands line args */
	while ((opt = getopt_long(argc, argv, "hSrqd:",
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
		case 'S':
			xdp_flags |= XDP_FLAGS_SKB_MODE;
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
		remove_xdp_program(ifindex, ifname, xdp_flags);
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

	/* Load bpf-ELF file with callback for loading maps via filesystem */
	if (load_bpf_file_fixup_map(filename, pre_load_maps_via_fs)) {
		fprintf(stderr, "ERR in load_bpf_file(): %s", bpf_log_buf);
		return EXIT_FAIL;
	}

	if (!prog_fd[0]) {
		printf("load_bpf_file: %s\n", strerror(errno));
		return 1;
	}

	/* Export maps that were not loaded from filesystem */
	export_maps();

	if (owner >= 0)
		chown_maps(owner, group);

	if (set_link_xdp_fd(ifindex, prog_fd[0], xdp_flags) < 0) {
		printf("link set xdp fd failed\n");
		return EXIT_FAIL_XDP;
	}

	/* Add something to the map as a test */
	blacklist_modify(map_fd[0], "198.18.50.3", ACTION_ADD);
	blacklist_port_modify(map_fd[2], map_fd[4], 80, ACTION_ADD, IPPROTO_UDP);

	return EXIT_OK;
}
