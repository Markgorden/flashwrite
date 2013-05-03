/*
 * IPscan is an mini IP scanner implementation for busybox
 *
 * Copyright 2007 Tito Ragusa <farmatito@tiscali.it>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */

//usage:#define ipscan_trivial_usage
//usage:       "[-p PORT] [-t TIMEOUT] file"
//usage:#define ipscan_full_usage "\n\n"
//usage:       "Scan a list of ip addresses for a port, print the ip address with the port open.\n"
//usage:     "\n	-p	Scan this port (default 515)"
//usage:     "\n	file contains a list of space-separated ip addresses"

#include "libbb.h"

//#define DEBUG_IPSCAN 1

/* debugging */
#ifdef DEBUG_IPSCAN
#define DMSG(...) bb_error_msg(__VA_ARGS__)
#define DERR(...) bb_perror_msg(__VA_ARGS__)
#else
#define DMSG(...) ((void)0)
#define DERR(...) ((void)0)
#endif

#define MONOTONIC_US() ((unsigned)monotonic_us())

#define max 1000
#define buf_size 16

int ipscan_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int ipscan_main(int argc UNUSED_PARAM, char **argv)
{
	len_and_sockaddr *lsap;
    int sks[max];
    int i;
	unsigned opt;
    const char *opt_port = "515";
    unsigned port;
    char ips[max][buf_size];
    FILE *f;
    int total = 0;
    int block_total;
    struct in_addr inp;
    int ret = EXIT_SUCCESS;
	unsigned start;

	opt = getopt32(argv, "p:", &opt_port);
	argv += optind;

    if (*argv == NULL) {
        f = stdin;
    } else if ((f = fopen(*argv, "r")) == NULL) {
        bb_error_msg("Failed to open ip address list file %s.", *argv);
        return EXIT_FAILURE;
    }
    port = xatou_range(opt_port, 1, 65535);
    do {
		start = MONOTONIC_US();
        for (i = 0; i < max; i ++) {
            if (fgets(ips[i], buf_size, f) == NULL) {
                goto test_write;
                break;
            }
            if (!inet_aton(ips[i], &inp)) {
                bb_error_msg("Invalid IP address %s", ips[i]);
                ips[i][0] = 0;
                continue;
            }
            lsap = xhost2sockaddr(ips[i], port);
            set_nport(&lsap->u.sa, htons(port));
            sks[i] = xsocket(lsap->u.sa.sa_family, SOCK_STREAM, 0);
            /* We need unblocking socket so we don't need to wait for ETIMEOUT. */
            /* Nonblocking connect typically "fails" with errno == EINPROGRESS */
            ndelay_on(sks[i]);

            if (connect(sks[i], &lsap->u.sa, lsap->len) == 0) {
                /* Unlikely, for me even localhost fails :) */
                bb_info_msg("%s", ips[i]);
                goto out;
                break;
            }
        }
test_write:
        while (MONOTONIC_US() - start < 1000000) {
			usleep(10000);
        }
        block_total = i;
        for (i = 0; i < block_total; i++) {
            if (! ips[i][0]) {
                continue;
            }
            DMSG("checking ip = %s\n", ips[i]);
            if (write(sks[i], " ", 1) >= 0) { /* We were able to write to the socket */
                bb_info_msg("%s", ips[i]);
                goto out;
            }
            close(sks[i]);
        }
        total += i;
        if ( ! (total % max)) {
            bb_error_msg("scanned %d addresses ...", total);
        }
    } while (block_total);
    ret = EXIT_FAILURE;

out:
	if (ENABLE_FEATURE_CLEAN_UP && lsap) free(lsap);
    fclose(f);
	return ret;
}
