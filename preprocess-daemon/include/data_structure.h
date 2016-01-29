#ifndef DATA_STRUCTURE_H
#define DATA_STRUCTURE_H

/*
 * Structure describing the data input
 * buffer
 */
typedef struct data_s {
	unsigned char *ptr;
	int nb_data;
} data_t;

/*
 * Structure describing the feature vector
 * frame information such as the presence
 * of an eye-blink and such... 
 */
typedef struct frame_info_s{
	char eye_blink_detected;
}frame_info_t;

/*
 * Structure describing the feature vector 
 * it is preceded by a frame_status and
 * the actual feature vector
 */
typedef struct feature_buf_s {
	frame_info_t frame_status;
	int nb_features;
	unsigned char *featvect_ptr;
} feature_buf_t;




#endif
