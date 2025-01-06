/*
 * Lock-Free SPSC Event Queue Implementation
 *
 * Uses acquire-release semantics for ISR→main safety.
 * On Cortex-M3, __DMB() provides the necessary memory barrier.
 */

#include "event_queue.h"

/*
 * Memory barrier macros
 *
 * For ARM Cortex-M3:
 *   __DMB() - Data Memory Barrier, ensures memory operations complete
 *
 * For host testing:
 *   Use compiler barriers (adequate for single-threaded tests)
 */
#ifdef HOST_TEST
    /* Host testing - compiler barrier only */
    #define MEMORY_BARRIER()  __asm__ volatile("" ::: "memory")
#else
    /* ARM Cortex-M - use CMSIS intrinsic */
    #include "platform/stm32f1xx.h"
    #define MEMORY_BARRIER()  __DMB()
#endif

void event_queue_init(event_queue_t *q)
{
    q->head = 0;
    q->tail = 0;
    MEMORY_BARRIER();
}

bool event_queue_push(event_queue_t *q, const event_t *evt)
{
    uint16_t head = q->head;
    uint16_t tail = q->tail;

    /* Check if queue is full */
    uint16_t next_head = (head + 1u) & EVENT_QUEUE_MASK;
    if (next_head == tail) {
        return false;  /* Queue full */
    }

    /* Write event to buffer */
    q->events[head] = *evt;

    /* Memory barrier ensures event is written before head is updated */
    MEMORY_BARRIER();

    /* Publish new head position */
    q->head = next_head;

    return true;
}

bool event_queue_pop(event_queue_t *q, event_t *evt)
{
    uint16_t head = q->head;
    uint16_t tail = q->tail;

    /* Check if queue is empty */
    if (head == tail) {
        return false;  /* Queue empty */
    }

    /* Memory barrier ensures we see event after head was updated */
    MEMORY_BARRIER();

    /* Read event from buffer */
    *evt = q->events[tail];

    /* Memory barrier ensures event is read before tail is updated */
    MEMORY_BARRIER();

    /* Advance tail */
    q->tail = (tail + 1u) & EVENT_QUEUE_MASK;

    return true;
}

bool event_queue_empty(const event_queue_t *q)
{
    return q->head == q->tail;
}

bool event_queue_full(const event_queue_t *q)
{
    uint16_t next_head = (q->head + 1u) & EVENT_QUEUE_MASK;
    return next_head == q->tail;
}

uint16_t event_queue_count(const event_queue_t *q)
{
    uint16_t head = q->head;
    uint16_t tail = q->tail;

    /* Handle wrap-around with mask */
    return (head - tail) & EVENT_QUEUE_MASK;
}

uint16_t event_queue_drain(event_queue_t *q, event_handler_fn handler, void *ctx)
{
    uint16_t count = 0;
    event_t evt;

    while (event_queue_pop(q, &evt)) {
        if (handler) {
            handler(&evt, ctx);
        }
        count++;
    }

    return count;
}
