class StripedStructure : public Structure {
  size_t myBlocksN;

  /* Helper variables */
  size_t myN;
  size_t myNp;

  size_t myMaxMlInd;
protected:
  StripedStructure( size_t blocksN, Structure **stripe );

public:
  Structure **myStripe;
  virtual ~StripedStructure();

  virtual int getN() const { return myN; }
  virtual int getNp() const { return myNp; }
  virtual int getM() const { return myStripe[0]->getM(); }
  
  int getBlocksN() const { return myBlocksN; }

  int getMl( int k ) const { return myStripe[k]->getN(); }
  const Structure *getBlock( size_t k ) const { 
    return myStripe[k]; 
  }

  int getMaxMl() const { return getMl(myMaxMlInd); }
  const Structure *getMaxBlock() const { 
    return getBlock(myMaxMlInd); 
  }

  virtual void fillMatrixFromP( gsl_matrix* c, const gsl_vector* p ) ;
  virtual void correctP( gsl_vector* p, gsl_matrix *R, gsl_vector *yr,
                         bool scaled );

  virtual Cholesky *createCholesky( int D, double reg_gamma ) const;
  virtual DGamma *createDGamma( int D ) const;
};

class StripedCholesky : virtual public Cholesky {
  Cholesky **myGamma;
  int myD;
  const StripedStructure *myStruct;
public:  
  StripedCholesky( const StripedStructure *s, int D, double reg_gamma );
  virtual ~StripedCholesky();

  
  virtual void calcGammaCholesky( gsl_matrix *R );

  virtual void multInvCholeskyVector( gsl_vector * yr, int trans );  
  virtual void multInvGammaVector( gsl_vector * yr );                
};

class StripedDGamma : virtual public DGamma {
  DGamma **myLHDGamma;
  const StripedStructure *myStruct;
  gsl_matrix *myTmpGrad;
public:  
  StripedDGamma( const StripedStructure *s, int D  ) ;
  virtual ~StripedDGamma();

  virtual void calcYrtDgammaYr( gsl_matrix *grad, gsl_matrix *R, 
                   gsl_vector *yr );

  virtual void calcDijGammaYr( gsl_vector *res, gsl_matrix *R, 
                   gsl_matrix *perm, int i, int j, gsl_vector *Yr );

};

