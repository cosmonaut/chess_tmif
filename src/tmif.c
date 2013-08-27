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
#define DMA_BUF_SIZE 0xa000
#define DMA_BUF_NUM 1


int init_output_ports(DM7820_Board_Descriptor *);
int init_output_fifo(DM7820_Board_Descriptor *);
int init_output_dma(DM7820_Board_Descriptor *);


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

    dm7820_status = init_output_fifo(output_board);
    if (dm7820_status < 0) {
        printf("Failed to set fifo \n");
    }

    dm7820_status = init_output_dma(output_board);
    if (dm7820_status < 0) {
        printf("Failed to set up DMA \n");
    }
    

    /* Enable FIFO 0 */
    dm7820_status = DM7820_FIFO_Enable(output_board, DM7820_FIFO_QUEUE_0, 0xFF);
    if (dm7820_status < 0) {
        printf("Failed to enable fifo \n");
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

    /* Disable DMA on FIFO 0 */
    dm7820_status = DM7820_FIFO_DMA_Enable(output_board,
                                           DM7820_FIFO_QUEUE_0, 0x00, 0x00);
    if (dm7820_status < 0) {
        printf("Failed to disble dma on fifo 0 \n");
    }
    
    dm7820_status = DM7820_FIFO_Enable(output_board, DM7820_FIFO_QUEUE_0, 0x00);
    if (dm7820_status < 0) {
        printf("Failed to disable fifo \n");
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

int init_output_fifo(DM7820_Board_Descriptor *board) {
    DM7820_Error dm7820_status;    

    /* Init Output FIFO */

    /* FIFO 0 */
    /* Disable FIFO 0 */
    dm7820_status = DM7820_FIFO_Enable(board, DM7820_FIFO_QUEUE_0, 0x00);
    
    /* Set FIFO input clock to PCI write */
    dm7820_status = DM7820_FIFO_Set_Input_Clock(board,
                                                DM7820_FIFO_QUEUE_0,
                                                DM7820_FIFO_INPUT_CLOCK_PCI_WRITE);
    
    
    /* Set FIFO 0 output clock to strobe 1 (wallops strobe) */
    dm7820_status = DM7820_FIFO_Set_Output_Clock(board,
                                                 DM7820_FIFO_QUEUE_0,
                                                 DM7820_FIFO_OUTPUT_CLOCK_STROBE_1);

    /* Set FIFO 0 data input to PCI data */
    dm7820_status = DM7820_FIFO_Set_Data_Input(board,
                                               DM7820_FIFO_QUEUE_0,
                                               DM7820_FIFO_0_DATA_INPUT_PCI_DATA);

    dm7820_status = DM7820_FIFO_Set_DMA_Request(board,
                                                DM7820_FIFO_QUEUE_0,
                                                DM7820_FIFO_DMA_REQUEST_WRITE);

    /* Set the FIFO 0 output to port 0 */
    /* The mask is 0xFFFF for a full 16 bit word */
    dm7820_status = DM7820_StdIO_Set_Periph_Mode(board,
                                                 DM7820_STDIO_PORT_0,
                                                 0xFFFF,
                                                 DM7820_STDIO_PERIPH_FIFO_0);
    
    return 0;
}

int init_output_dma(DM7820_Board_Descriptor *board) {
    DM7820_Error dm7820_status;    

    /*  Initializing DMA 0 */
    //syslog(LOG_INFO, "Initializing DMA 0 ...");
    dm7820_status = DM7820_FIFO_DMA_Initialize(board,
                                               DM7820_FIFO_QUEUE_0,
                                               DMA_BUF_NUM, DMA_BUF_SIZE);
    //DM7820_Return_Status(dm7820_status, "DM7820_FIFO_DMA_Initialize()");

    /*  Configuring DMA 0 */
    //syslog(LOG_INFO, "    Configuring DMA 0 ...");
    dm7820_status = DM7820_FIFO_DMA_Configure(board,
                                              DM7820_FIFO_QUEUE_0,
                                              DM7820_DMA_DEMAND_ON_PCI_TO_DM7820,
                                              DMA_BUF_SIZE);
    //DM7820_Return_Status(dm7820_status, "DM7820_FIFO_DMA_Configure()");

    return 0;
}
