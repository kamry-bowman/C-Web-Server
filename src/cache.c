#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hashtable.h"
#include "cache.h"

/**
 * Allocate a cache entry
 */
struct cache_entry *alloc_entry(char *path, char *content_type, void *content, int content_length)
{
  struct cache_entry * entry = malloc(sizeof(struct cache_entry));
  entry->path = strdup(path);
  entry->content_type = strdup(content_type);
  entry->content_length = content_length;
  entry->content = malloc(content_length);
  memcpy(entry->content, content, content_length);
  return entry;
}

/**
 * Deallocate a cache entry
 */
void free_entry(struct cache_entry *entry)
{
    free(entry->content);
    free(entry->content_type);
    free(entry->path);
    free(entry);
}

/**
 * Insert a cache entry at the head of the linked list
 */
void dllist_insert_head(struct cache *cache, struct cache_entry *ce)
{
    // Insert at the head of the list
    if (cache->head == NULL) {
        cache->head = cache->tail = ce;
        ce->prev = ce->next = NULL;
    } else {
        cache->head->prev = ce;
        ce->next = cache->head;
        ce->prev = NULL;
        cache->head = ce;
    }
    cache->cur_size++;
}

/**
 * Move a cache entry to the head of the list
 */
void dllist_move_to_head(struct cache *cache, struct cache_entry *ce)
{
    if (ce != cache->head) {
        if (ce == cache->tail) {
            // We're the tail
            cache->tail = ce->prev;
            cache->tail->next = NULL;

        } else {
            // We're neither the head nor the tail
            ce->prev->next = ce->next;
            ce->next->prev = ce->prev;
        }

        ce->next = cache->head;
        cache->head->prev = ce;
        ce->prev = NULL;
        cache->head = ce;
    }
}


/**
 * Removes the tail from the list and returns it
 * 
 * NOTE: does not deallocate the tail
 */
struct cache_entry *dllist_remove_tail(struct cache *cache)
{
    struct cache_entry *oldtail = cache->tail;

    cache->tail = oldtail->prev;
    cache->tail->next = NULL;

    cache->cur_size--;

    return oldtail;
}

/**
 * Create a new cache
 * 
 * max_size: maximum number of entries in the cache
 * hashsize: hashtable size (0 for default)
 */
struct cache *cache_create(int max_size, int hashsize)
{
  struct cache *cache = malloc(sizeof(struct cache));
  cache->index = hashtable_create(max_size, NULL);
  cache->max_size = max_size;
  cache->cur_size = 0;
  cache->head = NULL;
  cache->tail = NULL;
  return cache;
}

void cache_free(struct cache *cache)
{
    struct cache_entry *cur_entry = cache->head;

    hashtable_destroy(cache->index);

    while (cur_entry != NULL) {
        struct cache_entry *next_entry = cur_entry->next;

        free_entry(cur_entry);

        cur_entry = next_entry;
    }

    free(cache);
}

/**
 * Store an entry in the cache
 *
 * This will also remove the least-recently-used items as necessary.
 * 
 * NOTE: doesn't check for duplicate cache entries
 */
// struct cache_entry {
//     char *path;   // Endpoint path--key to the cache
//     char *content_type;
//     int content_length;
//     void *content;

//     struct cache_entry *prev, *next; // Doubly-linked list
// };

// // A cache
// struct cache {
//     struct hashtable *index;
//     struct cache_entry *head, *tail; // Doubly-linked list
//     int max_size; // Maxiumum number of entries
//     int cur_size; // Current number of entries
// };
void cache_put(struct cache *cache, char *path, char *content_type, void *content, int content_length)
{
  struct cache_entry * entry = alloc_entry(path, content_type, content, content_length);

  // store in doubly linked list
  dllist_insert_head(cache, entry);

  // add entry to hash_table
  hashtable_put(cache->index, entry->path, entry);


  // if at capacity, delete oldest cache item
  if (cache->cur_size > cache->max_size) {
    struct cache_entry *prior_tail = dllist_remove_tail(cache);
    hashtable_delete(cache->index, prior_tail->path);
    free_entry(prior_tail);
  } else {
    // else, update cache size
  }
  printf("current_size: %d\n", cache->cur_size);

}

/**
 * Retrieve an entry from the cache
 */
struct cache_entry *cache_get(struct cache *cache, char *path)
{
  struct cache_entry * entry = hashtable_get(cache->index, path);
  if (!entry) {
    return NULL;
  }

  // add entry to front of list
  dllist_move_to_head(cache, entry);
  return entry;
}