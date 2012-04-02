#include <time.h>
#include <gsl/gsl_vector.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_permutation.h>

#include <gsl/gsl_blas.h>
#include <gsl/gsl_math.h>


#include "slra.h"


int slra_gsl_optimize( slraCostFunction *F, opt_and_info *opt, gsl_vector* x_vec, gsl_matrix *v ) {
  const gsl_multifit_fdfsolver_type *Tlm[] =
    {gsl_multifit_fdfsolver_lmder, gsl_multifit_fdfsolver_lmsder};

  const gsl_multimin_fdfminimizer_type *Tqn[] = 
    { gsl_multimin_fdfminimizer_vector_bfgs,
      gsl_multimin_fdfminimizer_vector_bfgs2, 
      gsl_multimin_fdfminimizer_conjugate_fr,
      gsl_multimin_fdfminimizer_conjugate_pr };
               
  const gsl_multimin_fminimizer_type *Tnm[] = 
    { gsl_multimin_fminimizer_nmsimplex, gsl_multimin_fminimizer_nmsimplex2, 
      gsl_multimin_fminimizer_nmsimplex2rand };
  
  int gsl_submethod_max[] = { sizeof(Tlm) / sizeof(Tlm[0]),
			  sizeof(Tqn) / sizeof(Tqn[0]),
			  sizeof(Tnm) / sizeof(Tnm[0]) };  
			  
			  
  int status, status_dx, status_grad, k;
  double g_norm, x_norm;

  /* vectorize x row-wise */
  size_t max_ind, min_ind;
  double max_val, min_val, abs_max_val = 0, abs_min_val;
  
  if (opt->method < 0 || 
      opt->method > sizeof(gsl_submethod_max) / sizeof(gsl_submethod_max[0]) || 
      opt->submethod < 0 || 
      opt->submethod > gsl_submethod_max[opt->method]) {
    throw new slraException("Unknown optimization method.\n");   
  }
  
  /* LM */
  int ls_nfun = opt->ls_correction ? F->getNp() : F->getM() * F->getD();
  gsl_multifit_fdfsolver* solverlm;
  gsl_multifit_function_fdf fdflm = { 
      opt->ls_correction ? &(F->slra_f_cor) : &(F->slra_f_ls), 
      opt->ls_correction ? &(F->slra_df_cor) : &(F->slra_df_ls), 
      opt->ls_correction ? &(F->slra_fdf_cor) : &(F->slra_fdf_ls), 
      ls_nfun, F->getN() * F->getD(), F };
  gsl_vector *g;

  /* QN */
  double stepqn = opt->step; 
  gsl_multimin_fdfminimizer* solverqn;
  gsl_multimin_function_fdf fdfqn = { 
    &(F->slra_f), &(F->slra_df), &(F->slra_fdf), F->getN() * F->getD(), F };


  /* NM */
  double size;
  gsl_vector *stepnm;
  gsl_multimin_fminimizer* solvernm;
  gsl_multimin_function fnm = {&(slraCostFunction::slra_f), F->getN() * F->getD(), F };

  /* initialize the optimization method */
  switch (opt->method) {
  case SLRA_OPT_METHOD_LM: /* LM */
    solverlm = gsl_multifit_fdfsolver_alloc(Tlm[opt->submethod], ls_nfun, F->getN() * F->getD());
    gsl_multifit_fdfsolver_set(solverlm, &fdflm, x_vec);
    g = gsl_vector_alloc(F->getN() * F->getD());
    break;
  case SLRA_OPT_METHOD_QN: /* QN */
    solverqn = gsl_multimin_fdfminimizer_alloc( Tqn[opt->submethod], 
						F->getN() * F->getD() );
    gsl_multimin_fdfminimizer_set(solverqn, &fdfqn, x_vec, 
				  stepqn, opt->tol); 
    status_dx = GSL_CONTINUE;  
    break;
  case SLRA_OPT_METHOD_NM: /* NM */
    solvernm = gsl_multimin_fminimizer_alloc( Tnm[opt->submethod], F->getN() * F->getD() );
    stepnm = gsl_vector_alloc( F->getN() * F->getD() );
    gsl_vector_set_all(stepnm, opt->step); 
    gsl_multimin_fminimizer_set( solvernm, &fnm, x_vec, stepnm );
    break;
  }

  /* optimization loop */
  if (opt->disp == SLRA_OPT_DISP_FINAL || opt->disp == SLRA_OPT_DISP_ITER) {
    PRINTF("STLS optimization:\n");
  }
    
  status = GSL_SUCCESS;  
  status_dx = GSL_CONTINUE;
  status_grad = GSL_CONTINUE;  
  opt->iter = 0;
  
  
  if (opt->method == SLRA_OPT_METHOD_LM && opt->disp == SLRA_OPT_DISP_ITER) {
    gsl_blas_ddot(solverlm->f, solverlm->f, &opt->fmin);

    gsl_multifit_gradient(solverlm->J, solverlm->f, g);	
    x_norm = gsl_blas_dnrm2(solverlm->x);
    g_norm = gsl_blas_dnrm2(g);
    PRINTF("  0: f0 = %16.11f,  ||f0'|| = %16.8f,  ||x|| = %10.8f\n", opt->fmin, g_norm, x_norm);
  }
  
  
 
  while (status_dx == GSL_CONTINUE && 
	 status_grad == GSL_CONTINUE &&
	 status == GSL_SUCCESS &&
	 opt->iter < opt->maxiter) {
    /* print_vec(solverlm->x); */
    opt->iter++;
    switch (opt->method) {
    case SLRA_OPT_METHOD_LM: /* Levenberge-Marquardt */
      status = gsl_multifit_fdfsolver_iterate( solverlm );
      /* check for convergence problems */
      if (status == GSL_ETOLF || 
	  status == GSL_ETOLX || 
	  status == GSL_ETOLG) {
	break; /* <- THIS IS WRONG */
      }
      /* check the convergence criteria */
      status_dx = gsl_multifit_test_delta(solverlm->dx, 
					  solverlm->x, 
					  opt->epsabs, 
					  opt->epsrel);
      gsl_multifit_gradient(solverlm->J, solverlm->f, g);
      status_grad = gsl_multifit_test_gradient(g, opt->epsgrad);
      /* print information */
      if (opt->disp == SLRA_OPT_DISP_ITER) {
	gsl_blas_ddot(solverlm->f, solverlm->f, &opt->fmin);
	
	x_norm = gsl_blas_dnrm2(solverlm->x);
	g_norm = gsl_blas_dnrm2(g);
	
	PRINTF("%3u: f0 = %16.11f,  ||f0'|| = %16.8f,  ||x|| = %10.8f\n",
	       opt->iter, opt->fmin, g_norm, x_norm);
      }
      break;
    case SLRA_OPT_METHOD_QN:
      status = gsl_multimin_fdfminimizer_iterate( solverqn );
/*      if (status == GSL_ENOPROG) {
	break; /* <- THIS IS WRONG * /
      }*/

      /* check the convergence criteria */
      status_grad = gsl_multimin_test_gradient(
		    gsl_multimin_fdfminimizer_gradient(solverqn), 
		    opt->epsgrad );
		    
     status_dx = gsl_multifit_test_delta(solverqn->dx, 
                                         solverqn->x, 
					 opt->epsabs, 
					 opt->epsrel);  		    
      if (opt->disp == SLRA_OPT_DISP_ITER) {
	opt->fmin = gsl_multimin_fdfminimizer_minimum( solverqn );
	x_norm = gsl_blas_dnrm2(solverqn->x);
	g_norm = gsl_blas_dnrm2(solverqn->gradient);
	PRINTF("%3u: f0 = %16.11f,  ||f0'|| = %16.8f,  ||x|| = %10.8f\n", 
	       opt->iter, opt->fmin, g_norm, x_norm);
      }
      break;
    case SLRA_OPT_METHOD_NM:
      status = gsl_multimin_fminimizer_iterate( solvernm );
      /* check the convergence criteria */
      size = gsl_multimin_fminimizer_size( solvernm );
      status_dx = gsl_multimin_test_size( size, opt->epsx );
      /* print information */
      if (opt->disp == SLRA_OPT_DISP_ITER) {
	opt->fmin = gsl_multimin_fminimizer_minimum( solvernm );
	x_norm = gsl_blas_dnrm2(solvernm->x);

	PRINTF("%3u: f0 = %16.11f,  ||x|| = %10.8f\n", 
	       opt->iter, opt->fmin, g_norm, x_norm);
      }
      break;
    }
  } 
  if (opt->iter >= opt->maxiter) {
    status = EITER;
  }

  switch (opt->method) {
  case  SLRA_OPT_METHOD_LM:
    /* return the results */
    gsl_vector_memcpy(x_vec, solverlm->x);
    if (v != NULL) {
      gsl_multifit_covar(solverlm->J, opt->epsrel, v); /* ??? Different eps */
    }
    /* assign the opt output fields */
    gsl_blas_ddot(solverlm->f, solverlm->f, &opt->fmin);
    break;
  case SLRA_OPT_METHOD_QN:
    gsl_vector_memcpy(x_vec, solverqn->x);

    opt->fmin = solverqn->f;
    break;
  case SLRA_OPT_METHOD_NM:
    /* return the results */
    gsl_vector_memcpy(x_vec, solvernm->x);
    /* gsl_multifit_covar( J??, opt->epsrel, v); */
    /* assign the opt output fields */
    opt->fmin = solvernm->fval;
    break;
  }
  
/*  if (opt->disp == SLRA_OPT_DISP_ITER) {
     opt->chol_time =  ((double)P->chol_time / CLOCKS_PER_SEC) / P->chol_count;
  }*/

  
  /* print exit information */  
  if (opt->disp != SLRA_OPT_DISP_OFF) { /* unless "off" */
    switch (status) {
    case EITER: 
      PRINTF("STLS optimization terminated by reaching the maximum number " 
	     "of iterations.\nThe result could be far from optimal.\n");
      break;
    case GSL_ETOLF:
      PRINTF("Lack of convergence: progress in function value < machine EPS.\n");
      break;
    case GSL_ETOLX:
      PRINTF("Lack of convergence: change in parameters < machine EPS.\n");
      break;
    case GSL_ETOLG:
      PRINTF("Lack of convergence: change in gradient < machine EPS.\n");
      break;
    case GSL_ENOPROG:
      PRINTF("Possible lack of convergence: no progress.\n");
      break;
    }
    if (!status && (opt->disp == SLRA_OPT_DISP_FINAL || 
                    opt->disp == SLRA_OPT_DISP_ITER)) { 
      if (status_grad == GSL_CONTINUE) {
	PRINTF("Optimization terminated by reaching the convergence " 
	       "tolerance for X.\n");
      } else if (status_dx == GSL_CONTINUE) {
	PRINTF("Optimization terminated by reaching the convergence " 
	       "tolerance for the gradient.\n");
      } else {
	PRINTF("Optimization terminated by reaching the convergence " 
	       "tolerance for both X and the gradient.\n"); 
      }
    }
  }


  /* Cleanup  */
  switch (opt->method) {
  case SLRA_OPT_METHOD_LM: /* LM */
    gsl_multifit_fdfsolver_free(solverlm);
    gsl_vector_free(g);
    break;
  case SLRA_OPT_METHOD_QN: /* QN */
    gsl_multimin_fdfminimizer_free(solverqn);
    break;
  case SLRA_OPT_METHOD_NM: /* NM */
    gsl_multimin_fminimizer_free(solvernm);
    gsl_vector_free(stepnm);
    break;
  }



  return GSL_SUCCESS; /* <- correct with status */
}



