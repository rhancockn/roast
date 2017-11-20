/* $Id: spm_mrf.c 4356 2011-06-14 12:16:24Z john $ */
/* (c) John Ashburner (2010) */

#include "mex.h"
#include <math.h>
#define MAXCLASSES 1024

#define mwSize int


static void mrf1(mwSize dm[], unsigned char q[], float p[], float G[], float w[], int code)
{
    mwSize i0, i1, i2, k, m, n;
    float a[MAXCLASSES], e[MAXCLASSES], *p0, *p1;
    unsigned char *q0, *q1;
    int it;

    m = dm[0]*dm[1]*dm[2];

    /* Use a red-black scheme, so the updates are for
       alternating voxels.  Then do another pass to
       update the other half.
       A B A B A B
       B A B A B A
       A B A B A B
       B A B A B A

       Updates involve computing the number of neighbours
       of each type (stored in vector a), and using the
       connectivity matrix (G) to update with:
           q = (p.*exp(G'*a))/sum((p.*exp(G'*a))
    */
    /* Use checkerboard updating (synchronous) scheme, but actually each voxel is visited sequentially.
     Why not directly use pure sequential (asynchronous) updating, i.e., voxel is visited from q[0],q[1],...q[numOfVoxels-1]?
     Because in that way, the order of updating will have effects on neighboring voxels, although very small.
     See Roche 2011, asynchronous updating makes algorithm dependent on voxel traversal.
     While checkerboard updating (6-neighbor) does NOT have any effect on neighboring voxels.
     Keep in mind that checkerboard can only do 6-neighbor, but pure sequential can do up to 26-neighbor.
     ANDY 2013-04-22
     */
    for(it=0; it<2; it++) /*checkerboard pass  ANDY 2013-04-22*/
    {
        mwSize i2start = it%2; /*implement checkerboard visiting pattern  ANDY 2013-04-22*/
        for(i2=0; i2<dm[2]; i2++) /* Inferior -> Superior */
        {
            mwSize i1start = (i2start == (i2%2));
            for(i1=0; i1<dm[1]; i1++) /* Posterior -> Anterior */
            {
                mwSize i0start = (i1start == (i1%2));
                p1 = p + dm[0]*(i1+dm[1]*i2);
                q1 = q + dm[0]*(i1+dm[1]*i2);
/* NOTE: convert Matlab indexing into linear indexing, for COLUMN-first indexing (Matlab matrix style)
If "a" is M x N x P, then an appropriate define for 0-based indexing would be:
 
#define A(i,j,k) a[(i)+((j)+(k)*N)*M]

where
i = 0,...,M-1
j = 0,...,N-1
k = 0,...,P-1
and an appropriate define for 1-based indexing would be:

#define A(i,j,k) a[(i-1)+((j-1)+(k-1)*N)*M]

where
i = 1,...,M
j = 1,...,N
k = 1,...,P 
 
Here we have A(i0,i1,i2), is equivalent to A[i0 + dm[0]*(i1 + dm[1]*i2)], 0-based

 Therefore: 0-based/1-based different; row-first/column-first indexing different
 ANDY 2013-04-22 */

                for(i0=i0start; i0<dm[0]; i0+=2) /* Left -> Right */
                {
                    float se;
                    unsigned char *qq;

                    /* Pointers to current voxel in first volume */
                    p0 = p1 + i0;
                    q0 = q1 + i0;

                    /* Initialise neighbour counts to zero */
                    for(k=0; k<dm[3]; k++) a[k] = 0.0; /*the 'counts' are actually probabilities (normalized or updated), from the first argument of spm_mrf  ANDY 2013-04-22*/

                    /* Count neighbours of each class */
                    if(i2>0)       /* Inferior */
                    {

                        qq = q0 - dm[0]*dm[1];
                        /*since q is from Matlab matrix, it's column-first indexing, first dim varies fastest, so the code is like this; if row-first indexing, then last dim varies fastest, then just qq = q0-1,
                         also, this is along superior-inferior direction ONLY if the image is in RAS orientation, if in NON-RAS, then this is NOT along superior-inferior direction, but other direction. ANDY 2013-05-07*/
                        for(k=0; k<dm[3]; k++) a[k] += qq[k*m]*w[2];
                    }

                    if(i2<dm[2]-1) /* Superior */
                    {
                        qq = q0 + dm[0]*dm[1];
                        for(k=0; k<dm[3]; k++) a[k] += qq[k*m]*w[2];
                    }

                    if(i1>0)       /* Posterior */
                    {
                        qq = q0 - dm[0];
                        for(k=0; k<dm[3]; k++) a[k] += qq[k*m]*w[1];
                    }

                    if(i1<dm[1]-1) /* Anterior */
                    {
                        qq = q0 + dm[0];
                        for(k=0; k<dm[3]; k++) a[k] += qq[k*m]*w[1];
                    }

                    if(i0>0)       /* Left */
                    {
                        qq = q0 - 1;
                        for(k=0; k<dm[3]; k++) a[k] += qq[k*m]*w[0];
                    }

                    if(i0<dm[0]-1) /* Right */
                    {
                        qq = q0 + 1;
                        for(k=0; k<dm[3]; k++) a[k] += qq[k*m]*w[0];
                    }

                    /* Responsibility data is uint8, so correct scaling.
                       Note also that data is divided by 6 (the number
                       of neighbours examined). */
                    for(k=0; k<dm[3]; k++)
                        a[k]/=(255.0*6.0);

                    if (code == 1) 
                    {
                        /* Weights are in the form of a matrix,
                           shared among all voxels. */
                        float *g;
                        se = 0.0;
                        for(k=0, g=G; k<dm[3]; k++)
                        {
                            e[k] = 0;
                            for(n=0; n<dm[3]; n++, g++)
                            /*Problem!: Here g points to G, which is from Matlab matrix, so should be column-first indexing, then
                              g++ goes along each column. But our algorithm requires dot product between each ROW of matrix G and
                              vector a. It is fine if G is a symmetric matrix, but NOT good when G is not symmetric. ANDY 2013-05-07*/
                                e[k] += (*g)*a[n];
                            e[k] = exp((double)e[k])*p0[k*m]; /*updating this voxel, using direct result from New Segment, the second argument of spm_mrf  ANDY 2013-04-22*/
                            se  += e[k];
                        }
                    }
                    else if (code == 2)
                    {
                        /* Weights are assumed to be a diagonal matrix,
                           so only the diagonal elements are passed. */
                        se = 0.0;
                        for(k=0; k<dm[3]; k++)
                        {
                            e[k] = exp((double)(G[k]*a[k]))*p0[k*m];
                            se  += e[k];
                        }
                    }
                    else if (code == 3)
                    {
                        /* Separate weights for each voxel, in the form of
                           the full matrix (loads of memory). */
                        float *g;
                        se = 0.0;
                        g = G + i0+dm[0]*(i1+dm[1]*i2);
                        for(k=0; k<dm[3]; k++)
                        {
                            e[k] = 0.0;
                            for(n=0; n<dm[3]; n++, g+=m)
                            /*Problem!: same problem in local G. g points to some location in the first volume of G, then g+=m goes along each 'column'.
                              But our algorithm requires dot product between each 'ROW' of (local version of) matrix G and
                              vector a. Usually local version of G is NOT symmetric (our local TCM not symmetric), so this code needs to be modified. ANDY 2013-05-07*/
                                e[k] += (*g)*a[n];
                            e[k] = exp((double)e[k])*p0[k*m];
                            se  += e[k];
                        }
                    }
                    else if (code == 4)
                    {
                        /* Separate weight matrices for each voxel,
                           where the matrices are assumed to be symmetric
                           with zeros on the diagonal. For a 4x4
                           matrix, the elements are ordered as
                           (2,1), (3,1), (4,1), (3,2), (4,2), (4,3).
                         */
                        float *g;
                        g = G + i0+dm[0]*(i1+dm[1]*i2);
                        for(k=0; k<dm[3]; k++) e[k] = 0.0;

                        for(k=0; k<dm[3]; k++)
                        {
                            for(n=k+1; n<dm[3]; n++, g+=m)
                            {
                                e[k] += (*g)*a[n];
                                e[n] += (*g)*a[k];
                            }
                        }
                        se = 0.0;
                        for(k=0; k<dm[3]; k++)
                        {
                            e[k] = exp((double)e[k])*p0[k*m];
                            se  += e[k];
                        }
                    }
                    else if (code == 5)
                    {
                        /* Separate weight matrices for each voxel,
                           where the matrices are assumed to be symmetric
                           with zeros on the diagonal. For a 4x4
                           matrix, the elements are ordered as
                           (2,1), (3,1), (4,1), (3,2), (4,2), (4,3).
                           
                           The weight matrices are encoded as uint8, and
                           their values need to be scaled by -0.0625 to
                           bring them into a reasonable range.
                         */
                        unsigned char *g;
                        g = (unsigned char *)G + i0+dm[0]*(i1+dm[1]*i2);
                        for(k=0; k<dm[3]; k++) e[k] = 0.0;

                        for(k=0; k<dm[3]; k++)
                        {
                            for(n=k+1; n<dm[3]; n++, g+=m)
                            {
                                e[k] += ((float)(*g))*a[n];
                                e[n] += ((float)(*g))*a[k];
                            }
                        }
                        se = 0.0;
                        for(k=0; k<dm[3]; k++)
                        {
                            e[k] = exp(-0.0625*e[k])*p0[k*m];
                            se  += e[k];
                        }
                    }


                    /* Normalise responsibilities to sum to 1
                       and rescale for saving as uint8 data. */
                    se = 255.0/se;
                    for(k=0; k<dm[3]; k++)
                        q0[k*m] = (unsigned char)(e[k]*se+0.5); /*normalize and assign back to first argument of spm_mrf, the final result  ANDY 2013-04-22*/

                }
            }
        }
    }
}

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    mwSize i;
    mwSize dm[4];
    float *p, w[3];
    unsigned char *q;
    float *G;
    int code=0;

    if (nrhs<3 || nrhs>4 || nlhs>1)
        mexErrMsgTxt("Incorrect usage");

    if (!mxIsNumeric(prhs[0]) || mxIsComplex(prhs[0]) || mxIsSparse(prhs[0]) || !mxIsUint8(prhs[0]))
        mexErrMsgTxt("First arg must be numeric, real, full and uint8.");

    if (!mxIsNumeric(prhs[1]) || mxIsComplex(prhs[1]) || mxIsSparse(prhs[1]) || !mxIsSingle(prhs[1]))
        mexErrMsgTxt("Second arg must be numeric, real, full and single.");

    if (!mxIsNumeric(prhs[2]) || mxIsComplex(prhs[2]) || mxIsSparse(prhs[2]))
        mexErrMsgTxt("Third arg must be numeric, real and full.");

    if (mxGetNumberOfDimensions(prhs[0])!= mxGetNumberOfDimensions(prhs[1]) ||
        mxGetNumberOfDimensions(prhs[0])>4)
        mexErrMsgTxt("First or second args have wrong number of dimensions.");

    for(i=0; i<mxGetNumberOfDimensions(prhs[0]); i++)
        dm[i] = mxGetDimensions(prhs[0])[i];

    for(i=mxGetNumberOfDimensions(prhs[0]); i<4; i++)
        dm[i] = 1;

    if (dm[3]>MAXCLASSES) mexErrMsgTxt("Too many classes.");

    for(i=0; i<4; i++)
        if (mxGetDimensions(prhs[1])[i] != dm[i])
            mexErrMsgTxt("First and second args have incompatible dimensions.");

    if (mxGetDimensions(prhs[2])[1] == 1)
    {
        code = 2;
        if (mxGetDimensions(prhs[2])[0] != dm[3])
            mexErrMsgTxt("Third arg has incompatible dimensions.");

        if (!mxIsSingle(prhs[2])) mexErrMsgTxt("Third arg must be single.");
    }
    else if (mxGetNumberOfDimensions(prhs[2])==2)
    {
        code = 1;
        if (mxGetDimensions(prhs[2])[0] != dm[3] || mxGetDimensions(prhs[2])[1] != dm[3])
            mexErrMsgTxt("Third arg has incompatible dimensions.");

        if (!mxIsSingle(prhs[2])) mexErrMsgTxt("Third arg must be single.");
    }
    else if (mxGetNumberOfDimensions(prhs[2])==5)
    {
        code = 3;
        for(i=0; i<4; i++)
            if (mxGetDimensions(prhs[2])[i] != dm[i])
                mexErrMsgTxt("Third arg has incompatible dimensions.");

        if (mxGetDimensions(prhs[2])[4] != dm[3])
            mexErrMsgTxt("Third arg has incompatible dimensions.");

        if (!mxIsSingle(prhs[2])) mexErrMsgTxt("Third arg must be single.");
    }
    else if (mxGetNumberOfDimensions(prhs[2])==4)
    {
        for(i=0; i<3; i++)
            if (mxGetDimensions(prhs[2])[i] != dm[i])
                mexErrMsgTxt("Third arg has incompatible dimensions.");

        if (mxGetDimensions(prhs[2])[3] != (dm[3]*(dm[3]-1))/2)
            mexErrMsgTxt("Third arg has incompatible dimensions.");

        if (mxIsSingle(prhs[2]))
            code = 4;
        else if (mxIsUint8(prhs[0]))
            code = 5;
        else
            mexErrMsgTxt("Third arg must be either single or uint8.");
    }
    else
        mexErrMsgTxt("Third arg has incompatible dimensions.");


    p = (float *)mxGetData(prhs[1]); /*probabilities not normalized, direct result from New Segment  ANDY 2013-04-22*/
    G = (float *)mxGetData(prhs[2]); /*MRF vector/matrix  ANDY 2013-04-22*/

    if (nrhs>=4)
    {
        /* Adjustment for anisotropic voxel sizes.  w should contain
           the square of each voxel size. */
        if (!mxIsNumeric(prhs[3]) || mxIsComplex(prhs[3]) || mxIsSparse(prhs[3]) || !mxIsSingle(prhs[3]))
            mexErrMsgTxt("Fourth arg must be numeric, real, full and single.");

        if (mxGetNumberOfElements(prhs[3]) != 3)
            mexErrMsgTxt("Fourth arg must contain three elements.");

        for(i=0; i<3; i++) w[i] = ((float *)mxGetData(prhs[3]))[i];
    }
    else
    {
        for(i=0; i<3; i++) w[i] = 1.0;
    }

    if (nlhs>0)
    {
        /* Copy input to output */
        unsigned char *q0;
        plhs[0]  = mxCreateNumericArray(4,dm, mxUINT8_CLASS, mxREAL);
        q0 = (unsigned char *)mxGetData(prhs[0]);
        q  = (unsigned char *)mxGetData(plhs[0]);

        for(i=0; i<dm[0]*dm[1]*dm[2]*dm[3]; i++)
            q[i] = q0[i];
    }
    else /* Note the nasty side effects - but it does save memory */
        q = (unsigned char *)mxGetData(prhs[0]); /*probabilities normalized, final result after cleanup, saved in uint8  ANDY 2013-04-22*/
             /*because prhs[0] is of type uint8, only unsigned char is consistent with this type  ANDY 2013-05-06*/
    mrf1(dm, q,p,G,w,code); /*main function ANDY 2013-04-22*/
}
