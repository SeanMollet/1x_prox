/*
 *   Copyright (C) 2016 by Mackey
 *   https://www.dslreports.com/profile/1479488
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <linux/filter.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <unistd.h>
#include <fcntl.h>


char wan_ifname[] = "eth0\0";
char int_ifname[] = "eth1\0";

struct sockaddr_ll wan_ll;
struct sockaddr_ll int_ll;

int bind_if( const char *ifname, struct sockaddr_ll *i_ll, int *sock, const char *prettyname )
{
    int sockopts;
    struct packet_mreq our_mreq;
    struct sock_fprog filter;

    printf("binding %s to if %s... ", prettyname, ifname);

    *sock = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_PAE)); // PAE == 802.1x

    memset(i_ll, 0, sizeof(struct sockaddr_ll));
    i_ll->sll_family = PF_PACKET;
    i_ll->sll_protocol = htons(ETH_P_PAE);
    i_ll->sll_ifindex = if_nametoindex(ifname);
    if (bind(*sock, (struct sockaddr *) i_ll, sizeof(struct sockaddr_ll)) < 0)
    {
        perror("bind failed\n");
        close(*sock);
        *sock = 0;
        return -1;
    }

    printf("bind ok, if %s idx: %d\n", ifname, i_ll->sll_ifindex);

    memset(&our_mreq, 0, sizeof(our_mreq));

    our_mreq.mr_ifindex = i_ll->sll_ifindex;
    our_mreq.mr_type = PACKET_MR_MULTICAST;
    our_mreq.mr_alen = 6;
    memcpy( our_mreq.mr_address, "\x01\x80\xC2\x00\x00\x03x0", 7 );

    if( setsockopt( *sock, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &our_mreq, sizeof( our_mreq ) ) < 0 )
    {
        printf("setsockopt error on %d/%s: %s\n", sock, ifname, strerror(errno) );
        close(*sock);
        *sock = 0;
        return -2;
    }

    // we only care about 802.1x packets, so attach a filter that only gives us those
    // ** is this really needed with the ETH_P_PAE above??
    /*
    // created by: tcpdump -dd 'ether dst 01:80:C2:00:00:03'
#define BPF_code_sz 6
    struct sock_filter BPF_code[BPF_code_sz] = {
        { 0x28, 0, 0, 0x00000000 },  // load [half-word] mac bytes 0:1
        { 0x15, 0, 3, 0x00000180 },  // match 01:80, jump to failed if false
        { 0x20, 0, 0, 0x00000002 },  // load [word] mac bytes 2:5
        { 0x15, 0, 1, 0xc2000003 },  // match c2:00:00:03, jump to failed if false
        { 0x6, 0, 0, 0x7fffffff },   // failed
        { 0x6, 0, 0, 0x00000000 }    // matched
    };

    filter.len = BPF_code_sz;
    filter.filter = BPF_code;

    if( setsockopt( *sock, SOL_SOCKET, SO_ATTACH_FILTER, &filter, sizeof(filter) ) < 0 )
    {
        printf("SO_ATTACH_FILTER failed, setsockopt error on %d/%s: %s (%d)\n", *sock, ifname, strerror(errno), sizeof(filter) );
        close(*sock);
        *sock = 0;
        return -3;
    }
    */

    /*
    // set non-blocking
    sockopts = fcntl( *sock, F_GETFL );
    if (sockopts < 0)
    {
        printf("fcntl(F_GETFL) error on %d/%s: %s\n", *sock, ifname, strerror(errno) );
        return -4;
    }

    sockopts = (sockopts | O_NONBLOCK);
    if ( fcntl( *sock, F_SETFL, sockopts ) < 0 )
    {
        printf("fcntl(F_SETFL) error on %d/%s: %s\n", *sock, ifname, strerror(errno) );
        return -5;
    }
    */

    return 0;
}

int mainloop( int set_wan, int set_int )
{
#define buf_sz 2000

    //struct ifreq ifr;
    //struct sockaddr wan_saddr;
    int wan_sock = -1, int_sock = -1;
    fd_set rfds;
    int max_sock, retval, i, r, w;
    unsigned char buf[buf_sz];
    struct ifreq ifr, ifr2;
    short ifr2_flags;
    unsigned char *mac;

    if( bind_if( wan_ifname, &wan_ll, &wan_sock, "ONT/wan" ) < 0 )
    {
        printf("ONT sock bind failed, erroring out!\n");
        return -1;
    }

    if( bind_if( int_ifname, &int_ll, &int_sock, "RG/int" ) < 0 )
    {
        printf("RG sock bind failed, erroring out!\n");
        close( wan_sock );
        return -1;
    }

    max_sock = ( int_sock > wan_sock ? int_sock : wan_sock) + 1;
    //printf("socks: %d/%d/%d\n", wan_sock, int_sock, max_sock);

    while( 1 )
    {
        FD_ZERO(&rfds);
        FD_SET(wan_sock, &rfds);
        FD_SET(int_sock, &rfds);

        retval = select( (max_sock), &rfds, NULL, NULL, NULL );

        if (retval == -1)
        {
            perror("select() returned error");
            close( int_sock );
            close( wan_sock );
            perror("select()");
        }
        else if (!retval)
        {
            printf("select() returned empty!\n");
            close( int_sock );
            close( wan_sock );
            return -2;
        }

        if( FD_ISSET( wan_sock, &rfds ) )
        {
            r = read(wan_sock, buf, buf_sz-1);

            if (r <= 0)
            {
                perror("wan_sock: no data");
                close( int_sock );
                close( wan_sock );
                return -3;
            }

            /*
            printf("wan_sock said (%d):", r);

            for( i = 0; i < r; i++ )
                printf( " %02X", buf[i] );

            printf("\n");
            */

            if( set_int )
            {
                memset( &ifr, 0, sizeof(ifr) );
                memset( &ifr2, 0, sizeof(ifr2) );

                strncpy( ifr.ifr_name, int_ifname , IFNAMSIZ );
                strncpy( ifr2.ifr_name, int_ifname , IFNAMSIZ );

                if( ioctl(int_sock, SIOCGIFHWADDR, &ifr) )
                {
                    perror("SIOCGIFHWADDR on int_sock failed");
                    close( int_sock );
                    close( wan_sock );
                    return -4;
                }

                if( !(memcmp( ifr.ifr_hwaddr.sa_data, &buf[6], 6 )) )
                {
                    printf("int MAC already correct\n");
                    set_int = 0;
                }
                else
                {
                    if ( ioctl( int_sock, SIOCGIFFLAGS, &ifr2) )
                    {
                        perror( "SIOCGIFFLAGS on int_sock failed" );
                        close( int_sock );
                        close( wan_sock );
                        return -4;
                    }

                    ifr2_flags = ifr2.ifr_flags;

                    if( ifr2_flags & IFF_UP) // interface is up, take it down so we can set the MAC
                    {
                        ifr2.ifr_flags &= ~IFF_UP;
                        if ( ioctl( int_sock, SIOCSIFFLAGS, &ifr2 ) )
                        {
                            perror( "SIOCSIFFLAGS (1) on int_sock failed" );
                            close( int_sock );
                            close( wan_sock );
                            return -5;
                        }
                    }

                    ifr.ifr_hwaddr.sa_family = ARPHRD_ETHER;
                    memcpy( ifr.ifr_hwaddr.sa_data, &buf[6], 6 );

                    if( ioctl(int_sock, SIOCSIFHWADDR, &ifr) )
                    {
                        perror("SIOCSIFHWADDR on int_sock failed");
                    }
                    else
                    {
                        mac = (unsigned char *) ifr.ifr_hwaddr.sa_data;
                        printf( "set %s MAC to %02X:%02X:%02X:%02X:%02X:%02X ok\n", int_ifname, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5] );
                    }

                    if( ifr2_flags != ifr2.ifr_flags )
                    {
                        ifr2.ifr_flags = ifr2_flags;
                        if ( ioctl( int_sock, SIOCSIFFLAGS, &ifr2 ) )
                        {
                            perror( "SIOCSIFFLAGS (2) on int_sock failed" );
                            close( int_sock );
                            close( wan_sock );
                            return -6;
                        }

                        do
                        {
                            usleep(1000);

                            if ( ioctl( int_sock, SIOCGIFFLAGS, &ifr2) )
                            {
                                perror( "SIOCGIFFLAGS on int_sock failed" );
                                close( int_sock );
                                close( wan_sock );
                                return -4;
                            }
                        }
                        while( !(ifr2.ifr_flags & IFF_UP) );

                        close( int_sock );
                        printf("re-");
                        if( bind_if( int_ifname, &int_ll, &int_sock, "RG/int" ) < 0 )
                        {
                            printf("RG sock re-bind failed, erroring out!\n");
                            return -1;
                        }
                    }

                    if( ioctl(int_sock, SIOCGIFHWADDR, &ifr) )
                    {
                        perror("SIOCGIFHWADDR on int_sock failed");
                    }
                    else
                    {
                        mac = (unsigned char *) ifr.ifr_hwaddr.sa_data;
                        printf( "get %s MAC:   %02X:%02X:%02X:%02X:%02X:%02X (%d)\n", int_ifname, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], ifr.ifr_hwaddr.sa_family );
                    }

                    set_int = 0;
                }
            } // if( set_int )

            printf("copying %d bytes ONT->RG\n", r);

            w = write(int_sock, buf, r);
            if (w < 0)
            {
                perror("write failed");
                close( int_sock );
                close( wan_sock );
                return -1;
            }
            else if (w != r)
            {
                printf("write truncate: %d -> %d\n", r, w);
                close( int_sock );
                close( wan_sock );
                return -1;
            }
        }

        if( FD_ISSET( int_sock, &rfds ) )
        {
            r = read(int_sock, buf, buf_sz-1);

            if (r <= 0)
            {
                perror("int_sock: no data");
                close( int_sock );
                close( wan_sock );
                return -4;
            }

            /*
            printf("int_sock said (%d):", r);

            for( i = 0; i < r; i++ )
                printf( " %02X", buf[i] );

            printf("\n");
            */

            if( set_wan )
            {
                memset( &ifr, 0, sizeof(ifr) );
                memset( &ifr2, 0, sizeof(ifr2) );

                strncpy( ifr.ifr_name, wan_ifname , IFNAMSIZ );
                strncpy( ifr2.ifr_name, wan_ifname , IFNAMSIZ );

                if( ioctl(wan_sock, SIOCGIFHWADDR, &ifr) )
                {
                    perror("SIOCGIFHWADDR on wan_sock failed");
                    close( int_sock );
                    close( wan_sock );
                    return -4;
                }

                if( !(memcmp( ifr.ifr_hwaddr.sa_data, &buf[6], 6 )) )
                {
                    printf("wan MAC already correct\n");
                    set_wan = 0;
                }
                else
                {
                    if ( ioctl( wan_sock, SIOCGIFFLAGS, &ifr2) )
                    {
                        perror( "SIOCGIFFLAGS on wan_sock failed" );
                        close( int_sock );
                        close( wan_sock );
                        return -4;
                    }

                    ifr2_flags = ifr2.ifr_flags;

                    if( ifr2_flags & IFF_UP) // interface is up, take it down so we can set the MAC
                    {
                        ifr2.ifr_flags &= ~IFF_UP;
                        if ( ioctl( wan_sock, SIOCSIFFLAGS, &ifr2 ) )
                        {
                            perror( "SIOCSIFFLAGS (1) on wan_sock failed" );
                            close( int_sock );
                            close( wan_sock );
                            return -5;
                        }
                    }

                    ifr.ifr_hwaddr.sa_family = ARPHRD_ETHER;
                    memcpy( ifr.ifr_hwaddr.sa_data, &buf[6], 6 );

                    if( ioctl(wan_sock, SIOCSIFHWADDR, &ifr) )
                    {
                        perror("SIOCSIFHWADDR on int_sock failed");
                    }
                    else
                    {
                        mac = (unsigned char *) ifr.ifr_hwaddr.sa_data;
                        printf( "set %s MAC to %02X:%02X:%02X:%02X:%02X:%02X ok\n", wan_ifname, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5] );
                    }

                    if( ifr2_flags != ifr2.ifr_flags )
                    {
                        ifr2.ifr_flags = ifr2_flags;
                        if ( ioctl( wan_sock, SIOCSIFFLAGS, &ifr2 ) )
                        {
                            perror( "SIOCSIFFLAGS (2) on wan_sock failed" );
                            close( int_sock );
                            close( wan_sock );
                            return -6;
                        }

                        do
                        {
                            usleep(1000);

                            if ( ioctl( wan_sock, SIOCGIFFLAGS, &ifr2) )
                            {
                                perror( "SIOCGIFFLAGS on wan_sock failed" );
                                close( int_sock );
                                close( wan_sock );
                                return -4;
                            }
                        }
                        while( !(ifr2.ifr_flags & IFF_UP) );

                        close( wan_sock );
                        printf("re-");
                        if( bind_if( wan_ifname, &wan_ll, &wan_sock, "ONT/wan" ) < 0 )
                        {
                            printf("ONT sock re-bind failed, erroring out!\n");
                            return -1;
                        }
                    }

                    if( ioctl(wan_sock, SIOCGIFHWADDR, &ifr) )
                    {
                        perror("SIOCGIFHWADDR on wan_sock failed");
                    }
                    else
                    {
                        mac = (unsigned char *) ifr.ifr_hwaddr.sa_data;
                        printf( "get %s MAC:   %02X:%02X:%02X:%02X:%02X:%02X (%d)\n", wan_ifname, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], ifr.ifr_hwaddr.sa_family );
                    }
                    //Bounce DHCP so it will get an address when the time is right
                    system("/config/1x_prox/link_up.sh");
                    set_wan = 0;
                }
            } // if( set_wan )

            printf("copying %d bytes RG->ONT\n", r);

            w = write(wan_sock, buf, r);
            if (w < 0)
            {
                perror("write failed");
                close( int_sock );
                close( wan_sock );
                return -1;
            }
            else if (w != r)
            {
                printf("write truncate: %d -> %d\n", r, w);
                close( int_sock );
                close( wan_sock );
                return -1;
            }
        }
    }

    return 0;
}

int main()
{
    int retval;

    do
    {
        retval = mainloop( 1, 1 );
        sleep(2);
    }
    while( 1 );

    return retval;
}
