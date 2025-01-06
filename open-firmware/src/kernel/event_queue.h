/*
 * Lock-Free Single-Producer Single-Consumer (SPSC) Event Queue
 *
 * Safe for ISR→main communication without disabling interrupts.
 * Uses memory barriers for Cortex-M3 cache coherency.
 *
 * Properties:
 *   - Fixed capacity (power of 2 for fast modulo)
 *   - Producer (ISR) writes head, consumer (main) writes tail
 *   - Never blocks - push fails if full, pop fails if empty
 *   - ~270 bytes per queue instance
 */

#ifndef KERNEL_EVENT_QUEUE_H
#define KERNEL_EVENT_QUEUE_H

#include "event.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * Queue capacity - must be power of 2 for efficient modulo
 */
#define EVENT_QUEUE_CAPACITY 32u
#define EVENT_QUEUE_MASK     (EVENT_QUEUE_CAPACITY - 1u)

/* Static assert for power of 2 */
_Static_assert((EVENT_QUEUE_CAPACITY & EVENT_QUEUE_MASK) == 0,
               "EVENT_QUEUE_CAPACITY must be power of 2");

/*
 * Event queue structure
 *
 * Memory layout optimized for minimal false sharing:
 *   - head is written by producer (ISR), read by consumer
 *   - tail is written by consumer (main), read by producer
 */
typedef struct {
    volatile uint16_t head;   /* Next write position (producer) */
    volatile uint16_t tail;   /* Next read position (consumer) */
    event_t events[EVENT_QUEUE_CAPACITY];
} event_queue_t;

/*
 * Initialize a queue to empty state
 */
void event_queue_init(event_queue_t *q);

/*
 * Push an event onto the queue (producer side - ISR safe)
 *
 * Returns: true if event was pushed, false if queue is full
 *
 * Note: Only one producer should call this per queue.
 */
bool event_queue_push(event_queue_t *q, const event_t *evt);

/*
 * Pop an event from the queue (consumer side - main loop)
 *
 * Returns: true if event was popped, false if queue is empty
 *
 * Note: Only one consumer should call this per queue.
 */
bool event_queue_pop(event_queue_t *q, event_t *evt);

/*
 * Check if queue is empty (consumer side)
 */
bool event_queue_empty(const event_queue_t *q);

/*
 * Check if queue is full (producer side)
 */
bool event_queue_full(const event_queue_t *q);

/*
 * Get number of events in queue
 *
 * Note: This is a snapshot and may change immediately after return.
 */
uint16_t event_queue_count(const event_queue_t *q);

/*
 * Drain all events from queue, calling handler for each
 *
 * Returns: Number of events processed
 */
typedef void (*event_handler_fn)(const event_t *evt, void *ctx);
uint16_t event_queue_drain(event_queue_t *q, event_handler_fn handler, void *ctx);

#endif /* KERNEL_EVENT_QUEUE_H */
