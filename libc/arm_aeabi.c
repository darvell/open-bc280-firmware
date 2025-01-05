/**
 * @file arm_aeabi.c
 * @brief ARM EABI runtime support for bare-metal builds
 *
 * Provides 64-bit division/modulo functions required by ARM EABI.
 * These are normally provided by libgcc or compiler-rt.
 */

#include <stdint.h>

/**
 * 64-bit unsigned division: binary long division algorithm
 */
void __udivmoddi4(uint64_t num, uint64_t den, uint64_t *quot_out, uint64_t *rem_out)
{
    if (den == 0) {
        /* Division by zero - return max quotient, zero remainder */
        *quot_out = UINT64_MAX;
        *rem_out = 0;
        return;
    }

    if (num < den) {
        *quot_out = 0;
        *rem_out = num;
        return;
    }

    if (den == 1) {
        *quot_out = num;
        *rem_out = 0;
        return;
    }

    /* Binary long division */
    uint64_t quot = 0;
    uint64_t rem = 0;

    for (int i = 63; i >= 0; i--) {
        rem = (rem << 1) | ((num >> i) & 1);
        if (rem >= den) {
            rem -= den;
            quot |= (1ULL << i);
        }
    }

    *quot_out = quot;
    *rem_out = rem;
}

/* 64-bit unsigned division - returns quotient only */
uint64_t __aeabi_uldiv(uint64_t num, uint64_t den)
{
    uint64_t quot, rem;
    __udivmoddi4(num, den, &quot, &rem);
    return quot;
}

/**
 * __aeabi_uldivmod - 64-bit unsigned division with remainder
 *
 * ARM EABI calling convention:
 *   Input:  r0:r1 = numerator (lo:hi), r2:r3 = denominator (lo:hi)
 *   Output: r0:r1 = quotient (lo:hi), r2:r3 = remainder (lo:hi)
 *
 * This is implemented in assembly to handle the register convention properly.
 */
__attribute__((naked))
void __aeabi_uldivmod(void)
{
    __asm__ volatile(
        /* Push callee-saved registers and make room for results */
        "push {r4-r7, lr}\n"
        "sub sp, sp, #16\n"         /* Space for quot (8) + rem (8) */

        /* num is in r0:r1, den is in r2:r3 */
        /* Call __udivmoddi4(num, den, &quot, &rem) */

        /* Compute addresses for output pointers */
        "mov r4, sp\n"              /* r4 = &quot */
        "add r5, sp, #8\n"          /* r5 = &rem */

        /* Push output pointer arguments (on stack per ARM AAPCS) */
        "push {r4, r5}\n"

        /* Call the C division function */
        "bl __udivmoddi4\n"

        /* Pop the output pointer args */
        "add sp, sp, #8\n"

        /* Load results: quot to r0:r1, rem to r2:r3 */
        "ldrd r0, r1, [sp, #0]\n"   /* quot */
        "ldrd r2, r3, [sp, #8]\n"   /* rem */

        /* Cleanup and return */
        "add sp, sp, #16\n"
        "pop {r4-r7, pc}\n"
    );
}

/* 64-bit signed division - returns quotient only */
int64_t __aeabi_ldiv(int64_t num, int64_t den)
{
    int neg = 0;
    uint64_t unum, uden, quot, rem;

    if (num < 0) {
        neg = !neg;
        unum = (uint64_t)(-num);
    } else {
        unum = (uint64_t)num;
    }

    if (den < 0) {
        neg = !neg;
        uden = (uint64_t)(-den);
    } else {
        uden = (uint64_t)den;
    }

    __udivmoddi4(unum, uden, &quot, &rem);

    return neg ? -(int64_t)quot : (int64_t)quot;
}

/**
 * Helper for signed divmod - stores results to pointers
 * Not static since it's called from assembly
 */
void __ldivmod_impl(int64_t num, int64_t den, int64_t *quot_out, int64_t *rem_out)
{
    int num_neg = 0, den_neg = 0;
    uint64_t unum, uden, uquot, urem;

    if (num < 0) {
        num_neg = 1;
        unum = (uint64_t)(-num);
    } else {
        unum = (uint64_t)num;
    }

    if (den < 0) {
        den_neg = 1;
        uden = (uint64_t)(-den);
    } else {
        uden = (uint64_t)den;
    }

    __udivmoddi4(unum, uden, &uquot, &urem);

    /* Quotient is negative if signs differ */
    if (num_neg != den_neg) {
        *quot_out = -(int64_t)uquot;
    } else {
        *quot_out = (int64_t)uquot;
    }

    /* Remainder has same sign as numerator */
    if (num_neg) {
        *rem_out = -(int64_t)urem;
    } else {
        *rem_out = (int64_t)urem;
    }
}

/**
 * __aeabi_ldivmod - 64-bit signed division with remainder
 */
__attribute__((naked))
void __aeabi_ldivmod(void)
{
    __asm__ volatile(
        /* Push callee-saved registers and make room for results */
        "push {r4-r7, lr}\n"
        "sub sp, sp, #16\n"         /* Space for quot (8) + rem (8) */

        /* num is in r0:r1, den is in r2:r3 */
        /* Call __ldivmod_impl(num, den, &quot, &rem) */

        /* Compute addresses for output pointers */
        "mov r4, sp\n"              /* r4 = &quot */
        "add r5, sp, #8\n"          /* r5 = &rem */

        /* Push output pointer arguments (on stack per ARM AAPCS) */
        "push {r4, r5}\n"

        /* Call the C division function */
        "bl __ldivmod_impl\n"

        /* Pop the output pointer args */
        "add sp, sp, #8\n"

        /* Load results: quot to r0:r1, rem to r2:r3 */
        "ldrd r0, r1, [sp, #0]\n"   /* quot */
        "ldrd r2, r3, [sp, #8]\n"   /* rem */

        /* Cleanup and return */
        "add sp, sp, #16\n"
        "pop {r4-r7, pc}\n"
    );
}

uint64_t __aeabi_llsr(uint64_t value, int shift)
{
    if (shift <= 0) {
        return value;
    }
    if (shift >= 64) {
        return 0;
    }
    return value >> (uint32_t)shift;
}

uint64_t __aeabi_llsl(uint64_t value, int shift)
{
    if (shift <= 0) {
        return value;
    }
    if (shift >= 64) {
        return 0;
    }
    return value << (uint32_t)shift;
}
