#include "core.h"

static inline int16_t sample_at(const ringbuf_i16_t *rb, uint32_t sample_idx)
{
    return rb->data[sample_idx & rb->mask];
}

static inline int mono_queue_empty(const mono_queue_t *q)
{
    return q->count == 0;
}

static inline uint16_t mono_queue_front(const mono_queue_t *q)
{
    return q->buf[q->head];
}

static inline uint16_t mono_queue_back(const mono_queue_t *q)
{
    return q->buf[(uint16_t)((q->tail - 1u) & (q->capacity - 1u))];
}

static inline void mono_queue_pop_front(mono_queue_t *q)
{
    q->head = (uint16_t)((q->head + 1u) & (q->capacity - 1u));
    if (q->count)
        q->count--;
}

static inline void mono_queue_pop_back(mono_queue_t *q)
{
    q->tail = (uint16_t)((q->tail - 1u) & (q->capacity - 1u));
    if (q->count)
        q->count--;
}

static inline void mono_queue_push_back(mono_queue_t *q, uint16_t v)
{
    if (q->count == q->capacity)
        mono_queue_pop_front(q);
    q->buf[q->tail++ & (q->capacity - 1u)] = v;
    q->tail &= (q->capacity - 1u);
    if (q->count < q->capacity)
        q->count++;
}

void ringbuf_i16_init(ringbuf_i16_t *rb, int16_t *storage, uint16_t capacity,
                      uint16_t *min_idx_buf, uint16_t *max_idx_buf)
{
    if (!rb || !storage || !min_idx_buf || !max_idx_buf)
        return;
    /* enforce power-of-two capacity */
    if (capacity == 0 || (capacity & (capacity - 1u)))
        return;

    rb->data = storage;
    rb->capacity = capacity;
    rb->mask = (uint16_t)(capacity - 1u);
    rb->count = 0;
    rb->head = 0;

    rb->min_q.buf = min_idx_buf;
    rb->min_q.capacity = capacity;
    rb->min_q.head = rb->min_q.tail = rb->min_q.count = 0;

    rb->max_q.buf = max_idx_buf;
    rb->max_q.capacity = capacity;
    rb->max_q.head = rb->max_q.tail = rb->max_q.count = 0;
}

void ringbuf_i16_reset(ringbuf_i16_t *rb)
{
    if (!rb)
        return;
    rb->count = 0;
    rb->head = 0;
    rb->min_q.head = rb->min_q.tail = rb->min_q.count = 0;
    rb->max_q.head = rb->max_q.tail = rb->max_q.count = 0;
}

void ringbuf_i16_push(ringbuf_i16_t *rb, int16_t sample)
{
    if (!rb || rb->capacity == 0)
        return;

    uint32_t idx = rb->head;
    rb->data[idx & rb->mask] = sample;

    /* Maintain monotonic queues: min_q increasing, max_q decreasing. */
    while (!mono_queue_empty(&rb->min_q) && sample_at(rb, mono_queue_back(&rb->min_q)) > sample)
        mono_queue_pop_back(&rb->min_q);
    mono_queue_push_back(&rb->min_q, (uint16_t)idx);

    while (!mono_queue_empty(&rb->max_q) && sample_at(rb, mono_queue_back(&rb->max_q)) < sample)
        mono_queue_pop_back(&rb->max_q);
    mono_queue_push_back(&rb->max_q, (uint16_t)idx);

    rb->head++;
    if (rb->count < rb->capacity)
    {
        rb->count++;
    }
    else
    {
        /* Evict the oldest sample and drop from queues if present. */
        uint32_t evict_idx = idx - rb->capacity;
        if (!mono_queue_empty(&rb->min_q) && mono_queue_front(&rb->min_q) == (uint16_t)evict_idx)
            mono_queue_pop_front(&rb->min_q);
        if (!mono_queue_empty(&rb->max_q) && mono_queue_front(&rb->max_q) == (uint16_t)evict_idx)
            mono_queue_pop_front(&rb->max_q);
    }
}

void ringbuf_i16_summary(const ringbuf_i16_t *rb, ringbuf_i16_summary_t *out)
{
    if (!rb || !out)
        return;
    out->capacity = rb->capacity;
    out->count = rb->count;
    if (rb->count == 0)
    {
        out->min = 0;
        out->max = 0;
        out->latest = 0;
        return;
    }
    out->latest = sample_at(rb, rb->head - 1u);
    out->min = sample_at(rb, mono_queue_front(&rb->min_q));
    out->max = sample_at(rb, mono_queue_front(&rb->max_q));
}

/* Minimal libc stubs (freestanding build) */
void __aeabi_memclr(void *dest, size_t n)
{
    uint8_t *d = (uint8_t *)dest;
    for (size_t i = 0; i < n; ++i)
        d[i] = 0;
}

void __aeabi_memclr4(void *dest, size_t n)
{
    __aeabi_memclr(dest, n);
}

void __aeabi_memcpy4(void *dest, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; ++i)
        d[i] = s[i];
}

void __aeabi_memcpy(void *dest, const void *src, size_t n)
{
    __aeabi_memcpy4(dest, src, n);
}

void __aeabi_memcpy8(void *dest, const void *src, size_t n)
{
    __aeabi_memcpy4(dest, src, n);
}
