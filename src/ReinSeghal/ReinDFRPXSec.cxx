//____________________________________________________________________________
/*
 Copyright (c) 2003-2009, GENIE Neutrino MC Generator Collaboration
 For the full text of the license visit http://copyright.genie-mc.org
 or see $GENIE/LICENSE

 Author: Costas Andreopoulos <costas.andreopoulos \at stfc.ac.uk>
         STFC, Rutherford Appleton Laboratory - February 15, 2008

 For the class documentation see the corresponding header file.

 Important revisions after version 2.0.0 :
 @ Feb 15, 2009 - CA
   This class was first added in version 2.5.1. 
*/
//____________________________________________________________________________

#include <TMath.h>

#include "Algorithm/AlgConfigPool.h"
#include "Conventions/GBuild.h"
#include "Conventions/Constants.h"
#include "Conventions/Controls.h"
#include "Conventions/RefFrame.h"
#include "Messenger/Messenger.h"
#include "PDG/PDGUtils.h"
#include "ReinSeghal/ReinDFRPXSec.h"
#include "Utils/KineUtils.h"
#include "Utils/HadXSUtils.h"

using namespace genie;
using namespace genie::constants;
using namespace genie::controls;
using namespace genie::utils;

//____________________________________________________________________________
ReinDFRPXSec::ReinDFRPXSec() :
XSecAlgorithmI("genie::ReinDFRPXSec")
{

}
//____________________________________________________________________________
ReinDFRPXSec::ReinDFRPXSec(string config) :
XSecAlgorithmI("genie::ReinDFRPXSec", config)
{

}
//____________________________________________________________________________
ReinDFRPXSec::~ReinDFRPXSec()
{

}
//____________________________________________________________________________
double ReinDFRPXSec::XSec(
        const Interaction * interaction, KinePhaseSpace_t kps) const
{
  if(! this -> ValidProcess    (interaction) ) return 0.;
  if(! this -> ValidKinematics (interaction) ) return 0.;

  const Kinematics &   kinematics = interaction -> Kine();
  const InitialState & init_state = interaction -> InitState();
  const Target &       target     = init_state.Tgt();

  double E      = init_state.ProbeE(kRfHitNucRest);  // neutrino energy
  double x      = kinematics.x();                    // bjorken x
  double y      = kinematics.y();                    // inelasticity y
  double Q2     = 2.*x*y*kNucleonMass*E;             // momentum transfer Q2>0
  double Gf     = kGF2 * kNucleonMass/(16*kPi3);     // GF/pi/etc factor
  double fp     = 0.93 * kPionMass;                  // pion decay constant
  double fp2    = TMath::Power(fp,2.);         
  double Epi    = y*E;                               // pion energy
  double ma2    = TMath::Power(fMa,2);
  double propg  = TMath::Power(ma2/(ma2+Q2),2.);     // propagator term
  double sTot   = utils::hadxs::TotalPionNucleonXSec(Epi); // tot. pi+N xsec
  double sTot2  = TMath::Power(sTot,2.);
  double b      = fBeta;

#ifdef __GENIE_LOW_LEVEL_MESG_ENABLED__
  LOG("ReinDFR", pDEBUG)
    << "E = " << E << ", x = " << x << ", y = " << y << ", Q2 = " << Q2;
  LOG("ReinDFR", pDEBUG)
    << "Epi = " << Epi << ", s^{piN}_{tot} = " << sTot;
#endif

  double xsec = Gf*E*fp2*(1-y)*propg*sTot2;


  //----- compute d^3sigma/dxdydt or d^2sigma/dxdy

  //if(kps==kPSxytfE) 
  //{
  //  double t = kinematics.t(); 
  //  xsec *= TMath::Exp(-b*t);
  //}

  if(kps==kPSxyfE) 
  {
    double tmin = TMath::Power(0.5*kPionMass2/Epi, 2.);
  //double tmax =  0.5;
    double tmax = 99.0;
    double t_integral = 0;
    if(tmin<tmax) {
       t_integral = (TMath::Exp(-b*tmin) - TMath::Exp(-b*tmax))/b;
    }
    xsec *= t_integral;
  }

  //----- Check whether variable tranformation is needed
  if(kps!=kPSxyfE) {
    double J = utils::kinematics::Jacobian(interaction,kPSxyfE,kps);
#ifdef __GENIE_LOW_LEVEL_MESG_ENABLED__
    LOG("ReinDFR", pDEBUG)
     << "Jacobian for transformation to: " 
                  << KinePhaseSpace::AsString(kps) << ", J = " << J;
#endif
    xsec *= J;
  }

  //----- if requested return the free nucleon xsec even for input nuclear tgt 
  if( interaction->TestBit(kIAssumeFreeNucleon) ) return xsec;

  //----- number of scattering centers in the target
  int nucpdgc = target.HitNucPdg();
  int NNucl = (pdg::IsProton(nucpdgc)) ? target.Z() : target.N(); 
  xsec *= NNucl; 

  return xsec;
}
//____________________________________________________________________________
double ReinDFRPXSec::Integral(const Interaction * interaction) const
{
  double ml = kMuonMass;

  const InitialState & init_state = interaction -> InitState();
  double E = init_state.ProbeE(kRfHitNucRest); 

  Range1D_t x(kASmallNum, 1.-kASmallNum);
  Range1D_t y(kPionMass/E + kASmallNum, 1.-ml/E - kASmallNum);

  if(y.max <= y.min) return 0;

  KinePhaseSpace_t kps = kPSxyfE;

  int nx=300;
  int ny=300;

  double dx = (x.max - x.min)/(nx-1);
  double dy = (y.max - y.min)/(ny-1);

  double xsec = 0;

  for(int ix=0; ix<nx; ix++) {
    double xc = x.min + ix*dx;
    for(int iy=0; iy<ny; iy++) {
      double yc = y.min + iy*dy;
      interaction->KinePtr()->Setx(xc);
      interaction->KinePtr()->Sety(yc);
      xsec += (dx*dy * this->XSec(interaction,kps));
    }
  }

  LOG("ReinDFR", pNOTICE)
    << "xsec (E = " << E << " GeV) = "
    << xsec/(1E-38*units::cm2) << "1E-38 * cm2";

  return xsec;
}
//____________________________________________________________________________
bool ReinDFRPXSec::ValidProcess(const Interaction * interaction) const
{
  if(interaction->TestBit(kISkipProcessChk)) return true;

  if(interaction->ProcInfo().IsDiffractive()) return true;
  return false;
}
//____________________________________________________________________________
bool ReinDFRPXSec::ValidKinematics(const Interaction* interaction) const
{
  if(interaction->TestBit(kISkipKinematicChk)) return true;
  return true;
}
//____________________________________________________________________________
void ReinDFRPXSec::Configure(const Registry & config)
{
  Algorithm::Configure(config);
  this->LoadConfig();
}
//____________________________________________________________________________
void ReinDFRPXSec::Configure(string config)
{
  Algorithm::Configure(config);
  this->LoadConfig();
}
//____________________________________________________________________________
void ReinDFRPXSec::LoadConfig(void)
{
  AlgConfigPool * confp = AlgConfigPool::Instance();
  const Registry * gc = confp->GlobalParameterList();

  fMa   = fConfig->GetDoubleDef("Ma",   gc->GetDouble("DFR-Ma"));
  fBeta = fConfig->GetDoubleDef("beta", gc->GetDouble("DFR-Beta"));
}
//____________________________________________________________________________

