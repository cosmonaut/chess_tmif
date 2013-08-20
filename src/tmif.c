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


#define CU40MMXS_PORT 60000


int main(void) {
    /* DM9820 items */
    DM7820_Error dm7820_status;
    DM7820_Board_Descriptor *output_board;

    /* Socket items */
    int sock_fd;
    int sock_status = 0;
    struct sockaddr_in sin;
    int sock_opts = 0;

    printf("Hello!\n");

    /* Create socket */
    sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0) {
        printf("Error creatting socket...\n");
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
