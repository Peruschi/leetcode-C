#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <stdbool.h>

#include <stdio.h>

#define CHECK_MAX_THRESHOLD 20000

typedef char ALIGN[24];
typedef union header{
	struct {
		union header *next;
	       	size_t size;
		bool is_free;
	} info;
	ALIGN placeholder;
} Header;

static Header *head, *tail;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static void* apply_memory(size_t size);
static Header* get_free_block(size_t size);
static Header* split_block(Header *curr, size_t size);
static Header* merge_block(Header *curr);

void* malloc(size_t size);
void* calloc(size_t num, size_t type_size);
void* realloc(void *block, size_t size);
void free(void* block);

void debug(void* block){
	Header* curr = block;
	printf("%p %p\n", head, tail);
	while (curr){
		printf("%p %zu %u %p\n", curr, curr->info.size, curr->info.is_free, curr->info.next);
		curr = curr->info.next;
	}
}

int main(void){
	printf("%ld\n",sizeof(Header));
	
	long *ptr1 = malloc(sizeof(long));
	long *ptr2 = malloc(sizeof(long));
	long *ptr3 = malloc(sizeof(long));
	debug(head);
	
	free(ptr1);
	free(ptr2);
	free(ptr3);
	char *ptr = malloc(sizeof(char));
	int *ptr5 = malloc(sizeof(int));
	int *ptr6 = malloc(sizeof(int));
	long*ptr7 = malloc(sizeof(long));
	debug(head);
	
	return 0;
}

static void* apply_memory(size_t size){
	void *block = sbrk(size);
	if (block == (void*)-1){
		return NULL;
	}
	else {
		return block;
	}
}

static Header* get_free_block(size_t size){
	Header *curr = head;
	while (curr){
		if (curr->info.is_free && size == curr->info.size){
			return curr; 
		}
		if (curr->info.is_free && curr->info.next->info.is_free){
			curr = merge_block(curr);
		}
		if (curr->info.is_free && size <= curr->info.size){
			return split_block(curr, size);
		}
		curr = curr->info.next;
	}
	
	return NULL;
}

static Header* split_block(Header* curr, size_t size){
	size_t total_size = sizeof(Header) + size;

	Header *new_header = (void*)curr + total_size;
	void *program_break = sbrk(0);
	size_t diff = (size_t)new_header - (size_t)program_break;
	if (0 < diff){
		sbrk(0 - diff);
		curr->info.size = size;
		curr->info.is_free = true;
		curr->info.next = NULL;
		tail = curr;
		return curr;
	}

	new_header->info.next = curr->info.next;
	new_header->info.size = size;
	new_header->info.is_free = true;

	curr->info.next = new_header;
	curr->info.size -= total_size;

	return new_header;
}

static Header* merge_block(Header *header){
	if (!header->info.next){
		return header;
	}
	Header *curr = header;
	Header *next = header->info.next;
	curr->info.size += sizeof(Header) + next->info.size;
	curr->info.next = next->info.next;
	
	return curr;
}

void* malloc(size_t size){
	if (!size){
		return NULL;
	}

	pthread_mutex_lock(&mutex);

	Header *header = get_free_block(size);
	if (header){
		header->info.is_free = false;
		pthread_mutex_unlock(&mutex);
		return (void*)(header + 1);
	}

	size_t needed_total_size = sizeof(Header) + size;
	header = apply_memory(needed_total_size);
	if (!header){
		pthread_mutex_unlock(&mutex);
		return NULL;
	}
	header->info.is_free = false;
	header->info.size = size;
	header->info.next = NULL;
	
	if (!head){
		head = header;
	}
	if (tail){
		tail->info.next = header;
	}
	tail = header;

	pthread_mutex_unlock(&mutex);
	return (void*)(header + 1);
}

void* calloc(size_t num, size_t type_size){
	if (!num || !type_size){
		return NULL;
	}

	size_t total_size = num * type_size;
	if (num != total_size / type_size){
		return NULL;
	}

	void *block = malloc(total_size);
	if (!block){
		return NULL;
	}
	memset(block, 0, total_size);
	return block;
}

void* realloc(void *block, size_t size){
	if (!block){
		return NULL;
	}
	if (!size){
		free(block);
		return NULL;
	}

	Header *header = (Header*)block - 1;
	if (size <= header->info.size){
		return block;
	}

	void *new_block = malloc(size);
	if (new_block){
		memcpy(new_block, block, header->info.size);
		free(block);
	}
	return new_block;
}

void free(void* block){
	if (!block){
		return ;
	}

	pthread_mutex_lock(&mutex);
	
	void *program_break = sbrk(0);
	Header *header = (Header*)block - 1;
	if (program_break == block + header->info.size){
		if (head == tail){
			head = tail = NULL;
		}
		else {
			Header * curr = head;
			while (curr){
				if (curr->info.next == tail){
					curr->info.next = NULL;
					tail = curr;
				}
				curr = curr->info.next;
			}
		}
		sbrk(-header->info.size - sizeof(Header));
		pthread_mutex_unlock(&mutex);
		return ;
	}
	header->info.is_free = true;

	size_t heap_top = (size_t)sbrk(0);
	size_t heap_bottom = (size_t)head->info.next;
	if (CHECK_MAX_THRESHOLD > heap_top - heap_bottom){
		pthread_mutex_unlock(&mutex);
		return ;
	}
	
	Header dummy;
	dummy.info.next = head;

	Header *prev = &dummy;
	Header *curr = head;
	while (curr){
		if (curr->info.is_free && curr->info.next->info.is_free){
			curr = merge_block(curr);
		}
		curr = curr->info.next;
		prev = prev->info.next;
	}

	if (curr->info.is_free && !curr->info.next){
		tail = prev;
		prev->info.next = NULL;
		sbrk(0 - curr->info.size - sizeof(Header));
		pthread_mutex_unlock(&mutex);
		return ;
	}

	pthread_mutex_unlock(&mutex);
}
