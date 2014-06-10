// CacheSimulator.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "Windows.h"
#include "stdlib.h"
#include "conio.h"

#define	MAX_INPUT_LENGTH	256

#define	MAX_ADDRESS_BITS 32
#define	CACHE_ASSOSIATIVITY_SIZE	8
#define	CACHE_LINES					16*1024
#define	CACHE_LINE_DATA_SIZE		64

#define	INPUT_READ_REQUEST_DATA_COMMAND		0
#define	INPUT_WRITE_REQUEST_DATA_COMMAND	1
#define	INPUT_READ_REQUEST_INSTR_COMMAND	2
#define	INPUT_SNOOP_INVALIDATE_COMMNAD		3
#define	INPUT_SNOOP_READ_COMMAND			4
#define	INPUT_SNOOP_WRITE_COMMAND			5
#define	INPUT_SNOOP_READ_INTENT_MODIFY_COMMAND	6
#define	INPUT_CLEAR_CACHE_RESET_COMMAND		8
#define	INPUT_PRINT_CACHE_STATE				9

#define	BYTE_MEMORY_READ				1
#define	BYTE_MEMORY_WRITE				2

#define	MAX_MESI_STATES	4
#define	MAX_INPUT_OPERATIONS 4
#define	MAX_SNOOP_RESPONSES	3

int				g_cache_data_bits;
int				g_cache_index_bits;
int				g_cache_tag_bits;

unsigned long	g_curr_line_feed_no = 0;

unsigned long	g_cache_data_offset_mask = 0;
unsigned long	g_cache_line_index_mask = 0;
unsigned long	g_cache_tag_mask = 0;

typedef enum _cache_operation {
	neither_operation = 0,
	read_operation,
	write_operation,
	rfo
} cache_operation;

typedef enum _mesi_state {
	unreachable_state = -1,
	invalid = 0,
	shared,
	exclusive,
	modified
} mesi_state;

// I means Invalid, S means shared, M means modified, E means exclusive
const char g_cache_state_str[] = "ISEM";

typedef enum _snoop_response {
	no_snoop_response=0,
	snoop_hit,
	snoop_hitm
} snoop_response;

typedef struct _cache_entry {
	bool			valid;
	char			cache_data[CACHE_LINE_DATA_SIZE];
	unsigned long	cache_tag;
	unsigned short	cache_lru_count;
	mesi_state		mesi_state;
} cache_entry, *pcache_entry;

typedef struct _cache_context {
	unsigned long		size;
	cache_entry			cache[CACHE_LINES][CACHE_ASSOSIATIVITY_SIZE];
	int					max_lru_index[CACHE_LINES];
	unsigned long long	cache_reads;
	unsigned long long	cache_writes;
	unsigned long long	hitcount;
	unsigned long long	misscount;
} cache_context, *pcache_context;

#define	GET_CACHE_INDEX(address)			((address & g_cache_line_index_mask)>>g_cache_data_bits)
#define	GET_CACHE_LINE_BYTE_OFFSET(address)	((address & g_cache_data_offset_mask))
#define	GET_CACHE_TAG(address)				((address & g_cache_tag_mask) >> (g_cache_data_bits+g_cache_index_bits))

/*
read_cache is a read on cache  L2 from L1.
It can either find the entry in cache or it will have to read it from main memory.
- In case it is found in cache, counters will be incremented accordingly function will return 0.
- In case it is not found in cache, counters will be incremented accordingly and contents will be brought into main memory.
  function will return 1.
*/

int read_cache(unsigned long address);

/*
write_cache is a read on cache  L2 from L1.
It can either find the entry in cache or it will have to read it from main memory and then perform the write on it.
- In case it is found in cache, counters will be incremented accordingly, dirty bit will be set, function will return 0.
- In case it is not found in cache, counters will be incremented accordingly and contents will be brought into main memory.
  function will return 1.
*/
int write_cache(unsigned long address);

mesi_state snoop_invalidate_cache_entry(int cache_index, int set_index,  int byteoffset);

mesi_state snoop_invalidate(unsigned long  address);

void print_shared_bus_operations(unsigned long address, char operation);
//int snoop_read(unsigned long  address);

//int snoop_write(unsigned long address);

//int snoop_read_intent_to_modify(unsigned long address);

void reset_clear_cache();

int print_valid_cachelines_state();

int PutSnoopResult(unsigned long address, char operation);

int GetSnoopResult(unsigned long address, char operation);

void fix_up_lru_vals_in_line(int cache_index, unsigned short prev_lru);

int find_cache_column_number(int cache_index, unsigned long cache_tag);

cache_context	g_cache;

mesi_state g_mesi_state_matrix[MAX_MESI_STATES][MAX_INPUT_OPERATIONS][MAX_SNOOP_RESPONSES] = 
							{{{invalid, invalid, invalid}, {exclusive, shared, shared}, {modified, modified, modified}, {invalid, invalid, invalid}},
							 {{shared, shared, shared}, {shared, shared, unreachable_state}, {modified, modified, unreachable_state}, {invalid, invalid, unreachable_state}},
							 {{exclusive, exclusive, exclusive}, {exclusive, unreachable_state, unreachable_state}, {modified, unreachable_state, unreachable_state}, {invalid, unreachable_state, unreachable_state}},
							 {{modified, modified, modified}, {modified, unreachable_state, unreachable_state}, {modified, unreachable_state, unreachable_state}, {invalid, unreachable_state, unreachable_state}}
							};

int print_valid_cachelines_state()
{
	int	i = 0, j = 0;
	int max_lru_column_no = 0;
	int valid_cache_count = 0;

	while (i < CACHE_LINES)
	{
		j = 0;
		max_lru_column_no = g_cache.max_lru_index[i];

		if (g_cache.cache[i][max_lru_column_no].cache_lru_count != 0)
		{
			while (j < CACHE_ASSOSIATIVITY_SIZE)
			{
				if (g_cache.cache[i][j].valid)
				{
					// This is unreachable, code should never ever reach this condition
					if (g_cache.cache[i][j].mesi_state == unreachable_state)
					{
						printf("print_valid_cachelines_state: There is a bug in implementation, it shouldn't have reached here\n");
						//__debugbreak();
					}
					else
					{
						printf("cache line: %d, set: %d, tag: 0x%x, lru_count:%d, mesi_state:%c\n", i, j, 
								g_cache.cache[i][j].cache_tag, g_cache.cache[i][j].cache_lru_count, g_cache_state_str[g_cache.cache[i][j].mesi_state]);
						valid_cache_count++;
					}
				}
				j++;
			}
		}
		i++;
	}

	printf ("# of valid entries in cache = %d\n", valid_cache_count);
	printf ("Hit Ratio is %d\n", (g_cache.hitcount*100)/(g_cache.hitcount+g_cache.misscount));
	return valid_cache_count;
}

// This function should be called only if you already know that there is an empty column
int cache_find_empty_column(int cache_index)
{
	int j = 0;

	while (j < CACHE_ASSOSIATIVITY_SIZE)
	{
		if (!g_cache.cache[cache_index][j].valid)
		{
			break;
		}
		j++;
	}

	// This should be unreachable code, this condition should never ever arise.
	// If it does there is a bug in algorithm and program, lets break here
	if (j==CACHE_ASSOSIATIVITY_SIZE)
	{
		printf("cache_find_empty_column: There isn't a single empty set for cache line number %x, but lru count is less than set associativity size, hence bug in code, go read the code\n", cache_index);
		//__debugbreak();
	}

	return j;
}

int cache_find_maxlru_column(int cache_index)
{
	int j = 0;
	int max_lru_column = 0;
	unsigned short max_lru = 0;

	while (j < CACHE_ASSOSIATIVITY_SIZE)
	{
		if (max_lru < g_cache.cache[cache_index][j].cache_lru_count)
		{
			max_lru_column = j;
			max_lru = g_cache.cache[cache_index][j].cache_lru_count;
		}
		j++;
	}

	return max_lru_column;
}

int cache_find_leastlru_column(int cache_index)
{
	int j = 0;

	while (j < CACHE_ASSOSIATIVITY_SIZE)
	{
		if (g_cache.cache[cache_index][j].cache_lru_count == 1)
		{
			break;
		}
		j++;
	}

	// This should be unreachable code, this condition should never ever arise.
	// If it does there is a bug in algorithm and program, lets break here
	if (j == CACHE_ASSOSIATIVITY_SIZE)
	{
		printf("cache_find_leastlru_column: Not a single set in cache line no %x having lru val = 1, bug in code, go read the code\n", cache_index);
		//__debugbreak();
	}

	return j;
}

void write_back_cache_entry(int cache_index, int set_index, int byteoffset)
{
	unsigned long address;
	unsigned long cache_tag;
	if (g_cache.cache[cache_index][set_index].mesi_state == modified)
	{
		cache_tag = g_cache.cache[cache_index][set_index].cache_tag;
		address = ((cache_tag << (g_cache_data_bits+g_cache_index_bits)) | (cache_index<<g_cache_data_bits) | (byteoffset));
		// Don't need to do anything special, just print on console that a write is going to be performed on shared bus
		print_shared_bus_operations(address, 'W');
	}
	// Else this function shouldn't have been called
	else
	{
		printf("write_back_cache_entry: called for cache line %x, set no %x but state is not in modified, bug in code, go read the code\n", cache_index, set_index);
		//__debugbreak();
	}
}

// This funcrtion evicts the cache entry and returns its mesi state
mesi_state evict_cache_entry(int cache_index, int set_index, int byteoffset)
{
	unsigned long address;
	unsigned long cache_tag;
	cache_tag = 0;
	cache_tag = g_cache.cache[cache_index][set_index].cache_tag;
	address = ((cache_tag << (g_cache_data_bits+g_cache_index_bits)) | (cache_index<<g_cache_data_bits) | (byteoffset));
	// Evict only if valid
	if (g_cache.cache[cache_index][set_index].valid)
	{
		// If modified state, we need to write it back to main memory
		if (g_cache.cache[cache_index][set_index].mesi_state == modified)
		{
			write_back_cache_entry(cache_index, set_index, byteoffset);
		}
		printf ("Sending this address %x to L1 cache to ensure eviction from L1 as it is evicted from L2\n", address);
	}
	return g_cache.cache[cache_index][set_index].mesi_state;
}

int cache_handle_miss(int cache_index, int byteoffset)
{
	int				j = 0;
	unsigned short	max_lru_count = 0, min_lru_count_set_index = 0;
	int				column_no = 0;

	max_lru_count = g_cache.cache[cache_index][g_cache.max_lru_index[cache_index]].cache_lru_count;

	if (max_lru_count < CACHE_ASSOSIATIVITY_SIZE)
	{
		column_no = cache_find_empty_column(cache_index);
		max_lru_count++;
	}
	else
	{
		column_no = cache_find_leastlru_column(cache_index);
		evict_cache_entry(cache_index, column_no, byteoffset);
		fix_up_lru_vals_in_line(cache_index, 1);
		memset(&g_cache.cache[cache_index][column_no], 0, sizeof(cache_entry));
	}

	g_cache.cache[cache_index][column_no].valid = true;
	g_cache.cache[cache_index][column_no].cache_lru_count = max_lru_count;
	g_cache.max_lru_index[cache_index] = column_no;

	return column_no;
}

void read_from_main_memory(int cache_index, int *set_index, int byteoffset)
{
	int j = 0;

	j = cache_handle_miss(cache_index, byteoffset);
	g_cache.cache[cache_index][j].cache_data[byteoffset] = BYTE_MEMORY_READ;
	*set_index = j;
}

void write_to_cache(int cache_index, int *set_index, int byteoffset)
{
	int j = 0;

	j = cache_handle_miss(cache_index, byteoffset);

	g_cache.cache[cache_index][j].cache_data[byteoffset] = BYTE_MEMORY_WRITE;
	*set_index = j;
}

int GetSnoopResult(unsigned long address, char operation)
{

	snoop_response response = no_snoop_response;
	
	if (address%2 == 0)
	{
		response = no_snoop_response;
	}
	else if (address%3 == 0)
	{
		response = snoop_hit;
	}
	else
	{
		// Since we are returning hitm, we need to write back this to main memory, because someone else is going to access it
		// print_shared_bus_operations(address, 'W');
		response = snoop_hitm;
	}

	return response;
}

void print_snoop_response(unsigned long address, snoop_response	response)
{
	switch (response)
	{
		case no_snoop_response:
			printf("SR no HIT\n");
			break;
		case snoop_hit:
			printf("SR HIT\n");
			break;
		case snoop_hitm:
			printf("SR HITM\n");
			break;
		default:
			printf("SR unrecognized snoop  %d result\n", response);
			break;
	}
}

int PutSnoopResult(unsigned long address, char operation)
{
	int cache_index = 0, set_index = 0;
	int j = 0;
	snoop_response response = no_snoop_response;
	unsigned long cache_tag  = 0;
	mesi_state	current_state = unreachable_state, new_mesi_state = unreachable_state;

	cache_index = GET_CACHE_INDEX(address);
	cache_tag = GET_CACHE_TAG(address);
	j = find_cache_column_number(cache_index, cache_tag);

	// If j >= CACHE_ASSOSIATIVITY_SIZE, then entry was not in cache, means invalid state
	if (j >= CACHE_ASSOSIATIVITY_SIZE)
	{
		print_snoop_response(address, response);
		return no_snoop_response;
	}
	else
	{
		current_state = g_cache.cache[cache_index][j].mesi_state;
	}

	operation = toupper(operation);

	switch (current_state)
	{
		case invalid:
			response = no_snoop_response;
			//new_mesi_state = current_state;
			break;
		case shared:
			response = snoop_hit;
			if (/*operation == 'W' || */operation == 'M')
			{
				new_mesi_state = invalid;
			}
			else if (operation == 'R')
			{
				new_mesi_state = current_state;
			}
			// This case should never ever arise because if we are in shared, no one else can write back to main memory
			// If someone is writing back to main memory that means that was in modified and hence should have sent Invalidate (I) to us
			else
			{
				printf("PutSnoopResult: Invalid state reached, cache is in shared state, writeback not possible, address %x, input line no. %d\n", address, g_curr_line_feed_no);
				//__debugbreak();
			}
			break;
		case exclusive:
			response  = snoop_hit;
			if (operation == 'R')
			{
				new_mesi_state = shared;
			}
			if (operation == 'M')
			{
				new_mesi_state = invalid;
			}
			// Below case should never ever reach, if it does there is a bug in program or the input
			// We can never be in exclusive and get a write on another cache (when did the other cache read it, we should have been in shared not in exclusive)
			if (operation == 'W')
			{
				printf("PutSnoopResult: Invalid state reached, cache is in exclusive state, writeback not possible, address %x, input line no. %d\n", address, g_curr_line_feed_no);
				//__debugbreak();
			}
			break;
		case modified:
			response = snoop_hitm;
			if (operation == 'R')
			{
				new_mesi_state = shared;
			}
			if (operation == 'M')
			{
				new_mesi_state = invalid;
			}
			// Below case should never ever reach, if it does there is a bug in program or the input
			// We can never be in modified and get a write on another cache (when did the other cache read it, we should have been in shared not in modified)
			if (operation == 'W')
			{
				printf("PutSnoopResult: Invalid state reached, cache is in modified state, writeback from other cache not possible, address %x, input line no. %d\n", address, g_curr_line_feed_no);
				//__debugbreak();
			}
			break;
		default:
			// If it reaches here there is a bug in code
			printf("PutSnoopResult: Cache is currently in undefined state, go back read the code, there is a bug in it\n");
			//__debugbreak();
			break;
	}

	// If new state is going to be unreachable we better not execute below code
	if (new_mesi_state == unreachable_state)
	{
		return response;
	}

	// Let's first print the snoop response
	print_snoop_response(address, response);

	// If new mesi state is going to be invalid then we better invalidate the line
	if (new_mesi_state == invalid)
	{
		snoop_invalidate_cache_entry(GET_CACHE_INDEX(address), j,GET_CACHE_LINE_BYTE_OFFSET(address));
	}

	// If current state was modified and new state is going to be shared, we have to write it back
	if (current_state == modified && 
		new_mesi_state == shared)
	{
		write_back_cache_entry(GET_CACHE_INDEX(address), j,GET_CACHE_LINE_BYTE_OFFSET(address));
	}
	
	g_cache.cache[cache_index][j].mesi_state = new_mesi_state;
	return response;
}

void fix_up_lru_vals_in_line(int cache_index, unsigned short prev_lru)
{
	int j = 0;

	while (j < CACHE_ASSOSIATIVITY_SIZE)
	{
		if (g_cache.cache[cache_index][j].cache_lru_count > prev_lru)
		{
			g_cache.cache[cache_index][j].cache_lru_count--;
		}
		j++;
	}
}

snoop_response emulate_correct_snoop_response(mesi_state current_state, snoop_response curr_snoop_response, unsigned long address)
{
	snoop_response new_snoop_response = curr_snoop_response;

	if (current_state == shared)
	{
		new_snoop_response = (curr_snoop_response == snoop_hitm)?snoop_hit: curr_snoop_response;
	}
	else if (current_state == exclusive || 
			current_state == modified)
	{
		new_snoop_response = no_snoop_response;
	}
	if (new_snoop_response == snoop_hitm)
	{
		print_shared_bus_operations(address, 'W');
	}
	return new_snoop_response;
}

bool	IsCacheHit(int cache_index, unsigned long cache_tag, int *set_index)
{
	int j = 0;
	unsigned short max_curr_lru = 0;
	unsigned short hit_lru = 0;

	j = find_cache_column_number(cache_index, cache_tag);

	// in this case it was a hit
	if (j < CACHE_ASSOSIATIVITY_SIZE)
	{
		*set_index = j;
		g_cache.hitcount++;
		// Handling LRU values below
		// Obtaining the previous LRU of the hit
		hit_lru = g_cache.cache[cache_index][j].cache_lru_count;
		// Obtain the maximum lru count
		max_curr_lru = g_cache.cache[cache_index][g_cache.max_lru_index[cache_index]].cache_lru_count;
		// fix up lru values in the whole line
		fix_up_lru_vals_in_line(cache_index, hit_lru);
		// Now this entry has max lru value
		g_cache.cache[cache_index][j].cache_lru_count = max_curr_lru;
		// Store the column number of this cache entry, we will need it next cache hit or miss
		g_cache.max_lru_index[cache_index] = j;		
		return true;
	}

	// It reached here, means it was a miss and hence we should increment the miss count
	g_cache.misscount++;
	return false;
}

//bool	IsCacheHit_old(int cache_index, unsigned long cache_tag, int *set_index)
//{
//	int j = 0;
//	unsigned short max_curr_lru = 0;
//	unsigned short hit_lru = 0;
//
//
//	while (j <  CACHE_ASSOSIATIVITY_SIZE)
//	{
//		if (g_cache.cache[cache_index][j].cache_tag == cache_tag)
//		{
//			*set_index = j;
//			g_cache.hitcount++;
//			// Handling LRU values below
//			// Obtaining the previous LRU of the hit
//			hit_lru = g_cache.cache[cache_index][j].cache_lru_count;
//			// Obtain the maximum lru count
//			max_curr_lru = g_cache.cache[cache_index][g_cache.max_lru_index[cache_index]].cache_lru_count;
//			// fix up lru values in the whole line
//			fix_up_lru_vals_in_line(cache_index, hit_lru);
//			// Now this entry has max lru value
//			g_cache.cache[cache_index][j].cache_lru_count = max_curr_lru;
//			// Store the column number of this cache entry, we will need it next cache hit or miss
//			g_cache.max_lru_index[cache_index] = j;		
//			return true;
//		}
//		j++;
//	}
//	g_cache.misscount++;
//	return false;
//}

int change_mesi_state(int cache_index, int set_index, int operation, int snoop_result)
{
	mesi_state	nextstate, currentstate;

	currentstate = g_cache.cache[cache_index][set_index].mesi_state;
	nextstate = g_mesi_state_matrix[currentstate][operation][snoop_result];

	if (nextstate == unreachable_state)
	{
		printf("change_mesi_state: There is a bug in implementation, it shouldn't have reached here\n");
		//__debugbreak();
	}
	else
	{
		g_cache.cache[cache_index][set_index].mesi_state = nextstate;
	}

	return nextstate;
}

int find_cache_column_number(int cache_index, unsigned long cache_tag)
{
	int j = 0;

	while (j < CACHE_ASSOSIATIVITY_SIZE)
	{
		// Tag can be zero, thats why we are checking the validity of cache entry first
		if (g_cache.cache[cache_index][j].valid)
		{
			if (g_cache.cache[cache_index][j].cache_tag == cache_tag)
			{
				break;
			}
		}

		j++;
	}

	//if (j == CACHE_ASSOSIATIVITY_SIZE)
	//{
	//	j == -1;
	//}

	return j;
}

mesi_state snoop_invalidate_cache_entry(int cache_index, int set_index,  int byteoffset)
{
	mesi_state result = unreachable_state;
	unsigned long address = 0, cache_tag = 0;

	cache_tag = g_cache.cache[cache_index][set_index].cache_tag;

	result = evict_cache_entry(cache_index, set_index, byteoffset);
	fix_up_lru_vals_in_line(cache_index, g_cache.cache[cache_index][set_index].cache_lru_count);

	// If index which is being invalidated is the max lru then we need to do fixup for max lru
	memset(&g_cache.cache[cache_index][set_index], 0, sizeof(cache_entry));

	// Update the column number, if it was the max lru index
	if (set_index == g_cache.max_lru_index[cache_index])
	{
		set_index = cache_find_maxlru_column(cache_index);
		g_cache.max_lru_index[cache_index] = set_index;
	}

	address = (cache_tag<<(g_cache_index_bits+g_cache_data_bits)) | (cache_index<<g_cache_data_bits) | (byteoffset);
	print_shared_bus_operations(address, 'I');
	return result;
}

/*
 It will invalidate the cache line.
 And returns the mesi_state of cache line 
*/
mesi_state snoop_invalidate(unsigned long  address)
{
	int cache_index = 0, byteoffset = 0;
	unsigned int cache_tag = 0;
	int j = 0;
	mesi_state result = unreachable_state;

	cache_index = GET_CACHE_INDEX(address);
	cache_tag = GET_CACHE_TAG(address);
	byteoffset = GET_CACHE_LINE_BYTE_OFFSET(address);
	j = find_cache_column_number(cache_index, cache_tag);
	
	if (j >= CACHE_ASSOSIATIVITY_SIZE)
	{
		result = unreachable_state;
		//__debugbreak();
		printf("snoop_invalidate: the cache line being referred to by address %x is in invalid state\n", address);
	}
	// This will reset each and every state to default
	else 
	{
		result = snoop_invalidate_cache_entry(cache_index, j, byteoffset);
		//result = evict_cache_entry(cache_index, j, byteoffset);
		//fix_up_lru_vals_in_line(cache_index, g_cache.cache[cache_index][j].cache_lru_count);

		//// If index which is being invalidated is the max lru then we need to do fixup for max lru
		//memset(&g_cache.cache[cache_index][j], 0, sizeof(cache_entry));

		//// Update the column number, if it was the max lru index
		//if (j == g_cache.max_lru_index[cache_index])
		//{
		//	j = cache_find_maxlru_column(cache_index);
		//	g_cache.max_lru_index[cache_index] = j;
		//}

		//print_shared_bus_operations(address, 'I');
	}

	return result;
}

int write_cache(unsigned long address)
{
	int				cache_index = 0, set_index = 0;
	snoop_response	snoop_result = no_snoop_response;
	int				byte_offset = 0;
	unsigned long	cache_tag = 0;
	bool			cache_hit = false;

	cache_index = GET_CACHE_INDEX(address);
	snoop_result = (snoop_response) GetSnoopResult(address, 'W');
	cache_tag = GET_CACHE_TAG(address);
	byte_offset = GET_CACHE_LINE_BYTE_OFFSET(address);

	g_cache.cache_writes++;
	cache_hit = IsCacheHit(cache_index, cache_tag, &set_index);

	if (!cache_hit)
	{
		
		//read_from_main_memory(cache_index, &set_index, byte_offset);
		write_to_cache(cache_index, &set_index, byte_offset);
		// Read with intent to modify is going to be performed on shared bus
		print_shared_bus_operations(address, 'M');
		// Store the cache_tag
		g_cache.cache[cache_index][set_index].cache_tag = cache_tag;
	}
	else
	{
		// If it was a cache hit, then we need to modify snoop result based on our current state (this is just for perfect emulation)
		snoop_result = emulate_correct_snoop_response(g_cache.cache[cache_index][set_index].mesi_state, snoop_result, address);
		if (g_cache.cache[cache_index][set_index].mesi_state == shared)
		{
			print_shared_bus_operations(address, 'I');
		}
	}

	change_mesi_state(cache_index, set_index, write_operation, snoop_result);

	return (cache_hit? 1: 0);
}

/*
* Below function will take address as input and read it from cache or bring it from main memory into cache (if cache miss)
* It returns following:-- 
* 0 - cache miss
* 1 - cache hit
* -1 - error
*/
int read_cache(unsigned long address)
{
	int				cache_index = 0, set_index = 0;
	snoop_response	snoop_result = no_snoop_response;
	int				byte_offset = 0;
	unsigned long	cache_tag = 0;
	bool			cache_hit = false;

	cache_index = GET_CACHE_INDEX(address);
	snoop_result = (snoop_response) GetSnoopResult(address, 'R');
	cache_tag = GET_CACHE_TAG(address);
	byte_offset = GET_CACHE_LINE_BYTE_OFFSET(address);

	g_cache.cache_reads++;
	cache_hit = IsCacheHit(cache_index, cache_tag, &set_index);

	if (!cache_hit)
	{
		read_from_main_memory(cache_index, &set_index, byte_offset);
		// Read is going to be performed on shared bus
		print_shared_bus_operations(address, 'R');
		// store the cache_tag
		g_cache.cache[cache_index][set_index].cache_tag = cache_tag;
	}
	else
	{
		snoop_result = emulate_correct_snoop_response(g_cache.cache[cache_index][set_index].mesi_state, snoop_result, address);
	}

	change_mesi_state(cache_index, set_index, read_operation, snoop_result);

	return (cache_hit? 1: 0);
}


void reset_clear_cache()
{
	ZeroMemory(&g_cache, sizeof(g_cache));
	g_cache.size = sizeof(g_cache);
}

int get_power_of_two(unsigned long datasize)
{
	int power_of_two = 0;

	while (datasize/2)
	{
		power_of_two++;
		datasize = datasize/2;
	}

	return power_of_two;
}

void initialize_globals()
{
	g_cache_data_bits = get_power_of_two(CACHE_LINE_DATA_SIZE);
	g_cache_index_bits = get_power_of_two(CACHE_LINES);
	g_cache_tag_bits = MAX_ADDRESS_BITS - g_cache_data_bits - g_cache_index_bits;
	
	g_cache_data_offset_mask = CACHE_LINE_DATA_SIZE - 1;
	g_cache_line_index_mask = ((CACHE_LINES - 1)<<g_cache_data_bits);
	g_cache_tag_mask = (-1) ^ (g_cache_data_offset_mask | g_cache_line_index_mask);

	g_curr_line_feed_no = 0;
}

void print_shared_bus_operations(unsigned long address, char operation)
{
	printf("%c 0x%x, %x, %x, %x, input_line_number = %lu\n", operation, address, 
		GET_CACHE_INDEX(address), GET_CACHE_TAG(address), GET_CACHE_LINE_BYTE_OFFSET(address), g_curr_line_feed_no);
	//printf("%c 0x%x\n", operation, address);

}

int _tmain(int argc, _TCHAR* argv[])
{
	char			inputstring[MAX_INPUT_LENGTH];
	unsigned long	address = 0;
	char			command, *endptr;
	int				result = 0;
	mesi_state		prev_mesi_state = unreachable_state;

	reset_clear_cache();
	initialize_globals();
	
	while (gets_s(inputstring, sizeof(inputstring)/sizeof(char)))
	{
		g_curr_line_feed_no++;

		command = inputstring[0];
		if (command == '#') // Its'a comment, lets skip the whole line and goto next line
			continue;
		else if (command == '\0') // It probably was a new line or a space
			continue;

		command = command - '0';
		//address = atoi(&inputstring[1]);
		address = strtoul(&inputstring[1], &endptr, 16);
		switch(command)
		{
		case INPUT_READ_REQUEST_DATA_COMMAND:
			result = read_cache(address);
			//if (result == 0)
			//{
			//	print_shared_bus_operations(address, 'R');
			//}
			break;
		case INPUT_WRITE_REQUEST_DATA_COMMAND:
			result = write_cache(address);
			// Result 0 means, it was a cache miss and Read with intent to modify is going to be issued on shared bus
			//if (result == 0)
			//{
			//	print_shared_bus_operations(address, 'M');
			//}

			break;
		case INPUT_READ_REQUEST_INSTR_COMMAND:
			result = read_cache(address);
			//if (result == 0)
			//{
			//	print_shared_bus_operations(address, 'R');
			//}
			break;
		case INPUT_SNOOP_INVALIDATE_COMMNAD:
			prev_mesi_state = snoop_invalidate(address);

			// If the mesi state of invalidated cache line was modified, then that means a Write was performed on shared bus, lets print it
			//if (prev_mesi_state == modified)
			//{
			//	print_shared_bus_operations(address, 'W');
			//}
			break;
		case INPUT_SNOOP_READ_COMMAND:
			//result = snoop_read(address);
			result = PutSnoopResult(address, 'R');
			break;
		case INPUT_SNOOP_WRITE_COMMAND:
			//result = snoop_write(address);
			result = PutSnoopResult(address, 'W');
			break;
		case INPUT_SNOOP_READ_INTENT_MODIFY_COMMAND:
			//result = snoop_read_intent_to_modify(address);
			result = PutSnoopResult(address, 'M');
			break;
		case INPUT_CLEAR_CACHE_RESET_COMMAND:
			reset_clear_cache();
			break;
		case INPUT_PRINT_CACHE_STATE:
			result = print_valid_cachelines_state();
			//printf("Press any key to continue to next trace...\n");
			//_getch();
			break;
		default:
			printf("error: cache simulation: invalid input command %d", (int) command);
			break;
		}
	}

	printf("Press any key to exit ...\n");
	_getch();
	return 0;
}