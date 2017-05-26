
#include <utils/Log.h>
#include "filter.h"

/*static*/ real_64 IIR_REAL3264_MULT(real x, real_64 y)
{
    real_64  r2, r1;
    real yl = (real)(y&0x7fffffff);
    real yh = (real)(y >> 31);
    r1 = REAL_MULT(yl, x);
    r2 = REAL_MULT(x, yh);
    r1 >>= 27;
    r2 <<= 4;
    return (r1 + r2);
}

/*static */void iir_filter(iir_filter_t* filter, real* x, real* y, real* b/*iir_hf_b[1][][]*/, real* a/*iir_hf_a[1][][]*/, int length)
{
	real* xd = filter->xd;
	real_64* yd = filter->yd;
	real_64 result_tmp = 0;
	real result;
	int order = filter->Order;
	int i, j;


	for(i = 0; i < length; i++)
	{
		result_tmp = IIR_REAL_MULT(b[0] , x[i]);
#if 0
		for(j = 1; j <= order; j++)
			result_tmp += IIR_REAL_MULT(b[j] , xd[j-1]) - IIR_REAL3264_MULT(a[j] , yd[j - 1]);
#else
		result_tmp += IIR_REAL_MULT(b[1], xd[0]);
		result_tmp += IIR_REAL_MULT(b[2], xd[1]);
		result_tmp += IIR_REAL_MULT(b[3], xd[2]);
		result_tmp -= IIR_REAL3264_MULT(a[1] , yd[0]);
		result_tmp -= IIR_REAL3264_MULT(a[2] , yd[1]);
		result_tmp -= IIR_REAL3264_MULT(a[3] , yd[2]);
#endif

		result = RESULT_SHIFT_IIR(result_tmp);

#if 0
		for(j = order - 1; j > 0; j--)
		{
			xd[j] = xd[j-1];
			yd[j] = yd[j-1];		
		}
#else
		xd[2] = xd[1];
		xd[1] = xd[0];
		yd[2] = yd[1];
		yd[1] = yd[0];
#endif
		xd[0] = x[i];
		yd[0] = result_tmp;
		y[i] = result;

	}
}