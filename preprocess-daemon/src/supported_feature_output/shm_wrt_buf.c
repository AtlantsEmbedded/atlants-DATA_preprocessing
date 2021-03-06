/**
 * @file shm_wr_buf.c
 * @author Frederic Simard, Atlants Embedded (fred.simard@atlantsembedded.com)
 * @brief feature output interface, initialize a shared memory for feature output.
 *        Shared memory synchronization is ensured by IPC semaphore, shared with the
 * 		  process that captures the features.
 * 		  It's designed as a synchronous process, with blocking calls
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>

#include "data_structure.h"
#include "feature_output.h"
#include "shm_wrt_buf.h"

/*feature output interface object definition*/
typedef struct shm_output_s{
	
	shm_output_options_t options; /*interface options*/
	
	int shmid; /*id of the shared memory array*/
	char* shm_buf; /*pointer to the beginning of the shared buffer*/

	int page_size; /*size of a page of the buffer in bytes*/
	int total_buffer_size; /*total size of the buffer in bytes*/
	int current_page; /*id of the current page*/

	int semid; /*id of semaphore set*/
	struct sembuf *sops; /* pointer to operations to perform */

}shm_output_t;

/**
 * void* shm_wrt_init(void *options)
 * @brief Setups the shared memory output (memory allocation, linking and semaphores)
 * @param options, options for the shared memory buffer
 * @return pointter to feature output or NULL if invalid
 */
void* shm_wrt_init(void *options){
	
	/*allocate the memory for the shared memory interface*/
	shm_output_t* this_shm = (shm_output_t*)malloc(sizeof(shm_output_t));
	
	/*copy options*/
	memcpy(&(this_shm->options),options, sizeof(shm_output_options_t));
	
	/*compute a few properties of the buffer*/
	/*page size*/
	this_shm->page_size = this_shm->options.nb_features*sizeof(double) + this_shm->options.frame_status_size;
	/*buffer size*/
	this_shm->total_buffer_size = this_shm->page_size*this_shm->options.buffer_depth;
		        
    /*initialise the shared memory array*/
	if ((this_shm->shmid = shmget(this_shm->options.shm_key, this_shm->total_buffer_size, IPC_CREAT | 0666)) < 0) {
        perror("Data preprocessing: error shmget");
        return NULL;
    }
    	
    /*Now we attach it to our data space*/
    if ((this_shm->shm_buf = shmat(this_shm->shmid, NULL, 0)) == (char *) -1) {
        perror("Data preprocessing: error shmat");
        return NULL;
    }
    
    /*Access the semaphore array*/
    if ((this_shm->semid = semget(this_shm->options.sem_key, NB_SEM, IPC_CREAT | 0666)) == -1) {
		perror("semget failed\n");
		return NULL;
    } 
    
	/*allocate the memory for the pointer to semaphore operations*/
	this_shm->sops = (struct sembuf *) malloc(sizeof(struct sembuf));
	
	/*init current page id*/
	this_shm->current_page = 0;
	
	/*return the pointer*/
	return this_shm;
}

/**
 * int shm_wrt_write_to_buf(void *param)
 * @brief Writes the feature contained in the buffer to the shared memory
 * 		  It's a blocking call
 * @param feature_buf, feature buffer to be written to the output buffer
 * @param shm_interface, pointer to the shared memory output interface
 * @return EXIT_SUCCESS or EXIT_FAILURE
 */
int shm_wrt_write_to_buf(void *shm_interface, void *feature_vect_struct){
	
	/*cast/copy the pointers*/
	feature_buf_t *feat_vect = (feature_buf_t *) feature_vect_struct;
	
	shm_output_t *this_shm = (shm_output_t *)shm_interface;
	
	/*shared memory offset*/
	int write_ptr;

	/*check if free slot available*/
	this_shm->sops->sem_num = APP_IN_READY; /*sem: app ready for data*/
	this_shm->sops->sem_op = -1; /*decrement semaphore*/
	this_shm->sops->sem_flg = IPC_NOWAIT; /*undo if fails and non-blocking call*/	
	
	if(semop(this_shm->semid, this_shm->sops, 1) == 0){
		
		/*yes, write a feature vector:*/
		
		/*-compute the write location*/
		write_ptr = this_shm->page_size*this_shm->current_page;
		
		/*-write frame information and move pointer*/
		memcpy((void*)&(this_shm->shm_buf[write_ptr]),(void*)&(feat_vect->frame_status), sizeof(frame_info_t));
		write_ptr+=sizeof(frame_info_t);
		
		/*-write feature vector*/
		memcpy((void*)&(this_shm->shm_buf[write_ptr]),(void*)feat_vect->featvect_ptr, feat_vect->nb_features*sizeof(double));
		
		/*update page*/
		this_shm->current_page += 1;
		this_shm->current_page %= this_shm->options.buffer_depth;
		
		/*-post the semaphore*/
		this_shm->sops->sem_num = PREPROC_OUT_READY;
		this_shm->sops->sem_op = 1;
		this_shm->sops->sem_flg = SEM_UNDO | IPC_NOWAIT;
		semop(this_shm->semid, this_shm->sops, 1);
	}
	else{
		/*no, skip do nothing*/
	}

	/*done*/
	return EXIT_SUCCESS;
}

/**
 * int shm_wrt_cleanup(void* shm_interface)
 * @brief Clean up the shared memory: detach, deallocate mem and sem
 * @param shm_interface, pointer to the interface
 * @return EXIT_FAILURE for unknown type, EXIT_SUCCESS for known/success
 */
int shm_wrt_cleanup(void* shm_interface){
	
	shm_output_t *this_shm = (shm_output_t *)shm_interface;
	
	/* Detach the shared memory segment. */
	shmdt(this_shm->shm_buf);
	/* Deallocate the shared memory segment. */
	shmctl(this_shm->shmid, IPC_RMID, 0);
	/* Deallocate the semaphore array. */
	semctl(this_shm->semid, 0, IPC_RMID, 0);
	
	/*free the memory*/
	free(this_shm->sops);
	free(this_shm);
	
	return EXIT_SUCCESS;
}
