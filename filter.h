#ifndef _FILTER_H_
#define _FILTER_H_

typedef long real;
typedef long long real_64;

/* coeff Q27 */
#define DOUBLE_TO_REAL(x) (int)((x)*134217728)
#define REAL_MULT(x, y) (long long)((long long)(x)*(long long)(y))
#define RESULT_SHIFT_Q27(x) (int)((x)>>27)
#define IIR_REAL_MULT(x, y) REAL_MULT(x, y)
#define RESULT_SHIFT_IIR(x) RESULT_SHIFT_Q27(x)

#define N		3

typedef struct
{
    real xd[N];
    real_64 yd[N];
    int Order;

} iir_filter_t;

void iir_filter(iir_filter_t* filter, real* x, real* y, real* b/*iir_hf_b[1][][]*/, real* a/*iir_hf_a[1][][]*/, int length);

#endif // _FILTER_H_