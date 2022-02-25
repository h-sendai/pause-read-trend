#ifndef _FLOW_CTRL_PAUSE
#define _FLOW_CTRL_PAUSE

/*
Pause frame
+---------+--------+---------+-------+--------------+---------+--------+
| dest(6) | src(6) | type(2) | op(2) | pausetime(2) | pad(42) | FCS(4) |
+---------+--------+---------+-------+--------------+---------+--------+
                             |<----- Ethernet Payload ------> |
dest:      01:80:C2:00:00:01
type:      0x8808
op code:   0x0001
pausetime: 0 - 65535
*/

/*
 * MicroMiniHowto: 
 * Send an arbitrary Ethernet frame using an AF_PACKET socket in C
 */

/*
 * 1. Select the required EtherType.
 * 2. Create the AF_PACKET socket (see packet(7)).
 * 3. Determine the index number of the Ethernet interface to be used.
 *    (use interface name such as eth0, eno1, enp0s8 etc.)
 * 4. Construct the destination address.
 * 5. Send the Ethernet frame.
 */

#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>          /* SIOCGIFINDEX, ioctl() */
#include <sys/socket.h>         /* struct sockaddr, struct iovec, */
                                /* struct msghdr, AF_PACKET, SOCK_DGRAM, */
                                /* socket, sendto, sendmsg */

#include <arpa/inet.h>          /* in_addr_t, hton */
#include <net/ethernet.h>       /* ETHER_ADDR_LEN, ETH_P_* */
#include <net/if.h>             /* struct ifreq */
#include <netinet/if_ether.h>   /* struct ether_arp */
#include <netpacket/packet.h>   /* struct sockaddr_ll */

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

extern int create_pause_socket();
extern int send_pause_packet(int sockfd, char *if_name, int pause_time);

#endif
