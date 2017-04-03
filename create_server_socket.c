#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>

// Set this to the maximum number of clients you want waiting to be serviced
#define LISTEN_QUEUE_SIZE	1024

// Example: int sock = create_server_socket("8080", SOCK_STREAM);
int create_server_socket(char* port, int protocol) {
    int sock;
    int ret;
    int optval = 1;
    struct addrinfo hints;
    struct addrinfo* addr_ptr;
    struct addrinfo* addr_list;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = protocol;
    /* AI_PASSIVE for filtering out addresses on which we
     * can't use for servers
     *
     * AI_ADDRCONFIG to filter out address types the system
     * does not support
     *
     * AI_NUMERICSERV to indicate port parameter is a number
     * and not a string
     *
     * */
    hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG | AI_NUMERICSERV;
    /*
     *  On Linux binding to :: also binds to 0.0.0.0
     *  Null is fine for TCP, but UDP needs both
     *  See https://blog.powerdns.com/2012/10/08/on-binding-datagram-udp-sockets-to-the-any-addresses/
     */
    ret = getaddrinfo(protocol == SOCK_DGRAM ? "::" : NULL, port, &hints, &addr_list);
    if (ret != 0) {
        fprintf(stderr, "Failed in getaddrinfo: %s\n", gai_strerror(ret));
        exit(EXIT_FAILURE);
    }

    for (addr_ptr = addr_list; addr_ptr != NULL; addr_ptr = addr_ptr->ai_next) {
        sock = socket(addr_ptr->ai_family, addr_ptr->ai_socktype, addr_ptr->ai_protocol);
        if (sock == -1) {
            perror("socket");
            continue;
        }

        // Allow us to quickly reuse the address if we shut down (avoiding timeout)
        ret = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
        if (ret == -1) {
            perror("setsockopt");
            close(sock);
            continue;
        }

        ret = bind(sock, addr_ptr->ai_addr, addr_ptr->ai_addrlen);
        if (ret == -1) {
            perror("bind");
            close(sock);
            continue;
        }
        break;
    }
    freeaddrinfo(addr_list);
    if (addr_ptr == NULL) {
        fprintf(stderr, "Failed to find a suitable address for binding\n");
        exit(EXIT_FAILURE);
    }

    if (protocol == SOCK_DGRAM) {
        return sock;
    }
    // Turn the socket into a listening socket if TCP
    ret = listen(sock, LISTEN_QUEUE_SIZE);
    if (ret == -1) {
        perror("listen");
        close(sock);
        exit(EXIT_FAILURE);
    }

    return sock;
}
