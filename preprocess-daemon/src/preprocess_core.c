/**
 * @file preprocess_core.c
 * @author Frederic Simard (fred.simard@atlantsembedded.com) | Atlants Embedded
 * @brief Signal preprocessing core. It can let the raw data flow through,
 *        compute the fft or the EEG freq power bands.
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fft.h>

#include "xml.h"
#include "preprocess_core.h"
#include "data_structure.h"

/*eye blink detector defines*/
#define COUNT_THRESHOLD 2
#define SIGNAL_THRESHOLD -90
#define CHAN_1 0
#define CHAN_4 3
#define EYE_BLINK_LENGTH_IN_SECONDS 0.5

static int next_frame_has_blink = 0;
static int eye_blink_length = 0;

/*data properties*/
static int nb_channels = 0;
static int nb_samples = 0;
static int feature_vect_length = 0;

/*preprocessing options*/
static char timeseries_enabled = 0x00;
static char fft_enabled = 0x00;
static char alpha_pwr_enabled = 0x00;
static char beta_pwr_enabled = 0x00;
static char gamma_pwr_enabled = 0x00;
static char eye_blink_detect = 0x00;

/*prepocessing buffers*/
double* signals_avg_vector = NULL;
double* signals_wo_avg = NULL;
double* signals_transposed = NULL;
double* dft_vector = NULL;

/*function required for preprocessing*/
void remove_mean_col(double *a, double *mean, double *b, int dim_i, int dim_j);
void stat_mean(double *a, double *mean, int dim_i, int dim_j);
void mtx_transpose(double *A, double *A_prime, int dim_i, int dim_j);
void abs_dft_interval(const double *signal, double *abs_power_interval, int n, int interval_start, int interval_stop);

char has_eye_blink(const double *signal, int dim_i, int dim_j);

/**
 * int init_preprocess_core(appconfig_t *config)
 * @brief initialize the preprocessing core
 * @param config, configuration options
 * @return EXIT_SUCCESS or EXIT_FAILURE
 */
int init_preprocess_core(appconfig_t *config){
	
	/*copy infos about input data*/
	nb_channels = config->nb_channels;
	nb_samples = config->window_width;
	feature_vect_length = 0;
	
	//to do describe sampling frequency
	//eye_blink_length = ceil((double)config->sampling_freq*EYE_BLINK_LENGTH_IN_SECONDS);
	eye_blink_length = 100;
	eye_blink_detect = config->muse_eyeblink_detect;
	
	/*adjust the size of the feature buffer according to selected options*/ 
	/*timeseries*/
	if(config->timeseries){
		feature_vect_length += config->window_width*config->nb_channels;
		timeseries_enabled = 0x01;
	}
	
	/*fft*/
	if(config->fft){
		feature_vect_length += config->window_width/2*config->nb_channels;
		fft_enabled = 0x01;
	}
	
	/*EEG power bands*/
	/*alpha*/
	if(config->power_alpha){
		feature_vect_length += config->nb_channels;
		alpha_pwr_enabled = 0x01;
	}
	/*beta*/
	if(config->power_beta){
		feature_vect_length += config->nb_channels;
		beta_pwr_enabled = 0x01;
	}
	/*gamma*/
	if(config->power_gamma){
		feature_vect_length += config->nb_channels;
		gamma_pwr_enabled = 0x01;
	}
	
	/*allocate memory for buffer required during processing*/
	signals_avg_vector = (double*)malloc(config->nb_channels*sizeof(double));
	signals_wo_avg = (double*)malloc(config->nb_channels*config->window_width*sizeof(double));
	signals_transposed = (double*)malloc(config->nb_channels*config->window_width*sizeof(double));
	dft_vector = (double*)malloc(feature_vect_length*sizeof(double));
	
	/*done*/
	return EXIT_SUCCESS;
}

/**
 * int cleanup_preprocess_core()
 * @brief cleanup preprocessing core
 * @return EXIT_SUCCESS
 */
void cleanup_preprocess_core(){
	
	/*free the memory*/
	free(signals_avg_vector);
	free(signals_wo_avg);
	free(signals_transposed);
	free(dft_vector);
}

/**
 * int preprocess_data(data_t* data_input, feature_buf_t* feature_output)
 * @brief Transform the raw data into a feature vector, according to selected options
 * @param data_input(in), input buffer
 * @param feature_output(out), output buffer
 * @return EXIT_SUCCESS or EXIT_FAILURE
 */
int preprocess_data(data_t* data_input, feature_buf_t* feature_output){
	
	int i,j;
	int offset = 0;
	double* signals_array = (double*)data_input->ptr;
	double* features_array = (double*)feature_output->featvect_ptr;
	
	/*re-init frame status*/
	memset(&(feature_output->frame_status), 0, sizeof(frame_info_t));
	
	/*compute the average of each signal*/
	stat_mean(signals_array, signals_avg_vector, nb_samples, nb_channels);
	
	/*remove the average of the signals (DC component)*/
	remove_mean_col(signals_array, signals_avg_vector, signals_wo_avg, nb_samples, nb_channels);
    
	/*compute the average of each signal*/
	stat_mean(signals_wo_avg, signals_avg_vector, nb_samples, nb_channels);
    
	/*transpose the signal array*/
	mtx_transpose(signals_wo_avg, signals_transposed, nb_samples, nb_channels);
	
	/*TODO: raw data pass-through*/
	if(timeseries_enabled){
		printf("timeseries not implemented yet!");
	}
	
	/*If fft active*/
	if(fft_enabled){
		/*compute abs power-dft for each each channel*/
		for(i=0;i<nb_channels;i++){
			
			/*squared dft values*/
			abs_dft_interval(&(signals_transposed[i*nb_samples]), dft_vector, nb_samples, 0, nb_samples/2);
			
			/*copy to output buffer*/
			for(j=0;j<nb_samples/2;j++){
				features_array[i*nb_samples/2+ j + offset] = dft_vector[j];
                
			}
		}
	}
	
	/*TODO: EEG power band:alpha*/
	
	/*TODO: EEG power band:beta*/
	
	/*TODO: EEG power band:gamma*/
	
	/*Eye blink detection*/
	if(eye_blink_detect){
		feature_output->frame_status.eye_blink_detected = has_eye_blink(signals_wo_avg,nb_samples,nb_channels);
	}else{
		feature_output->frame_status.eye_blink_detected = 0;
	}
	
	/*TODO: artifact detection*/
	
	return EXIT_SUCCESS;
}

/**
 * char has_eye_blink(const double *signal, int dim_i, int dim_j)
 * @brief Detects if an eye blink is present in that frame and whether the artifact
 *        spans to other frames. Designed for the Muse (Interaxon)
 * @param signal, matrix with recording channels as columns and time samples as lines
 * @param dim_i, number of time samples 
 * @param dim_i, number of channels
 * @return 1 if frame is flagged as having a blink, 0 otherwise
 */
char has_eye_blink(const double *signal, int dim_i, int dim_j){
	
	char blink_onset_found = 0x00;
	int onset_timestamp = 0;
	unsigned char nb_blink_samples = 0;
	int time_iter = 0;
	
	/*if frame is not flagged*/
	if(!next_frame_has_blink){
		
		/*until eye_blink condition is reached or end of frame*/
		while(nb_blink_samples<COUNT_THRESHOLD && time_iter<dim_i){
		
			/*in chan 1*/
			if(signal[dim_j*time_iter+CHAN_1]<SIGNAL_THRESHOLD){	
				/*	-look for the first sample below the threshold*/
				if(!blink_onset_found){
					onset_timestamp = time_iter;
					blink_onset_found = 0x01;
				}
				/*	-sum the number of samples below the threshold*/
				nb_blink_samples++;
			}
			
			/*in chan 1*/
			if(signal[dim_j*time_iter+CHAN_4]<SIGNAL_THRESHOLD){
				/*	-look for the first sample below the threshold*/
				if(!blink_onset_found){
					onset_timestamp = time_iter;
					blink_onset_found = 0x01;
				}
				/*	-sum the number of samples below the threshold*/
				nb_blink_samples++;
			}
			
			/*to next line*/
			time_iter++;
		}	
	
		/*check if blink span to the next frame(s)*/
		if(blink_onset_found){
			
			/*compute how many frames need to be flagged*/
			for(time_iter=eye_blink_length-(nb_samples-onset_timestamp);time_iter>0;time_iter-=nb_samples){
				next_frame_has_blink++;
			}
			
			/*flag that frame*/
			return 0x01;
		}
			
	}else{
		/*decrement blink count*/
		next_frame_has_blink--;
		/*flag that frame*/
		return 0x01;
	}

	/*no blink in that frame*/
	return 0x00;
}

/**
 * void remove_mean_col(double *a, double *mean, double *b, int dim_i, int dim_j)
 * @brief This should come from stats_lib.so
 */
void remove_mean_col(double *a, double *mean, double *b, int dim_i, int dim_j){
	 
	int i,j;
	
	/*remove the mean of all data samples*/
	for (i=0;i<dim_i;i++){
        for (j=0;j<dim_j;j++){
            b[i*dim_j+j] = a[i*dim_j+j]-mean[j];
		} 
	}
}

/**
 * void remove_mean_col(double *a, double *mean, double *b, int dim_i, int dim_j)
 * @brief This should come from stats_lib.so
 */
void stat_mean(double *a, double *mean, int dim_i, int dim_j){
	
	int i,j,n;
	n=0;
	
    for (j=0;j<dim_j;j++){
        mean[j] = 0;
    }
    
	/*Sum over each column*/
	for(i=0;i<dim_i;i++){
		 n = i*dim_j;
		 for (j=0;j<dim_j;j++){
				mean[j] += a[j+n];
		 }
	}
	
	/*Divide to get average*/
	for (j=0;j<dim_j;j++){
		mean[j] /= dim_i;  
	}
  
}

/**
 * void remove_mean_col(double *a, double *mean, double *b, int dim_i, int dim_j)
 * @brief This should come from lin_algebra.so
 */
void mtx_transpose(double *A, double *A_prime, int dim_i, int dim_j){
	
	int i,j = 0;
	
	/*transpose the matrix such that A'[j,i] = A[i,j]*/
	for (i = 0; i < dim_i; i++){
		for (j = 0; j < dim_j; j++){
			A_prime[j*dim_i + i] = A[i*dim_j + j];
		}
	}
}

