#include "R.h"
#include "Rdefines.h"
#include "R_ext/BLAS.h"
#include "utils.h"
#include "sag_linesearch.h"

void sag_linesearch_warm(GlmTrainer* trainer, GlmModel* model, Dataset* dataset,
                       double* lambdas, int nLambdas, double * lambda_w);