#include "sag_linesearch_warm.h"

const static int DEBUG = 1;
const static int one = 1;

void sag_linesearch_warm(GlmTrainer* trainer, GlmModel* model, Dataset* dataset,
                       double * lambdas, int nLambdas, double * lambda_w) {

  int nVars = dataset->nVars;
  for (int i = 0; i < nLambdas; i++) {
    /* selecting lambda */
    trainer->lambda = lambdas[i];
    if (DEBUG) {
      R_TRACE("Warm training lambda[%d]=%f", i, lambdas[i]);
    }
    /* Training */
    sag_linesearch(trainer, model, dataset);
    /* Copying weights */
    F77_CALL(dcopy)(&nVars, model->w, &one, &lambda_w[nVars * i], &one);
  }

}