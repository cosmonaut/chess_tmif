/* Author :Nicholas Nell 
   email: nicholas.nell@colorado.edu
   
   This helper function saves packets of CHESS data as they are read
   by tmif.
*/

#include <hdf5.h>
#include <hdf5_hl.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>
//#include <syslog.h>
#include <stdint.h>

#include "tmif_hdf5.h"


static hid_t fid;
static hid_t comp_tid;
static hid_t ptable;
static hid_t space;
static hid_t array_tid;
/* Holds length of CHESS UDP packet in uint16_t units */
static hsize_t packet_dim[] = {CHESS_PACKET_LEN};
/* Struct for packet and timestamp */
static int tmif_hdf5_init = 0;


// /* log any hdf5 errors that occur so we know what the hell is going on... */
// static herr_t log_err(int n, H5E_error_t *err_desc, void *client_data) {
// //static H5E_walk2_t log_err(int n, H5E_error_t *err_desc, void *client_data) {
//     const char		*maj_str = NULL;
//     const char		*min_str = NULL;
//     const int		indent = 2;

//     /* Check arguments */
//     if (err_desc == NULL) {
//         /* log error with error handler? recursion? */
//         //syslog(LOG_WARNING, "HDF5 passed NULL err_desc to its error handler... wtf?");
//         return 0;
//     }

//     /* Get descriptions for the major and minor error numbers */
//     maj_str = H5Eget_major(err_desc->maj_num);
//     min_str = H5Eget_minor(err_desc->min_num);

//     /* Log those errors, baby! */
//     printf("%*s#%03d: %s line %u in %s(): %s\n",
//            indent, "", n, err_desc->file_name, err_desc->line,
//            err_desc->func_name, err_desc->desc);
//     printf("%*smajor(%02d): %s\n",
//            indent*2, "", err_desc->maj_num, maj_str);
//     printf("%*sminor(%02d): %s\n",
//            indent*2, "", err_desc->min_num, min_str);

//     // syslog(LOG_ERR, "%*s#%03d: %s line %u in %s(): %s\n",
//     //        indent, "", n, err_desc->file_name, err_desc->line,
//     //        err_desc->func_name, err_desc->desc);
//     // syslog(LOG_ERR, "%*smajor(%02d): %s\n",
//     //        indent*2, "", err_desc->maj_num, maj_str);
//     // syslog(LOG_ERR, "%*sminor(%02d): %s\n",
//     //        indent*2, "", err_desc->min_num, min_str);

//     return 0;
// }

// /* hdf5 error handler function */
// static herr_t tmif_hdf5_error_handler(void *unused) {
//     /* Go through errors and log them... */
//     H5Ewalk(H5E_WALK_DOWNWARD, log_err, (void *)0);
//     return 0;
// }

/* Initialize all of the hdf5 items... */
int init_packet_save(void) {
    herr_t status;
    int error = 0;
    hsize_t fspace;

    /* set custom hdf5 error handler to log any errors */
    //H5Eset_auto(tmif_hdf5_error_handler, NULL);

    /* open the file or create it */
    fid = H5Fopen(FILE_NAME, H5F_ACC_RDWR, H5P_DEFAULT);
    if (fid < 0) {
        //syslog(LOG_WARNING, "WARNING: No hdf5 file exists yet!");
        printf("WARNING: No hdf5 file exists yet!\n");
        fid = H5Fcreate(FILE_NAME, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
        if (fid < 0) {
            //syslog(LOG_ERR, "Failed to create packet table file!");
            printf("Failed to create packet table file!\n");
            error++;
            return error;
        }
    }

    fspace = H5Fget_freespace(fid);
    printf("File free space: %d\n", (int)fspace);

    /* HDF5 space for packet array */
    //array_tid = H5Tarray_create(H5T_NATIVE_UINT16, 1, packet_dim, NULL);
    array_tid = H5Tarray_create(H5T_NATIVE_UINT16, 1, packet_dim);
    if (array_tid < 0) {
        printf("Failed to create array_tid. \n");
        H5Fclose(fid);
        error++;
        return error;
    }

    /* Compound type (packet struct) */
    comp_tid = H5Tcreate(H5T_COMPOUND, sizeof (chess_word_packet_t));
    if (comp_tid < 0) {
        H5Tclose(array_tid);
        H5Fclose(fid);
        error++;
        return error;
    }

    status = H5Tinsert(comp_tid, "packet", HOFFSET(chess_word_packet_t, packet), array_tid);
    if (status < 0) {
        printf("failed to insert array\n");
        error++;
    }

    status = H5Tinsert(comp_tid, "timestamp_s", HOFFSET(chess_word_packet_t, timestamp_s), H5T_NATIVE_LLONG);
    if (status < 0) {
        //syslog(LOG_ERR, "H5Tinsert failed for timestamp_s");
        error++;
    }

    status = H5Tinsert(comp_tid, "timestamp_us", HOFFSET(chess_word_packet_t, timestamp_us), H5T_NATIVE_LLONG);
    if (status < 0) {
        //syslog(LOG_ERR, "H5Tinsert failed for timestamp_us");
        error++;
    }

    /* open or create the packet table */
    ptable = H5PTopen(fid, TABLE_NAME);
    if (ptable == H5I_BADID) {
        //syslog(LOG_WARNING, "WARNING: H5PTopen found no packet table yet...");
        printf("warn: no packet table found yet...\n");
        //ptable = H5PTcreate_fl(fid, TABLE_NAME, data_tid, (hsize_t)100, -1);
        ptable = H5PTcreate_fl(fid, TABLE_NAME, comp_tid, (hsize_t)1000, -1);
        if (ptable == H5I_BADID) {
            printf("failed to create pt?\n");
            //syslog(LOG_ERR, "Packet table creation failed");
            error++;
            H5Tclose(array_tid);
            H5Tclose(comp_tid);
            H5Fclose(fid);
            return error;
        }
    }

    /* Validate packet table... */
    status = H5PTis_valid(ptable);
    if (status < 0) {
        //syslog(LOG_ERR, "hdf5 file does not contain valid packet table");
        error++;
        H5Tclose(array_tid);
        H5Tclose(comp_tid);
        H5PTclose(ptable);
        H5Fclose(fid);
        return error;
    }

    tmif_hdf5_init = 1;
    return 0;
}

int close_packet_save(void) {
    herr_t status;
    int error = 0;

    /* Close all of the hdf5 data type memory */

    status = H5Tclose(comp_tid);
    if (status < 0) {
        error++;
    }
    status = H5Tclose(array_tid);
    if (status < 0) {
        error++;
    }
    status = H5PTclose(ptable);
    if (status < 0) {
        error++;
    }
    status = H5Fclose(fid);
    if (status < 0) {
        error++;
    }

    tmif_hdf5_init = 0;
    return error;
}

/* Note chess_pkt must be 735 in length. */
int save_packet(uint16_t *chess_pkt) {
    herr_t status;
    /* Struct for packet and timestamp */
    chess_word_packet_t data;
    struct timeval ts;
    int error = 0;


    if (tmif_hdf5_init) {
        /* Set the data */
        // for (i = 0; i < CHESS_PACKET_LEN; i++) {
        //     data.packet[i] = chess_pkt[i];
        // }
        memcpy(data.packet, chess_pkt, sizeof(uint16_t)*CHESS_PACKET_LEN);
    
        /* create timestamp */
        gettimeofday(&ts, NULL);
        data.timestamp_s = (int64_t)ts.tv_sec;
        data.timestamp_us = (int64_t)ts.tv_usec;

        /* Validate packet table... */
        status = H5PTis_valid(ptable);
        if (status == 0) {
        
            /* Append the packet */
            status = H5PTappend(ptable, (hsize_t)1, &data);
            if (status < 0) {
                //syslog(LOG_ERR, "Failed to append packet to table!");
                printf("Failed to append\n");
                error++;
            }

            /* Flush the file to disk */
            status = H5Fflush(fid, H5F_SCOPE_LOCAL);
            if (status < 0) {
                //syslog(LOG_ERR, "Failed to flush hdf5 file");
                printf("Failed to flush file\n");
                error++;
            }
        } else {
            printf("PACKET TABLE IS NOT VALID!\n");
        }
    } else {
        error++;
        printf("TMIF_HDF5 has not been initialized yet...\n");
    }

    return error;
}
