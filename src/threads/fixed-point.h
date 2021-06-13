#ifndef FIXED_POINT_H_
#define FIXED_POINT_H_
#include <inttypes.h>

#define P 17
#define Q 14
#define F (1<<Q)

#define I_TO_F(N) N*F               // Integer to fixed point
#define F_TO_I_DOWN(X) X/F          // Fixed point to integer
#define ADD_F_F(X,Y) X+Y            // Add fixed to fixed
#define SUB_F_F(X,Y) X-Y            // Sub fixed fixed
#define ADD_F_I(X,N) X+N*F          // Add fixed integer
#define SUB_F_I(X,N) X-N*F          // Sub fixed integer
#define MUL_F_I(X,N) X*N            // Multiply fixed integer
#define DIV_F_I(X,N) X/N            // Divide fixed integer

static inline int32_t MUL_F_F(int32_t x, int32_t y)
{
    return ((int64_t)x) * y / F;
}

static inline int32_t DIV_F_F(int32_t x, int32_t y)
{
    return ((int64_t)x) * F / y;
}


#endif