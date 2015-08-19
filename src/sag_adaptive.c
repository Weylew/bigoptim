#include "sag_adaptive.h"

const static int one = 1;

void sag_adaptive(GlmTrainer *trainer, GlmModel *model, Dataset *dataset) {

  /* Unpacking Structs */

  /* Dataset */
  double *y = dataset->y;
  double *Xt = dataset->Xt;
  double *Li = dataset->Li;
  double *Lmax = dataset->Lmax;
  int increasing = dataset->increasing;

  /* Dimensions */
  int nVars = dataset->nVars;
  int nSamples = dataset->nSamples;

  /* Sampling */
  double *randVals = dataset->randVals;
  int *covered = dataset->covered;
  double *unCoveredMatrix = dataset->unCoveredMatrix;
  double *LiMatrix = dataset->LiMatrix;
  double *nDescendants = dataset->nDescendants;
  double *nCovered = &dataset->nCovered;
  double *Lmean = &dataset->Lmean;
  int nLevels = dataset->nLevels;
  int nextpow2 = dataset->nextpow2;

  /* Training parameters */
  int maxIter = trainer->maxIter;
  double lambda = trainer->lambda;
  double alpha = trainer->alpha;
  double precision = trainer->precision;
  double tol = trainer->tol;

  /* Model */
  double *w = model->w;
  loss_fun loss_function = model->loss;
  loss_grad_fun grad_fun = model->grad;

  /* Sparse related variables */
  int sparse = dataset->sparse;
  int *jc = NULL, *ir = NULL;
  int *lastVisited = NULL;
  double *cumSum = NULL;
  if (sparse) {
    /* Sparce indices*/
    jc = dataset->jc;
    ir = dataset->ir;
    lastVisited = dataset->lastVisited;
    cumSum = dataset->cumSum;
  }
  /* Approximate gradients*/
  double *g = trainer->g;
  double *d = trainer->d;

  /* Training */
  _sag_adaptive(w, Xt, y, Li, Lmax, increasing, nVars, nSamples, randVals,
                covered, unCoveredMatrix, LiMatrix, nDescendants, nCovered,
                Lmean, nLevels, nextpow2, maxIter, lambda, alpha, precision,
                tol, loss_function, grad_fun, sparse, jc, ir, lastVisited,
                cumSum, d, g);

  Free(lastVisited);
  Free(cumSum);
  Free(nDescendants);
  Free(unCoveredMatrix);
  Free(LiMatrix);
}

void _sag_adaptive(double *w, double *Xt, double *y, double *Li, double *Lmax,
                   int increasing, int nVars, int nSamples, double *randVals,
                   int *covered, double *unCoveredMatrix, double *LiMatrix,
                   double *nDescendants, double *nCovered, double *Lmean,
                   int nLevels, int nextpow2, int maxIter, double lambda,
                   double alpha, double precision, double tol,
                   loss_fun loss_function, loss_grad_fun grad_fun, int sparse,
                   int *jc, int *ir, int *lastVisited, double *cumSum,
                   double *d, double *g) {

  /* Training variables*/
  int i = 0, ind = 0;
  double offset = 0;
  double c = 1.0;
  double scaling = 0, innerProd = 0, grad = 0;
  double fi = 0, fi_new = 0;
  double Li_old = 0;
  double gg = 0, wtx = 0, xtx = 0;
  double u = 0, z = 0, Z = 0;

  double cost_agrad_norm =
      get_cost_agrad_norm(w, d, lambda, *nCovered, nSamples, nVars);
  int stop_condition = 0;
  /* Training Loop */
  int k = 0; // TODO(Ishmael): Consider using the register keyword
  while (!stop_condition) {
    /* Select next training example */
    offset = 0;
    i = 0;
    u = randVals[k + maxIter];
    if (randVals[k] < (double)(nSamples - *nCovered) / (double)nSamples) {
      /* Sample fron uncovered guys */
      Z = unCoveredMatrix[nextpow2 * (nLevels - 1)];
      for (int level = nLevels - 1; level >= 0; level--) {
        z = offset + unCoveredMatrix[2 * i + nextpow2 * level];
        if (u < z / Z)
          i = 2 * i;
        else {
          offset = z;
          i = 2 * i + 1;
        }
      }
    } else {
      /* Sample from covered guys according to estimate of Lipschitz constant */
      Z = LiMatrix[nextpow2 * (nLevels - 1)] +
          (*Lmean + 2 * lambda) * (nDescendants[nextpow2 * (nLevels - 1)] -
                                   unCoveredMatrix[nextpow2 * (nLevels - 1)]);
      for (int level = nLevels - 1; level >= 0; level--) {
        z = offset + LiMatrix[2 * i + nextpow2 * level] +
            (*Lmean + 2 * lambda) * (nDescendants[2 * i + nextpow2 * level] -
                                     unCoveredMatrix[2 * i + nextpow2 * level]);
        if (u < z / Z) {
          i = 2 * i;
        } else {
          offset = z;
          i = 2 * i + 1;
        }
      }
      /*printf("i = %d\n",i);*/
    }

    /* Compute current values of needed parameters */
    if (sparse && k > 0) {
      for (int j = jc[i]; j < jc[i + 1]; j++) {
        if (lastVisited[ir[j]] == 0) {
          w[ir[j]] -= d[ir[j]] * cumSum[k - 1];
        } else {
          w[ir[j]] -=
              d[ir[j]] * (cumSum[k - 1] - cumSum[lastVisited[ir[j]] - 1]);
        }
        lastVisited[ir[j]] = k;
      }
    }

    /* Compute derivative of loss */
    innerProd = 0;
    if (sparse) {
      for (int j = jc[i]; j < jc[i + 1]; j++)
        innerProd += w[ir[j]] * Xt[j];
      innerProd *= c;
    } else {
      innerProd = F77_CALL(ddot)(&nVars, w, &one, &Xt[nVars * i], &one);
    }
    grad = grad_fun(y[i], innerProd);

    /* Update direction */
    if (sparse) {
      for (int j = jc[i]; j < jc[i + 1]; j++)
        d[ir[j]] += Xt[j] * (grad - g[i]);
    } else {
      scaling = grad - g[i];
      F77_CALL(daxpy)(&nVars, &scaling, &Xt[i * nVars], &one, d, &one);
    }

    /* Store derivative of loss */
    g[i] = grad;

    /* Line-search for Li */
    Li_old = Li[i];
    if (increasing && covered[i])
      Li[i] /= 2;
    fi = loss_function(y[i], innerProd);
    /* Compute f_new as the function value obtained by taking
     * a step size of 1/Li in the gradient direction */
    wtx = innerProd;
    xtx = 0;
    if (sparse) {
      for (int j = jc[i]; j < jc[i + 1]; j++)
        xtx += Xt[j] * Xt[j];
    } else {
      xtx = F77_CALL(ddot)(&nVars, &Xt[nVars * i], &one, &Xt[nVars * i], &one);
    }
    gg = grad * grad * xtx;
    innerProd = wtx - xtx * grad / Li[i];
    fi_new = loss_function(y[i], innerProd);
    /*printf("fi = %e, fi_new = %e, gg = %e\n",fi,fi_new,gg);*/
    while (gg > precision && fi_new > fi - gg / (2 * (Li[i]))) {
      /*printf("Lipschitz Backtracking (k = %d, fi = %e, fi_new = %e, 1/Li =
       * %e)\n",k+1,fi,fi_new,1/(Li[i]));*/
      Li[i] *= 2;
      innerProd = wtx - xtx * grad / Li[i];
      fi_new = loss_function(y[i], innerProd);
    }

    if (Li[i] > *Lmax)
      *Lmax = Li[i];

    /* Update the number of examples that we have seen */
    if (covered[i] == 0) {
      covered[i] = 1;
      (*nCovered)++;
      *Lmean = *Lmean *((double)(*nCovered - 1) / (double)*nCovered) +
               Li[i] / (double)*nCovered;

      /* Update unCoveredMatrix so we don't sample this guy when looking for a
       * new guy */
      ind = i;
      for (int level = 0; level < nLevels; level++) {
        unCoveredMatrix[ind + nextpow2 * level] -= 1;
        ind = ind / 2;
      }
      /* Update LiMatrix so we sample this guy proportional to its Lipschitz
       * constant*/
      ind = i;
      for (int level = 0; level < nLevels; level++) {
        LiMatrix[ind + nextpow2 * level] += Li[i];
        ind = ind / 2;
      }
    } else if (Li[i] != Li_old) {
      *Lmean = *Lmean + (Li[i] - Li_old) / (double)*nCovered;
      /* Update LiMatrix with the new estimate of the Lipscitz constant */
      ind = i;
      for (int level = 0; level < nLevels; level++) {
        LiMatrix[ind + nextpow2 * level] += (Li[i] - Li_old);
        ind = ind / 2;
      }
    }

    /*for(ind=0;ind<nextpow2;ind++) {
        for(j=0;j<nLevels;j++) {
            printf("%f ",LiMatrix[ind + nextpow2*j]);
        }
        printf("\n");
        }
    */

    /* Compute step size */
    alpha =
        ((double)(nSamples - *nCovered) / (double)nSamples) / (*Lmax + lambda) +
        ((double)*nCovered / (double)nSamples) *
            (1 / (2 * (*Lmax + lambda)) + 1 / (2 * (*Lmean + lambda)));

    /* Update parameters */
    if (sparse) {
      c *= 1 - alpha * lambda;
      if (k == 0) {
        cumSum[0] = alpha / (c * *nCovered);
      } else {
        cumSum[k] = cumSum[k - 1] + alpha / (c * *nCovered);
      }
    } else {
      scaling = 1 - alpha * lambda;
      F77_CALL(dscal)(&nVars, &scaling, w, &one);
      scaling = -alpha / *nCovered;
      F77_CALL(daxpy)(&nVars, &scaling, d, &one, w, &one);
    }

    /* Decrease value of max Lipschitz constant */
    if (increasing)
      *Lmax *= pow(2.0, -1.0 / nSamples);
    /* Incrementing iteration count */
    k++;
    /* Checking Stopping criterions */
    cost_agrad_norm =
        get_cost_agrad_norm(w, d, lambda, *nCovered, nSamples, nVars);
    stop_condition = (k >= maxIter) || (cost_agrad_norm <= tol);
  }

  if (sparse) {
    for (int j = 0; j < nVars; j++) {
      if (lastVisited[j] == 0) {
        w[j] -= d[j] * cumSum[maxIter - 1];
      } else {
        w[j] -= d[j] * (cumSum[maxIter - 1] - cumSum[lastVisited[j] - 1]);
      }
    }
    scaling = c;
    F77_CALL(dscal)(&nVars, &scaling, w, &one);
  }
}
