#include <stdlib.h>
#include <assert.h>
#include "Common.h"
#include "Matrix.h"
#include "ParaSails.h"
#include "ConjGrad.h"

extern double beta;

void report_times(MPI_Comm comm, double setup_time, double solve_time)
{
    int mype, npes;
    double setup_times[1024];
    double max_solve_time;
    double m = 0.0, tot = 0.0;
    int i;

    MPI_Comm_rank(comm, &mype);
    MPI_Comm_size(comm, &npes);

    MPI_Gather(&setup_time, 1, MPI_DOUBLE, setup_times, 1, MPI_DOUBLE, 0, comm);

    MPI_Reduce(&solve_time, &max_solve_time, 1, MPI_DOUBLE, MPI_MAX, 0, comm);

    if (mype != 0)
	return;

    printf("***********************\n");
    for (i=0; i<npes; i++)
    {
	printf("*** %2d * %10.3f ***\n", i, setup_times[i]);
	m = MAX(m, setup_times[i]);
	tot += setup_times[i];
    }
    printf("***********************\n");
    printf("*** ave: %10.3f ***\n", tot / (double) npes);
    printf("*** bal: %10.3f ***\n", tot / (double) npes / m);
    printf("***********************\n");
    printf("***      Setup      Solve      Total\n");
    printf("III %10.3f %10.3f %10.3f\n", m, max_solve_time, m+max_solve_time);
    printf("***********************\n");
}

int main(int argc, char *argv[])
{
    int mype, npes;
    Matrix *A;
    ParaSails *ps;
    FILE *file;
    int n, beg_row, end_row;
    double time0, time1;
    double setup_time, solve_time;

    double *x, *y, *b;
    int i, j;
    double thresh;
    double selparam;
    int nlevels;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &mype);
    MPI_Comm_size(MPI_COMM_WORLD, &npes);

    /* Read number of rows in matrix */
    file = fopen(argv[1], "r");
    assert(file != NULL);
    fscanf(file, "%d\n", &n);
    fclose(file);
    assert(n >= npes);

    beg_row = (int) ((double)(mype*n) / npes) + 1; /* assumes 1-based */
    end_row = (int) ((double)((mype+1)* n) / npes);
    if (mype == 0)
        assert(beg_row == 1);
    if (mype == npes-1)
        assert(end_row == n);

    x = (double *) malloc((end_row-beg_row+1) * sizeof(double));
    b = (double *) malloc((end_row-beg_row+1) * sizeof(double));

    A = MatrixCreate(MPI_COMM_WORLD, beg_row, end_row);

    MatrixRead(A, argv[1]);
    /* MatrixPrint(A, "A"); */

    /* Right-hand side */
    if (argc > 2)
        RhsRead(b, A, argv[2]);
    else
        for (i=0; i<end_row-beg_row+1; i++)
            b[i] = (double) (2*rand()) / (double) RAND_MAX - 1.0;

    while (1)
    {
        /* Initial guess */
        for (i=0; i<end_row-beg_row+1; i++)
            x[i] = 0.0;

#if ONE_TIME
        selparam = 0.75;
	nlevels = 1;
#else
	fflush(NULL);
        MPI_Barrier(MPI_COMM_WORLD);

        if (mype == 0)
        {
            printf("Enter parameters selparam (0.75), nlevels (1), beta: ");
            scanf("%lf %d %lf", &selparam, &nlevels, &beta);
            printf("selparam %f, nlevels %d, beta %f\n", selparam, nlevels, beta);
            fflush(stdout);
	}

	MPI_Bcast(&selparam, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
	MPI_Bcast(&nlevels,  1, MPI_INT,    0, MPI_COMM_WORLD);
	MPI_Bcast(&beta,     1, MPI_DOUBLE, 0, MPI_COMM_WORLD);

        if (nlevels < 0)
            break;
#endif

        MPI_Barrier(MPI_COMM_WORLD);
        time0 = MPI_Wtime();
        ps = ParaSailsCreate(A);
        thresh = ParaSailsSelectThresh(ps, selparam);
/*thresh=10.0;*/
        ParaSailsSetupPattern(ps, thresh, nlevels);
        ParaSailsSetupValues(ps, A);
        time1 = MPI_Wtime();
	setup_time = time1-time0;

        i = MatrixNnz(ps->M);
        j = (MatrixNnz(A) - n) / 2 + n;
        if (mype == 0) 
        {
            printf("%s\n", argv[1]);
            printf("Inumber of nonzeros: %d (%.2f)\n", i, i/(double)j);
        }
        /*MatrixPrint(ps->M, "M");*/

        time0 = MPI_Wtime();
        PCG_ParaSails(A, ps, b, x, 1.e-8, 1500);
        time1 = MPI_Wtime();
	solve_time = time1-time0;

	report_times(MPI_COMM_WORLD, setup_time, solve_time);

        MatrixMatvecComplete(A); /* convert matrix back to global numbering */
        ParaSailsDestroy(ps);

#if ONE_TIME
        break;
#endif
    }

    free(x);
    free(b);

    MatrixDestroy(A);
    MPI_Finalize();

    return 0;
}
