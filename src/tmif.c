/* Author: Nicholas Nell
   email: nicholas.nell@colorado.edu

   CHESS Telemetry Interface.
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
#include <sys/resource.h>
#include <sys/time.h>


#include "tmif_hdf5.h"

#define CU40MMXS_PORT 60000
#define CU40MMXS_PACKET_SIZE 1470
/* DMA buffer size in bytes */
#define DMA_BUF_SIZE 1470
/* Number of DMA buffers */
#define DMA_BUF_NUM 16
/* Size of complete DMA buffer in bytes */
#define DMA_USR_BUF_SIZE (DMA_BUF_SIZE * DMA_BUF_NUM)
/* Number of 16-bit samples in the DMA buffer */
#define DMA_NSAMPLES ( DMA_USR_BUF_SIZE / 2 )

/* P2.0 Heartbeat
   P2.1 FIFO Full
   P2.2 IDAN Error */
#define TMIF_STATUS_HEART 0x0001
#define TMIF_STATUS_FIFO_FULL 0x0002
#define TMIF_STATUS_ERR 0x0004

#define DM7820_Return_Status(status, string) \
  if (status != 0) { printf("ERROR: DM7820 %s FAILED", string); }


int init_output_ports(DM7820_Board_Descriptor *);
int init_output_fifo(DM7820_Board_Descriptor *);
int init_output_dma(DM7820_Board_Descriptor *);
void clear_fifo_flags(DM7820_Board_Descriptor *);
int set_status_bit(DM7820_Board_Descriptor *, int, int, uint16_t *);

/* global loop control */
static volatile sig_atomic_t loop_switch = 1;
static volatile uint8_t dma_flag = 0;
//static volatile uint8_t fifo_full_flag = 0;
/* global health bit */
static volatile uint8_t g_health_bit = 0;

static void health_handler(int sig) {
    /* flip health bit */
    g_health_bit = (g_health_bit + 1)%2;
}

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


static void ISR(dm7820_interrupt_info interrupt_status) {
    /* If this ISR is called that means an input DMA transfer has completed. */
    
    DM7820_Return_Status(interrupt_status.error, "ISR Failed\n");
    
    switch (interrupt_status.source) {
    case DM7820_INTERRUPT_FIFO_0_DMA_DONE:
        /* flag number of dma writes */
        dma_flag++;
        break;
    case DM7820_INTERRUPT_FIFO_1_DMA_DONE:
        break;
    case DM7820_INTERRUPT_FIFO_0_FULL:
        //fifo_full_flag = 1;
        //printf("FIFO 0 FULL!\n");
        break;
    case DM7820_INTERRUPT_FIFO_0_EMPTY:
        //printf("FIFO 0 empty!\n");
        break;
    case DM7820_INTERRUPT_FIFO_0_UNDERFLOW:
        //printf("FIFO 0 underflow!\n");
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
    /* DMA index */
    uint32_t dma_i = 0;
    uint16_t dma_chk = 0;

    /* Socket items */
    int sock_fd;
    int sock_status = 0;
    struct sockaddr_in sin;
    int sock_opts = 0;
    struct sockaddr_storage from_addr;
    socklen_t addr_len;
    int opt_status = 0;
    int sock_nbytes = 0;
    int sock_so_rcvbuf = 0;
    socklen_t optlen = sizeof(sock_so_rcvbuf);

    /* buffers */
    uint16_t packet_buf[735];
    uint16_t psave_buf[735*10];
    //uint16_t 
    //char s[100];
    uint16_t *pbufptr = packet_buf;
    uint16_t *psaveptr = psave_buf;
    uint16_t pbuf_ind = 0;

    /* health */
    uint8_t l_health_bit = 0;
    uint16_t status_bits = 0x0000;

    /* Signals */
    struct sigaction sa_quit;
    struct sigaction sa_health;
    struct itimerval health_timer;

    /* tmif */
    uint32_t tot_pkt_count = 0;
    uint16_t packet_counter = 0;
    uint16_t packet_counter_s = 0;
    uint16_t packet_counter_h5 = 0;
    int i = 0;
    uint16_t num_photons = 0;
    uint32_t pkt_mismatch_cnt = 0;
    /* Generic status checker! */
    int status = 0;

    /* priority */
    id_t pid;


    printf("Hello!\n");
    memset(packet_buf, 0, sizeof(uint16_t)*735);

    /* Set highest priority */
    pid = getpid();
    status = setpriority(PRIO_PROCESS, pid, -20);
    if (status != 0) {
        printf("setpriority() fail %d\n", status);
    }

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

    opt_status = getsockopt(sock_fd, SOL_SOCKET, SO_RCVBUF, &sock_so_rcvbuf, &optlen);
    if (opt_status < 0) {
        printf("getsockopt() error\n");
        perror("getsockopt()");
    }
    printf("so_rcvbuf: %i\n", sock_so_rcvbuf);

    sock_so_rcvbuf = 8388608*2;
    opt_status = setsockopt(sock_fd, SOL_SOCKET, SO_RCVBUF, &sock_so_rcvbuf, optlen);
    if (opt_status < 0) {
        printf("setsockopt() error\n");
        perror("setsockopt()");
    }

    opt_status = getsockopt(sock_fd, SOL_SOCKET, SO_RCVBUF, &sock_so_rcvbuf, &optlen);
    if (opt_status < 0) {
        printf("getsockopt() error\n");
        perror("getsockopt()");
    }
    printf("so_rcvbuf: %i\n", sock_so_rcvbuf);
    

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

    // dm7820_status = DM7820_General_Enable_Interrupt(output_board,
    //                                                 DM7820_INTERRUPT_FIFO_0_EMPTY,
    //                                                 0x00);
    // dm7820_status = DM7820_General_Enable_Interrupt(output_board,
    //                                                 DM7820_INTERRUPT_FIFO_0_UNDERFLOW,
    //                                                 0x00);
    // dm7820_status = DM7820_General_Enable_Interrupt(output_board,
    //                                                 DM7820_INTERRUPT_FIFO_0_WRITE_REQUEST,
    //                                                 0x00);
    // dm7820_status = DM7820_General_Enable_Interrupt(output_board,
    //                                                 DM7820_INTERRUPT_FIFO_0_FULL,
    //                                                 0x00);
    // dm7820_status = DM7820_General_Enable_Interrupt(output_board,
    //                                                 DM7820_INTERRUPT_FIFO_0_OVERFLOW, 
    //                                                 0x00);
    // dm7820_status = DM7820_General_Enable_Interrupt(output_board,
    //                                                 DM7820_INTERRUPT_FIFO_0_READ_REQUEST,
    //                                                 0x00);
    // dm7820_status = DM7820_General_Enable_Interrupt(output_board,
    //                                                 DM7820_INTERRUPT_FIFO_0_DMA_DONE,
    //                                                 0x00);



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
    /* Zero out the DMA buffer, don't want spurious words! */
    memset(dma_buf, 0, DMA_USR_BUF_SIZE);

    /* health status... */
    memset(&sa_health, 0, sizeof(sa_health));
    sa_health.sa_handler = &health_handler;
    sigaction(SIGALRM, &sa_health, NULL);

    health_timer.it_value.tv_sec = 0;
    health_timer.it_value.tv_usec = 500000;

    /* repeat half second intervals */ 
    health_timer.it_interval.tv_sec = 0; 
    health_timer.it_interval.tv_usec = 500000; 
    /* Start the health timer in "real" time */
    setitimer(ITIMER_REAL, &health_timer, NULL); 

    /* Allow graceful quit with various signals */
    memset(&sa_quit, 0, sizeof(sa_quit));
    sa_quit.sa_handler = &signal_handler;

    sigaction(SIGHUP, &sa_quit, NULL);
    sigaction(SIGTERM, &sa_quit, NULL);
    sigaction(SIGINT, &sa_quit, NULL);
    sigaction(SIGQUIT, &sa_quit, NULL);

    addr_len = sizeof(from_addr);

    status = init_packet_save();
    if (status != 0) {
        printf("Failed to open packet table!\n");
    }

    /* this is the magic. */
    while(loop_switch) {
    
        /* Health status bit stuff */
        if (l_health_bit != g_health_bit) {
            /* set status bit*/
            set_status_bit(output_board, 1, l_health_bit, &status_bits);
            l_health_bit = (l_health_bit + 1)%2;
            printf("flippity flop\n");
        }

        while(1) {
            // sock_nbytes = recvfrom(sock_fd, 
            //                        packet_buf, 
            //                        sizeof(packet_buf), 
            //                        0, 
            //                        (struct sockaddr *)&from_addr, 
            //                        &addr_len);

            sock_nbytes = recvfrom(sock_fd, 
                                   packet_buf, 
                                   CU40MMXS_PACKET_SIZE, 
                                   0, 
                                   (struct sockaddr *)&from_addr, 
                                   &addr_len);

	    //printf("sock bytes: %i\n", sock_nbytes);

            if (sock_nbytes == 1470) {
                // printf("sock bytes: %i\n", sock_nbytes);
                // printf("Num photons: %u\n", packet_buf[0]);
                // printf("Packet Count: %u\n", packet_buf[1]);
                // printf("got packet from: %s\n\n", inet_ntop(from_addr.ss_family, 
                                                            // &(((struct sockaddr_in*)((struct sockaddr *)&from_addr))->sin_addr), 
                                                            // s, 
                                                            // sizeof(s)));
                /* Check for packet loss */
                if ((packet_counter + 1) != packet_buf[1]) {
                    // printf("PACKET COUNTER MISMATCH! \n");
                    // printf("pc+1: %u\n", (packet_counter + 1));
                    // printf("pack: %u\n", (packet_buf[1]));
                    pkt_mismatch_cnt++;
                }
                tot_pkt_count++;
                packet_counter = packet_buf[1];


                /* If enough packets have been read, save what we have */
                if (((uint16_t)(packet_counter - packet_counter_h5)) >= 10) {
                    //if (pbuf_ind >= 7350) {
                    //printf("About to save packets %d...\n", pbuf_ind);
                    //status = save_packets(pbufptr, 10);
                    status = save_packets(psaveptr, pbuf_ind);
                    if (status != 0) {
                        printf("save_packets() failed! %d\n", status);
                    }
                    /* Reset packet buffer index */
                    pbuf_ind = 0;
                    memset(psave_buf, 0, sizeof(psave_buf));
                    packet_counter_h5 = packet_counter;
                }

                /* If there are photons in the packet do work. */
                num_photons = packet_buf[0];
                if (num_photons > 0) {
                    /* Save packet if there are any photons in it */
                    status = (int)memcpy(&psave_buf[pbuf_ind*735], &packet_buf, CU40MMXS_PACKET_SIZE);
                    if (status) {
                        //pbuf_ind += 735;
                        pbuf_ind += 1;
                    } else {
                        /* eek */
                        printf("failed to copy memory to packet save buffer\n");
                    }

                    /* OLD Save to disk! */
                    //save_packet(pbufptr);

                    for (i = 3; i < 3*(num_photons + 1); i += 3) {
                        //printf("X, Y: %u, %u\n", packet_buf[i] >> 1 , packet_buf[i+1] >> 1);
                        //printf("PHD: %u\n", packet_buf[i+2]);

                        if (dma_i < (DMA_NSAMPLES - 100)) {
                            dma_buf[dma_i] = ((packet_buf[i] >> 1) | 0x2000);
                            dma_i++;
                            dma_buf[dma_i] = ((packet_buf[i+1] >> 1) | 0x4000);
                            dma_i++;
                            dma_buf[dma_i] = ((packet_buf[i+2]) | 0x6000);
                            dma_i++;
                        } else {
                            printf("dma index too large: %d\n", dma_i);
                        }
                    }
                }
                
                /* Write DMA after 3 packets have been processed */
                //if ((packet_counter - packet_counter_s) > 2) {
                if (((uint16_t)(packet_counter - packet_counter_s)) > 2) {

                    if (dma_i > 1) {
                        /* Buffer it all with a 0 */
                        dma_buf[dma_i] = 0x0000;
                        dma_i++;

                        /* for (i = 0; i < (dma_i + 1); i++) { */
			/*     //printf("word: %02X\n", dma_buf[i]); */
			/*   printf("word: %u %u \n", ((dma_buf[i] & 0xe000) >> 12), dma_buf[i] & 0x1fff); */
                        /* } */
                        
                        /* Calculate number of buffers used */
                        dma_chk = 1 + ((dma_i - 1)/735);
                        if (dma_chk > 16) {
                            printf("ERROR, DMA_CHK: %d\n", dma_chk);
                        }

                        //printf("dma_chk: %i\n", dma_chk);
                        //printf("dma_i %i\n", dma_i);

                        /* Make sure fifo isn't full... */
                        get_fifo_status(output_board, DM7820_FIFO_QUEUE_0,
                                        DM7820_FIFO_STATUS_FULL,
                                        &fifo_status);
                        if (!fifo_status) {
                            /* DMA write to output FIFOs */
                            dm7820_status = DM7820_FIFO_DMA_Write(output_board,
                                                                  DM7820_FIFO_QUEUE_0,
                                                                  dma_buf, dma_chk);
                            DM7820_Return_Status(dm7820_status, "DM7820_FIFO_DMA_Write");
                            
                            if (dm7820_status == 0) {
                                /* Start DMA transfer */
                                dm7820_status = DM7820_FIFO_DMA_Enable(output_board,
                                                                       DM7820_FIFO_QUEUE_0, 0xFF, 0xFF);
                                DM7820_Return_Status(dm7820_status, "DM7820_FIFO_DMA_Enable()");

                                /* Wait for DMA to write out */
                                if (dm7820_status == 0) {
                                    while(dma_flag != dma_chk) {
                                        usleep(5);
                                    }
                                    dma_flag = 0;
                                } else {
                                    printf("DMA start/enable failed!\n");
                                }

                            } else {
                                printf("Didn't start xfer due to dma write failure \n");
                            }

                            /* Clear all data that's been shipped
                               off. .*/
                            memset(dma_buf, 0, sizeof(uint16_t)*(dma_i + 1));
                            dma_i = 0;
                        } else {
                            printf("FIFO FULL!\n");
                        }

                    }
                    packet_counter_s = packet_counter;
                }
            } else {
                /* recvfrom returns negative values due to
                   non-blocking status. This just means there were no
                   packets to read! */
                break;
            }
        }
        
        usleep(5);
    }

    printf("Exited main loop \n");

    //status = close_packet_save();
    // if (status != 0) {
    //     printf("close packet save fail\n");
    // }

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

    /* Close the socket! */
    status = close(sock_fd);
    if (status < 0) {
        printf("error closing socket: %d\n", status);
    }

    printf("Total packet mismatch: %u\n", pkt_mismatch_cnt);
    printf("Total # of packets: %u\n", tot_pkt_count);

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

    /* Set all used port 2 lines as stdio output */
    dm7820_status =
        DM7820_StdIO_Set_IO_Mode(board, DM7820_STDIO_PORT_2, 0x0007,
                                 DM7820_STDIO_MODE_OUTPUT);
    if (dm7820_status < 0) {
        return -1;
    }

    /* Set all lines low */
    dm7820_status =
        DM7820_StdIO_Set_Output(board, DM7820_STDIO_PORT_2, 0x0000);
    if (dm7820_status < 0) {
        return -1;
    }

    return 0;
}

int init_output_fifo(DM7820_Board_Descriptor *board) {
    DM7820_Error dm7820_status;    

    /* Init Output FIFO */

    /* Disable FIFO 0 */
    dm7820_status = DM7820_FIFO_Enable(board, DM7820_FIFO_QUEUE_0, 0x00);
    DM7820_Return_Status(dm7820_status, "DM7820_FIFO_Enable()");
    
    /* Set FIFO input clock to PCI write */
    dm7820_status = DM7820_FIFO_Set_Input_Clock(board,
                                                DM7820_FIFO_QUEUE_0,
                                                DM7820_FIFO_INPUT_CLOCK_PCI_WRITE);
    DM7820_Return_Status(dm7820_status, "DM7820_FIFO_Set_Input_Clock()");
    
    /* Set FIFO 0 output clock to strobe 2 (NSROC strobe) */
    dm7820_status = DM7820_FIFO_Set_Output_Clock(board,
                                                 DM7820_FIFO_QUEUE_0,
                                                 DM7820_FIFO_OUTPUT_CLOCK_STROBE_2);
    DM7820_Return_Status(dm7820_status, "DM7820_FIFO_Set_Output_Clock()");

    /* Set FIFO 0 data input to PCI data */
    dm7820_status = DM7820_FIFO_Set_Data_Input(board,
                                               DM7820_FIFO_QUEUE_0,
                                               DM7820_FIFO_0_DATA_INPUT_PCI_DATA);
    DM7820_Return_Status(dm7820_status, "DM7820_FIFO_Set_Data_Input()");

    dm7820_status = DM7820_FIFO_Set_DMA_Request(board,
                                                DM7820_FIFO_QUEUE_0,
                                                DM7820_FIFO_DMA_REQUEST_WRITE);
    DM7820_Return_Status(dm7820_status, "DM7820_FIFO_Set_DMA_Request()");

    /* Set the FIFO 0 output to port 0 */
    /* The mask is 0xFFFF for a full 16 bit word */
    dm7820_status = DM7820_StdIO_Set_Periph_Mode(board,
                                                 DM7820_STDIO_PORT_0,
                                                 0xFFFF,
                                                 DM7820_STDIO_PERIPH_FIFO_0);
    DM7820_Return_Status(dm7820_status, "DM7820_StdIO_Set_Periph_Mode()");

    return 0;
}

int init_output_dma(DM7820_Board_Descriptor *board) {
    DM7820_Error dm7820_status;    

    /*  Initializing DMA 0 */
    //syslog(LOG_INFO, "Initializing DMA 0 ...");
    dm7820_status = DM7820_FIFO_DMA_Initialize(board,
                                               DM7820_FIFO_QUEUE_0,
                                               DMA_BUF_NUM, DMA_BUF_SIZE);
    DM7820_Return_Status(dm7820_status, "DM7820_FIFO_DMA_Initialize()");

    /*  Configuring DMA 0 */
    //syslog(LOG_INFO, "    Configuring DMA 0 ...");
    dm7820_status = DM7820_FIFO_DMA_Configure(board,
                                              DM7820_FIFO_QUEUE_0,
                                              DM7820_DMA_DEMAND_ON_PCI_TO_DM7820,
                                              DMA_BUF_SIZE);
    DM7820_Return_Status(dm7820_status, "DM7820_FIFO_DMA_Configure()");

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

int set_status_bit(DM7820_Board_Descriptor *board, int status_bit, int status, uint16_t *status_word) {
    DM7820_Error dm7820_status;

    switch(status_bit) {
    case 1:
        if (status) {
            (*status_word) |= TMIF_STATUS_HEART;
        } else {
            (*status_word) &= ~TMIF_STATUS_HEART;
        }
        break;
    case 2:
        if (status) {
            (*status_word) |= TMIF_STATUS_FIFO_FULL;
        } else {
            (*status_word) &= ~TMIF_STATUS_FIFO_FULL;
        }
        break;
    case 3:
        if (status) {
            (*status_word) |= TMIF_STATUS_ERR;
        } else {
            (*status_word) &= ~TMIF_STATUS_ERR;
        }
        break;
    default:
        //syslog(LOG_WARNING, "set_status_bit: invalid bit set attempt: %i", status_bit)
        printf("set_status_bit: invalid bit set attempt: %i", status_bit);
        return(-1);
        break;
    }

    
    dm7820_status = DM7820_StdIO_Set_Output(board, DM7820_STDIO_PORT_2, *status_word);
    DM7820_Return_Status(dm7820_status, "DM7820_StdIO_Set_Output()");

    return(0);
}
