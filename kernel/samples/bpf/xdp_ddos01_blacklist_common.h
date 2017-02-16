#ifndef __XDP_DDOS01_BLACKLIST_COMMON_H
#define __XDP_DDOS01_BLACKLIST_COMMON_H

/* Exit return codes */
#define	EXIT_OK			0
#define EXIT_FAIL		1
#define EXIT_FAIL_OPTION	2
#define EXIT_FAIL_XDP		3
#define EXIT_FAIL_KEY_UPDATE	4
#define EXIT_FAIL_MAP		5
#define EXIT_FAIL_IP		102

static int verbose = 1;

static void blacklist_add(int fd, char *ip_string)
{
	__u64 value = 0;
	__u32 key;
	int res;

	/* Convert IP-string into 32-bit network byte-order value */
	res = inet_pton(AF_INET, ip_string, &key);
	if (res <= 0) {
		if (res == 0)
			fprintf(stderr,
			"ERROR: IPv4 \"%s\" not in presentation format\n",
				ip_string);
		else
			perror("inet_pton");
		exit(EXIT_FAIL_IP);
	}

	res = bpf_map_update_elem(fd, &key, &value, BPF_NOEXIST);
	if (res != 0) { /* 0 == success */

		printf("%s() IP:%s key:0x%X errno(%d/%s)",
		       __func__, ip_string, key, errno, strerror(errno));

		if (errno == 17) {
			printf(": Already in blacklist\n");
			return;
		}
		printf("\n");
		exit(EXIT_FAIL_KEY_UPDATE);
	}
	if (verbose)
		printf("%s() IP:%s key:0x%X\n", __func__, ip_string, key);
}


#endif
