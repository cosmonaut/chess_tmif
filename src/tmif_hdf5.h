#ifndef TMIF_HDF5_H_
#define TMIF_HDF5_H_

/* Author: Nicholas Nell
   email: nicholas.nell@colorado.edu

   tmif hdf5 packet struct for CHESS
*/

#include <stdint.h>

#define FILE_NAME "/home/clu/flight_data/chess_flight_data.h5"
#define TABLE_NAME "CHESS_PACKETS"
/* Number of 16-bit words in CHESS UDP packet */
#define CHESS_PACKET_LEN 735


/* SLICE word packet */
typedef struct {
    uint16_t packet[735];
    int64_t timestamp_s;
    int64_t timestamp_us;
} chess_word_packet_t;


int init_packet_save(void);
int close_packet_save(void);
int save_packet(uint16_t *);

#endif /* TMIF_HDF5_H_ */
