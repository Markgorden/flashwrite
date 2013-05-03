/*
 * IPscan is an mini IP scanner implementation for busybox
 *
 * Copyright 2007 Tito Ragusa <farmatito@tiscali.it>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */

//usage:#define arpscan_trivial_usage
//usage:       "[-c count]"
//usage:#define arpscan_full_usage "\n\n"
//usage:       "Scan all addresses within the netmask range on eth0. Return the IP addresses discovers.\n"
//usage:     "\n	-c <count> return after discover <count> ipaddresses. Default is 1"

#include "libbb.h"

#include <net/if.h>
#include <netinet/if_ether.h>
#include <netpacket/packet.h>
#include <signal.h>

const unsigned char ether_broadcast_addr[]= {0xff,0xff,0xff,0xff,0xff,0xff};
int discoveredCount = 1;
unsigned start;
unsigned end;
unsigned counter;
pid_t receiverPid, senderPid;

#define LTOA(a) inet_ntoa(*((struct in_addr*) &(a)))

static void progress(void)
{
    unsigned s, e, c;
    s = SWAP_BE32(start);
    e = SWAP_BE32(end);
    c = SWAP_BE32(counter);
    fprintf(stderr, "Scanning: current=%s, ", LTOA(c));
    fprintf(stderr, "start=%s, ", LTOA(s));
    fprintf(stderr, "end=%s, ", LTOA(e));
    fprintf(stderr, "%u/%u (%u%%).\n", counter-start, end-start, (counter-start)*100/(end-start));
}

static void signal_handler(int sig UNUSED_PARAM)
{
    switch (sig) {
        case SIGTERM:
            progress();
            fprintf(stderr, "Scan completed.\n");
            exit(0);
        case SIGUSR1:
            progress();
            break;
    }
}

static void nosigusr_handler(int sig UNUSED_PARAM)
{
    return;
}

static void info(unsigned int s, unsigned int e) {
    char *sp = (char *) &s;
    char *ep = (char *) &e;
    fprintf(stderr, "scanning from %u.%u.%u.%u to %u.%u.%u.%u\n", sp[3], sp[2], sp[1], sp[0], ep[3], ep[2], ep[1], ep[0]);
}

static void sendPackets(int fd, struct ether_arp* req, struct sockaddr_ll* addr, unsigned int address, unsigned int netmask) {
    unsigned long j;
    int sizeOfReq, sizeOfAddr;

    sizeOfReq = sizeof(*req);
    sizeOfAddr = sizeof(*addr);
    
    start = (address & netmask);
    end = (address | ~netmask);

    signal(SIGTERM, signal_handler);
    signal(SIGUSR1, signal_handler);
    info(start, end);

    for (counter = start; counter <= end; counter ++) {
        j = SWAP_BE32(counter);
        memcpy(&req->arp_tpa, &j, sizeof(req->arp_tpa));
        if (sendto(fd, req, sizeOfReq, 0, (struct sockaddr*) addr, sizeOfAddr)==-1) {
            bb_error_msg_and_die("Failed to send ARP packet. Error was %s", strerror(errno));
        }
    }
    sleep(1);
    fprintf(stderr, "Scan completed, total number of addresses scanned %d\n", end - start + 1);
    kill(receiverPid, SIGTERM);
}

char packet[64];

static void recvPacket(int fd) {
    ssize_t len;
    struct sockaddr_ll from;
    socklen_t alen = sizeof(from);
    struct arphdr *ah;
    unsigned char *p;
    struct in_addr src_ip;

    while (discoveredCount) {
        len = recvfrom(fd, (char *)packet, sizeof(packet), 0, (struct sockaddr *) &from, &alen);
        if (len < 0) {
            bb_perror_msg("Failed to receive.");
            continue;
        }
        ah = (struct arphdr *) packet;
        p = (unsigned char *) (ah + 1);
            /* Filter out wild packets */
        if (from.sll_pkttype != PACKET_HOST
         && from.sll_pkttype != PACKET_BROADCAST
         && from.sll_pkttype != PACKET_MULTICAST)
            continue;

        /* Only these types are recognized */
        if (ah->ar_op != htons(ARPOP_REQUEST) && ah->ar_op != htons(ARPOP_REPLY))
            continue;

        /* ARPHRD check and this darned FDDI hack here :-( */
        if (ah->ar_hrd != htons(from.sll_hatype)
         && (from.sll_hatype != ARPHRD_FDDI || ah->ar_hrd != htons(ARPHRD_ETHER)))
            continue;

        /* Protocol must be IP. */
        if (ah->ar_pro != htons(ETH_P_IP)
         || (ah->ar_pln != 4)
         || (len < (int)(sizeof(*ah) + 2 * (4 + ah->ar_hln))))
            continue;
        move_from_unaligned32(src_ip.s_addr, p + ah->ar_hln);
        printf("%s\n", inet_ntoa(src_ip));
        discoveredCount --;
    }

    kill(senderPid, SIGTERM);
}

static void send_arp(void) {
    int fd;
    struct ifreq ifr;
    struct ether_arp req;
    struct sockaddr_ll addr={0};
    unsigned int address;
    unsigned int netmask;
    pid_t pid;

    memcpy(ifr.ifr_name, "eth0\0", strlen("eth0") + 1);

    fd = socket(AF_PACKET,SOCK_DGRAM,htons(ETH_P_ARP));
    if (!fd) {
        bb_error_msg_and_die("Failed to open raw socket. Error was %s", strerror(errno));
    }

    // set req 
    req.arp_hrd = htons(ARPHRD_ETHER);
    req.arp_pro = htons(ETH_P_IP);
    req.arp_hln = ETHER_ADDR_LEN;
    req.arp_pln = sizeof(in_addr_t);
    req.arp_op = htons(ARPOP_REQUEST);
    if (ioctl(fd, SIOCGIFHWADDR, &ifr) == -1) {
        bb_error_msg_and_die("Failed to get HW address. Error was %s",strerror(errno));
    }
    memcpy(&req.arp_sha, ifr.ifr_hwaddr.sa_data, ETHER_ADDR_LEN);
    memcpy(&req.arp_tha, ether_broadcast_addr, ETHER_ADDR_LEN);
    
    if (ioctl(fd, SIOCGIFADDR, &ifr) == -1) {
        bb_error_msg_and_die("Failed to get ip address. Error was %s",strerror(errno));
    }
    memcpy(&req.arp_spa, &((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr.s_addr, sizeof(req.arp_spa));
    address = SWAP_BE32(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr.s_addr);

    if (ioctl(fd, SIOCGIFNETMASK, &ifr) == -1) {
        bb_error_msg_and_die("Failed to get netmask. Error was %s",strerror(errno));
    }
    netmask = SWAP_BE32(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr.s_addr);
    
    // set addr 
    if (ioctl(fd, SIOCGIFINDEX, &ifr) == -1) {
        bb_error_msg_and_die("Failed to get network device index. Error was %s",strerror(errno));
    }
    addr.sll_ifindex = ifr.ifr_ifindex;

    addr.sll_family = AF_PACKET;
    addr.sll_halen = ETHER_ADDR_LEN;
    addr.sll_protocol = htons(ETH_P_ARP);
    memcpy(addr.sll_addr, ether_broadcast_addr, ETHER_ADDR_LEN);

    receiverPid = getpid();
    pid = fork();
    if (pid < 0) {
        bb_error_msg_and_die("Cannot fork\n");
    }
    if (pid == 0) {
        senderPid = getpid();
        sendPackets(fd, &req, &addr, address, netmask);
    } else {
        senderPid = pid;
        recvPacket(fd);
    }
}

int arpscan_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int arpscan_main(int argc UNUSED_PARAM, char **argv)
{
    const char *opt_discoveredCount = NULL;

    signal(SIGUSR1, nosigusr_handler);
    getopt32(argv, "c:", &opt_discoveredCount);
    if (opt_discoveredCount) {
        discoveredCount = xatou_range(opt_discoveredCount, 1, 65535);
    }
    argv += optind;

    send_arp();
	return EXIT_SUCCESS;
}
