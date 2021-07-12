#ifndef FIXED_POINT_H_
#define FIXED_POINT_H_
#include <inttypes.h>

#define P 17
#define Q 14
#define F (1<<Q)

struct real
{
    int val;
};

static inline struct real I_TO_F(int N) // Integer to fixed point
{
    return (struct real) {N * F};
}

static inline int F_TO_I_DOWN(struct real X) // Fixed point to integer
{
    return X.val / F;
};

static inline struct real ADD_F_F(struct real X, struct real Y) // Add fixed to fixed
{
    return (struct real) {X.val + Y.val};
}

static inline struct real SUB_F_F(struct real X, struct real Y) // Sub fixed to fixed
{
    return (struct real) {X.val - Y.val};
}

static inline struct real ADD_F_I(struct real X, int N) // Add fixed integer
{
    return (struct real) {X.val + N*F};
}

static inline struct real SUB_F_I(struct real X, int N) // Sub fixed integer
{
    return (struct real) {X.val - N*F};
}

static inline struct real MUL_F_I(struct real X, int N) // Multiply fixed integer
{
    return (struct real) {X.val * N};
}

static inline struct real DIV_F_I(struct real X, int N) // Divide fixed integer
{
    return (struct real) {X.val / N};
}

static inline struct real MUL_F_F(struct real X, struct real Y)
{
    return (struct real) {((int64_t)X.val) * Y.val / F};
}

static inline struct real DIV_F_F(struct real X, struct real Y)
{
    return (struct real) {((int64_t)X.val) * F / Y.val};
}

static inline int F_TO_I_ROUND(struct real X)
{
    if(X.val >= 0)
        return (X.val + F/2) / F;
    else
        return (X.val - F/2) / F;
}

#endif