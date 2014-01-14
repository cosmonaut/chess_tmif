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
//#define DMA_BUF_SIZE 0xa000
//#define DMA_BUF_SIZE 65536
#define DMA_BUF_SIZE 8192
#define DMA_BUF_NUM 16
/* Size of total DMA buffer in bytes */
#define DMA_USR_BUF_SIZE (DMA_BUF_SIZE * DMA_BUF_NUM)
/* Number of 16-bit samples in the DMA buffer */
#define DMA_NSAMPLES ( DMA_USR_BUF_SIZE / 2 )

#define DM7820_Return_Status(status,string) \
  if (status != 0) { printf("ERROR: DM7820 %s FAILED", string); }


int init_output_ports(DM7820_Board_Descriptor *);
int init_output_fifo(DM7820_Board_Descriptor *);
int init_output_dma(DM7820_Board_Descriptor *);
void clear_fifo_flags(DM7820_Board_Descriptor *);


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

static void ISR(dm7820_interrupt_info interrupt_status)
{
    /* If this ISR is called that means an input DMA transfer has completed. */
    
    DM7820_Return_Status(interrupt_status.error, "ISR Failed\n");
    
    switch (interrupt_status.source) {
    case DM7820_INTERRUPT_FIFO_0_DMA_DONE:
        // dma0 = 1;
        // if (dma0 && dma1) {
        //     interrupts++;
        //     dma0 = 0;
        //     dma1 = 0;
        // }
        break;
    case DM7820_INTERRUPT_FIFO_1_DMA_DONE:
        // dma1 = 1;
        // if (dma0 && dma1) {
        //     interrupts++;
        //     dma0 = 0;
        //     dma1 = 0;
        // }
        break;
    default:
        break;
    }
}


static void get_fifo_status(DM7820_Board_Descriptor *board,
                            dm7820_fifo_queue fifo,
                            dm7820_fifo_status_condition condition, uint8_t *status) {
    if (DM7820_FIFO_Get_Status(board, fifo, condition, status) == -1) {
        status = NULL;
        //syslog(LOG_ERR, "ERROR: DM7820_FIFO_Get_Status() failed!");
        printf("Get FIFO Status failed \n");
    }
}


int main(void) {
    /* DM9820 items */
    DM7820_Error dm7820_status;
    DM7820_Board_Descriptor *output_board;
    uint8_t fifo_status = 0x00;
    /* DMA buffer */
    uint16_t *dma_buf = NULL;

    /* Socket items */
    // int sock_fd;
    // int sock_status = 0;
    // struct sockaddr_in sin;
    // int sock_opts = 0;
    // struct sockaddr_storage from_addr;
    // socklen_t addr_len;

    // int sock_nbytes = 0;

    /* buffers */
    //uint16_t packet_buf[2048];
    char s[100];

    /* Signals */
    struct sigaction sa_quit;

    uint16_t packet_counter = 0;
    int i = 0;
    uint16_t num_photons = 0;
    uint32_t pkt_mismatch_cnt = 0;

    uint32_t j = 0;

    uint32_t dma_i = 0;

    printf("Hello!\n");



    // /* Create socket */
    // sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    // if (sock_fd < 0) {
    //     printf("Error creating socket...\n");
    // }

    // /* Bind to port 60000 */
    // memset(&sin, 0, sizeof(sin));
    // sin.sin_family = AF_INET;
    // sin.sin_addr.s_addr = INADDR_ANY;
    // sin.sin_port = htons(CU40MMXS_PORT);

    // sock_status = bind(sock_fd, (struct sockaddr *)&sin, sizeof(sin));
    // if (sock_status < 0) {
    //     printf("Failed to bind\n");
    // }

    // /* Set non-blocking socket */
    // sock_opts = fcntl(sock_fd, F_GETFL);
    // sock_opts = fcntl(sock_fd, F_SETFL, (sock_opts | O_NONBLOCK));
    // if (sock_opts == -1) {
    //     printf("Failed to set socket as non-blocking\n");
    // }


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

    dm7820_status = DM7820_General_InstallISR(output_board, ISR);
    DM7820_Return_Status(dm7820_status, "DM7820_General_InstallISR()");
    
    printf("Setting ISR priority ...\n");
    dm7820_status = DM7820_General_SetISRPriority(output_board, 99);
    DM7820_Return_Status(dm7820_status, "DM7820_General_SetISRPriority()");
    
    /* Enable FIFO 0 */
    dm7820_status = DM7820_FIFO_Enable(output_board, DM7820_FIFO_QUEUE_0, 0xFF);
    if (dm7820_status < 0) {
        printf("Failed to enable fifo \n");
    }

    /* Set strobe 2 as input */
    dm7820_status = DM7820_StdIO_Strobe_Mode(output_board,
                                             DM7820_STDIO_STROBE_2,
                                             0x00);
    if (dm7820_status < 0) {
        printf("Failed to set strobe to input \n");
    }

    clear_fifo_flags(output_board);

    /* Output FIFOS should be empty */    
    get_fifo_status(output_board, DM7820_FIFO_QUEUE_0, DM7820_FIFO_STATUS_EMPTY,
                    &fifo_status);
    if (!fifo_status) {
        printf("FIFO 0 NOT empty! \n");
    }

    printf("DMA SIZE: %i \n", DMA_USR_BUF_SIZE);
    printf("DMA SAMPLES SIZE: %i \n", DMA_NSAMPLES);
    /* Create DMA buffers */
    dm7820_status =
        DM7820_FIFO_DMA_Create_Buffer(&dma_buf, DMA_USR_BUF_SIZE);
    if (dm7820_status < 0) {
        printf("Failed to create DMA buffer \n");
        perror("DMA BUF: ");
    }


    /* Allow graceful quit with various signals */
    memset(&sa_quit, 0, sizeof(sa_quit));
    sa_quit.sa_handler = &signal_handler;

    sigaction(SIGHUP, &sa_quit, NULL);
    sigaction(SIGTERM, &sa_quit, NULL);
    sigaction(SIGINT, &sa_quit, NULL);
    sigaction(SIGQUIT, &sa_quit, NULL);

    //addr_len = sizeof(from_addr);
    printf("memset?\n");
    memset(dma_buf, 0, DMA_USR_BUF_SIZE);
    printf("entering loop...\n");

    /* this is the magic. */
    while(loop_switch) {
    
        for (i = 0; i < 900; i++) {
            // dm7820_status =
            //     DM7820_FIFO_Write(output_board, DM7820_FIFO_QUEUE_0, (i%8192) | 0x2000);
            // dm7820_status =
            //     DM7820_FIFO_Write(output_board, DM7820_FIFO_QUEUE_0, (i%8192) | 0x4000);
            // dm7820_status =
            //     DM7820_FIFO_Write(output_board, DM7820_FIFO_QUEUE_0, (i%255 | 0x6000));
            // if (dm7820_status < 0) {
            //     loop_switch = 0;
            //     break;
            // }

            // switch(i%3) {
            // case 0:
            //     dma_buf[i] = (i%8192 | 0x2000);
            //     break;
            // case 1:
            //     dma_buf[i] = (i%8192 | 0x4000);
            //     break;
            // case 2:
            //     dma_buf[i] = (i%255 | 0x6000);
            //     break;
            // }

            switch(i%3) {
            case 0:
                dma_buf[dma_i] = (1 | 0x2000);
                dma_i++;
                break;
            case 1:
                dma_buf[dma_i] = (1 | 0x4000);
                dma_i++;
                break;
            case 2:
                dma_buf[dma_i] = (1 | 0x6000);
                dma_i++;
                break;
            }

                
        }

        //dma_buf[i+1] = 0x0000;
        /* buffer with a 0 */
        // dm7820_status =
        //     DM7820_FIFO_Write(output_board, DM7820_FIFO_QUEUE_0, 0x0000);
        
        if (j%3 == 0) {
            dma_buf[dma_i] = 0x0000;
            dma_i++;
            //printf("dma_i: %i\n", dma_i);

            dm7820_status = DM7820_FIFO_DMA_Write(output_board,
                                                  DM7820_FIFO_QUEUE_0,
                                                  dma_buf, 1);
            if (dm7820_status > 0) {
                printf("error with DMA write\n");
            }

            dm7820_status = DM7820_FIFO_DMA_Enable(output_board,
                                                   DM7820_FIFO_QUEUE_0, 0xFF, 0xFF);
            //DM7820_Return_Status(dm7820_status, "DM7820_FIFO_DMA_Enable()");
            if (dm7820_status > 0) {
                printf("error with DMA enable\n");
            }

            dma_i = 0;
            memset(dma_buf, 0, sizeof(uint16_t)*(3000 + 1));
        }
    
        j++;
        if (j > 99999) {
            loop_switch = 0;
        }
        usleep(1000);
    }


    //usleep(100000);
    sleep(5);

    /* Disable DMA on FIFO 0 */
    dm7820_status = DM7820_FIFO_DMA_Enable(output_board,
                                           DM7820_FIFO_QUEUE_0, 0x00, 0x00);
    if (dm7820_status < 0) {
        printf("Failed to disble dma on fifo 0 \n");
    }
    
    /* Free DMA buffer */
    dm7820_status =
        DM7820_FIFO_DMA_Free_Buffer(&dma_buf, DMA_USR_BUF_SIZE);
    if (dm7820_status < 0) {
        printf("Error freeing DMA buffer \n");
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

    // /* Close the socket! */
    // close(sock_fd);

    printf("Good close\n");
    printf("Total packet mismatch: %u\n", pkt_mismatch_cnt);

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
                                                 DM7820_FIFO_OUTPUT_CLOCK_STROBE_2);

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

void clear_fifo_flags(DM7820_Board_Descriptor *board) {
    uint8_t fifo_status;

    //syslog(LOG_INFO, "Clearing FIFO flags...");
    //fprintf(stdout, "Clearing FIFO flags... \n");

    //fprintf(stdout, "Clearing FIFO 0 status empty flag ...\n");
    get_fifo_status(board, DM7820_FIFO_QUEUE_0, DM7820_FIFO_STATUS_EMPTY,
                    &fifo_status);

    /* Clear FIFO status full flag without checking its state */
    //fprintf(stdout, "Clearing FIFO 0 status full flag ...\n");
    get_fifo_status(board, DM7820_FIFO_QUEUE_0, DM7820_FIFO_STATUS_FULL,
                    &fifo_status);

    /* Clear FIFO status overflow flag without checking its state */
    //fprintf(stdout, "Clearing FIFO 0 status overflow flag ...\n");
    get_fifo_status(board, DM7820_FIFO_QUEUE_0, DM7820_FIFO_STATUS_OVERFLOW,
                    &fifo_status);

    /* Clear FIFO status underflow flag without checking its state */
    //fprintf(stdout, "Clearing FIFO 0 status underflow flag ...\n");
    get_fifo_status(board, DM7820_FIFO_QUEUE_0,
                    DM7820_FIFO_STATUS_UNDERFLOW, &fifo_status);
}
