# House of IO - Heap Reuse

## What Is This? 
- A heap exploitation technique that allows for returns an arbitrary memory address from Malloc. 
- An exploit primitive improvement for the [House of IO - Underflow](https://awaraucom.wordpress.com/2020/07/19/house-of-io-remastered/).
- A bypass for pointer mangling.

## How Does this Work? 
- Overwritting the TCache Bin struct pointers, since it is on the heap itself. This allows an arbitrary chunk to be returned from Malloc. 

## What Is the Primitive Improvement? 
- The original technique uses an out of bounds write going backwards in order to corrupt the TCache Bins data structure to add an arbitrary memory location to the data struture. The underflow is required because the TCache Bin struct is normally the first allocation of the heap. 
- By using careful manipulating of the heap memory via threading, it is possible to put the TCache Bin struct in the middle of the heap. This is because the TCache Bin struct is unique for *threads* and the heap memory can be *reused* between threads. By combining both of these properties, it is possible to corrupt the TCache Bin struct with a more common primitive, such as a use after free, buffer overflow or double free. 

## Damn, this sounds real complex
- Yeah, this is!
- I broke this down in an article on my blog called [House of IO - Heap Reuse](https://maxwelldulin.com/BlogPost?post=6295828480). This has an overview of everything about the allocator that is needed to understand the technique and a full explanation of how the technique works with pretty diagrams. 

## What's In this Repo Then? 
- A *proof of concept* of the House of IO - Heap Reuse on GLibC Malloc 2.31. Theoretically, this should work on all versions of Malloc 2.30+. 
- The reason for this difference in versions is that from GLibC Malloc 2.26-2.29 the *counts* array of the TCache Bin has slightly different storage size (char vs. uint16_t). 

## Pros and Cons

<h3>
Pros
</h3>

<p>
<ul>
<li>Bypasses pointer mangling for the classic fd poison attack.</li>
<li>Relative overwrites on the heap pointers in the TCache Bin data structures can be used to bypass heap ASLR. </li>
<li>Multiple fake pointers can be written to the TCache Bin, if NULLbytes can be used. This allows for <i>multiple</i> write primitives.</li>
<li>Added improvement and flexibility on the required exploit primitive when compared to the original <i>House of IO - Underflow.</i></li>
</ul>
</p>

<h3>
Cons
</h3>
<p>
<ul>
<li>Practically, the settings for the arenas do not make it trivial to reuse heaps. This means that a way to exhaust threads for a process in the application is required. This is the highest bar of this technique.</li>
<li>A leak for another address, such as LibC, .bss or other sections of memory may be required.</li>
<li>If a continuous write is done from the bottom of the chunk, this destroys the TCache <code>counts</code> array. From this point on, all bins will have chunks in them, according to the allocator. </li>
</ul>
</p>
