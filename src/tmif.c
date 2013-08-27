/* Author: Nicholas Nell
   email: nicholas.nell@colorado.edu
*/

#include <dm7820_library.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <signal.h>


#define CU40MMXS_PORT 60000


int init_output_ports(DM7820_Board_Descriptor *);


/* global loop control */
static volatile sig_atomic_t loop_switch = 1;


static void signal_handler(int sig) {
    switch(sig) {
    case SIGHUP:
        //syslog(LOG_WARNING, "Caught signal SIGHUP! (tmif_server)");
        loop_switch = 0;
        break;
    case SIGTERM:
        //syslog(LOG_WARNING, "Caught signal SIGTERM! (tmif_server)");
        loop_switch = 0;
        break;
    case SIGQUIT:
        //syslog(LOG_WARNING, "Caught signal SIGQUIT! (tmif_server)");
        loop_switch = 0;
        break;
    default:
        //syslog(LOG_WARNING, "Caught signal (%d) %s", strsignal(sig));
        loop_switch = 0;
        break;
    }
}


int main(void) {
    /* DM9820 items */
    DM7820_Error dm7820_status;
    DM7820_Board_Descriptor *output_board;

    /* Socket items */
    int sock_fd;
    int sock_status = 0;
    struct sockaddr_in sin;
    int sock_opts = 0;
    struct sockaddr_storage from_addr;
    socklen_t addr_len;

    int sock_nbytes = 0;

    /* buffers */
    uint16_t packet_buf[2048];
    char s[100];

    /* Signals */
    struct sigaction sa_quit;


    printf("Hello!\n");



    /* Create socket */
    sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0) {
        printf("Error creating socket...\n");
    }

    /* Bind to port 60000 */
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(CU40MMXS_PORT);

    sock_status = bind(sock_fd, (struct sockaddr *)&sin, sizeof(sin));
    if (sock_status < 0) {
        printf("Failed to bind\n");
    }

    /* Set non-blocking socket */
    sock_opts = fcntl(sock_fd, F_GETFL);
    sock_opts = fcntl(sock_fd, F_SETFL, (sock_opts | O_NONBLOCK));
    if (sock_opts == -1) {
        printf("Failed to set socket as non-blocking\n");
    }


    /* Init DM9820 board */
    dm7820_status = DM7820_General_Open_Board(0, &output_board);
    if (dm7820_status < 0) {
        printf("Failed to open board\n");
        return -1;
    }

    dm7820_status = DM7820_General_Reset(output_board);
    if (dm7820_status < 0) {
        printf("Failed to reset board \n");
    }

    dm7820_status = init_output_ports(output_board);
    if (dm7820_status < 0) {
        printf("Failed to set up ports \n");
    }




    /* Allow graceful quit with various signals */
    memset(&sa_quit, 0, sizeof(sa_quit));
    sa_quit.sa_handler = &signal_handler;

    sigaction(SIGHUP, &sa_quit, NULL);
    sigaction(SIGTERM, &sa_quit, NULL);
    sigaction(SIGINT, &sa_quit, NULL);
    sigaction(SIGQUIT, &sa_quit, NULL);


    /* this is the magic. */
    while(loop_switch) {
    
        sock_nbytes = recvfrom(sock_fd, packet_buf, sizeof(packet_buf), 0, (struct sockaddr *)&from_addr, &addr_len);
        //printf("sock bytes: %i\n", sock_nbytes);
        //perror("socket!");
        if (sock_nbytes > 0) {
            printf("sock bytes: %i\n", sock_nbytes);
            printf("Num photons: %u\n", packet_buf[0]);
            printf("Packet Count: %u\n", packet_buf[1]);
            printf("got packet from: %s\n\n", inet_ntop(from_addr.ss_family, 
                                                    &(((struct sockaddr_in*)((struct sockaddr *)&from_addr))->sin_addr), 
                                                    s, 
                                                    sizeof(s)));
        }

        usleep(50);
    }




    /* Close down everything gracefully */

    dm7820_status = DM7820_General_Close_Board(output_board);
    if (dm7820_status < 0) {
        printf("Failed to close board!\n");
        return -1;
    }

    /* Close the socket! */
    close(sock_fd);

    printf("Good close\n");

    return 0;
}


int init_output_ports(DM7820_Board_Descriptor *board) {
    DM7820_Error dm7820_status;

    /* Set all port 0 lines as perhipheral output */
    dm7820_status =
        DM7820_StdIO_Set_IO_Mode(board, DM7820_STDIO_PORT_0, 0xFFFF,
                                 DM7820_STDIO_MODE_PER_OUT);
    if (dm7820_status < 0) {
        return -1;
    }

    /* Set all lines low */
    dm7820_status =
        DM7820_StdIO_Set_Output(board, DM7820_STDIO_PORT_0, 0x0000);
    if (dm7820_status < 0) {
        return -1;
    }

    return 0;
}
