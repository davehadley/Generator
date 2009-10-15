//____________________________________________________________________________
/*!

\program gvld_nuxsec_vs_world_data

\brief   Compares the GENIE neutrino cross sections against the world data.

         Syntax:
           gvld_nuxsec_vs_world_data [-h host] [-u user] [-p passwd] [-g files]

         Options:

           [] Denotes an optional argument.

           -h NuVld MySQL URL (eg mysql://localhost/NuScat).
           -u NuVld MySQL username.
           -p NuVld MySQL password.
           -g An XML file with GENIE inputs (cross sections and event  samples
              for decomposing the inclusive cross section to exclusive cross 
              sections). Multiple models can be included in that file, each 
              identified by a "name" (all model predictions will be overlayed).

              <?xml version="1.0" encoding="ISO-8859-1"?>
              <vld_inputs>
                 <model name="a_model_name">
                   <xsec_file>             /path/model_1/xsec.root     </xsec_file>
                   <evt_file format="gst"> /path/model_1/evtfile0.root </evt_file>
                   <evt_file format="gst"> /path/model_1/evtfile1.root </evt_file>
                   <evt_file format="gst"> /path/model_1/evtfile2.root </evt_file>
                   ...
                 </model>

                 <model name="another_model_name">
                   <xsec_file>             /path/model_2/xsec.root     </xsec_file>
                   <evt_file format="gst"> /path/model_2/evtfile0.root </evt_file>
                   <evt_file format="gst"> /path/model_2/evtfile1.root </evt_file>
                   <evt_file format="gst"> /path/model_2/evtfile2.root </evt_file>
                   ...
                 </model>
                 ...
              </vld_inputs>

         Notes:
           * The input ROOT cross section files are the ones generated by 
             GENIE's gspl2root utility. 
             See the GENIE User Manual for more details.
             It should contain at least the 
                - `nu_mu_n'
                - `nu_mu_H1'
                - `nu_mu_bar_n' 
                - `nu_mu_bar_H1' 
           * The input event files are `gst' summary ntuples generated by 
             GENIE gntpc utility.
             See the GENIE User Manual for more details.
             The files will be chained together. They should contain sufficient
             statistics of  nu_mu+n, nu_mu+p, nu_mu_bar+n, nu_mu_bar+p samples
             generated with an ~1/E energy spectrum over a large energy range 
             (eg 100 MeV - 120 GeV)

         Example:

           shell$ gvld_nuxsec_vs_world_data \
                      -h mysql://localhost/NuScat \
                      -u costas \
                      -p ^%@^!%@!*&@^
		      -g ./inputs.xml

\author  Costas Andreopoulos <costas.andreopoulos \at stfc.ac.uk>
         STFC, Rutherford Appleton Laboratory

\created June 06, 2008 

\cpright Copyright (c) 2003-2009, GENIE Neutrino MC Generator Collaboration
         For the full text of the license visit http://copyright.genie-mc.org
         or see $GENIE/LICENSE
*/
//____________________________________________________________________________

#include <cstdlib>
#include <cassert>
#include <sstream>
#include <string>

#include <TSystem.h>
#include <TFile.h>
#include <TDirectory.h>
#include <TGraph.h>
#include <TPostScript.h>
#include <TH1D.h>
#include <TMath.h>
#include <TCanvas.h>
#include <TPavesText.h>
#include <TText.h>
#include <TStyle.h>
#include <TLegend.h>
#include <TChain.h>

#include "Conventions/GBuild.h"
#include "Messenger/Messenger.h"
#include "PDG/PDGUtils.h"
#include "PDG/PDGCodes.h"
#include "Utils/CmdLineArgParserUtils.h"
#include "Utils/CmdLineArgParserException.h"
#include "Utils/StringUtils.h"
#include "Utils/VldTestInputs.h"
#include "ValidationTools/NuVld/DBI.h"
#include "ValidationTools/NuVld/DBStatus.h"

using std::ostringstream;
using std::string;

using namespace genie;
using namespace genie::nuvld;
using namespace genie::utils::vld;

/* 
..............................................................................
NEUTRINO CROSS SECTION DATA
..............................................................................
ID   DESCRIPTION
 0   nu_mu      CC QE     [all data]
 1   nu_mu      CC QE     [data on light targets]
 2   nu_mu      CC QE     [data on heavy targets]
 3   nu_mu_bar  CC QE     [all data]
 4   nu_mu_bar  CC QE     [data on light targets]
 5   nu_mu_bar  CC QE     [data on heavy targets]
 6   nu_mu      CC 1pi    [v + p -> mu- + p + pi+, all data]
 7   nu_mu      CC TOT    [E>10]
 8   nu_mu_bar  CC TOT    [E>10]
 9   nu_mu      CC 2pi    [v + n -> l + p + pi+ + pi-, all data]
10   nu_mu      CC 2pi    [v + p -> l + p + pi+ + pi0, all data]
11   nu_mu      CC 2pi    [v + p -> l + n + pi+ + pi+, all data]
12   numu       NC COH pi [A = 20] 
13   numu       CC COH pi [A = 20] 
14   nu_mu_bar  CC COH pi [A = 20] 
15   numu       NC COH pi [A = 27]
16   numu       NC COH pi [A = 30] 
17   numu       CC COH pi [A = 30] 
18   nu_mu_bar  CC COH pi [A = 30] 
..............................................................................
*/
const int    kNuXSecDataSets = 19;
const char * kNuXSecDataSetLabel[kNuXSecDataSets] = {
/* 0 */ "#nu_{#mu} CC QE [all data]          ",
/* 1 */ "#nu_{#mu} CC QE [light target data] ",
/* 2 */ "#nu_{#mu} CC QE [heavy target data] ",
/* 3 */ "#bar{#nu_{#mu}} CC QE [all data]          ",
/* 4 */ "#bar{#nu_{#mu}} CC QE [light target data] ",
/* 5 */ "#bar{#nu_{#mu}} CC QE [heavy target data] ",
/* 6 */ "#nu_{#mu} CC 1pi (#nu_{#mu} p -> #mu^{-} p #pi^{+}) ",
/* 7 */ "#nu_{#mu} CC TOT [E>10 GeV data]             ",
/* 8 */ "#bar{#nu_{#mu}} CC TOT [E>10 GeV data]             ",
/* 9 */ "#nu_{#mu} CC 2pi (#nu_{#mu} n -> #mu^{-} p #pi^{+} #pi^{-})",
/*10 */ "#nu_{#mu} CC 2pi (#nu_{#mu} p -> #mu^{-} p #pi^{+} #pi^{0})",
/*11 */ "#nu_{#mu} CC 2pi (#nu_{#mu} p -> #mu^{-} n #pi^{+} #pi^{+})",
/*12 */ "#nu_{#mu} NC COH pi (A = 20)", 
/*13 */ "#nu_{#mu} CC COH pi (A = 20)",
/*14 */ "#bar{#nu_{#mu}} CC COH pi (A = 20)",
/*15 */ "#nu_{#mu} NC COH pi (A = 27)",
/*16 */ "#nu_{#mu} NC COH pi (A = 30)",
/*17 */ "#nu_{#mu} CC COH pi (A = 30)",
/*18 */ "#bar{#nu_{#mu}} CC COH pi (A = 30)"
};
const char * kNuXSecKeyList[kNuXSecDataSets] = {
/* 0 */ "ANL_12FT,1;ANL_12FT,3;BEBC,12;BNL_7FT,3;FNAL_15FT,3;Gargamelle,2;SERP_A1,0;SERP_A1,1;SKAT,8",
/* 1 */ "ANL_12FT,1;ANL_12FT,3;BEBC,12;BNL_7FT,3;FNAL_15FT,3",
/* 2 */ "Gargamelle,2;SERP_A1,0;SERP_A1,1;SKAT,8",
/* 3 */ "BNL_7FT,2;Gargamelle,3;Gargamelle,5;SERP_A1,2;SKAT,9",
/* 4 */ "BNL_7FT,2",
/* 5 */ "Gargamelle,3;Gargamelle,5;SERP_A1,2;SKAT,9",
/* 6 */ "ANL_12FT,0;ANL_12FT,5;ANL_12FT,8;BEBC,4;BEBC,9;BEBC,13;BNL_7FT,5;FNAL_15FT,0;Gargamelle,4;SKAT,4;SKAT,5",
/* 7 */ "ANL_12FT,2;ANL_12FT,4;BEBC,0;BEBC,2;BEBC,5;BEBC,8;BNL_7FT,0;BNL_7FT,4;CCFR,2;CCFRR,0;CHARM,0;CHARM,4;FNAL_15FT,1;FNAL_15FT,2;Gargamelle,0;Gargamelle,10;Gargamelle,12;IHEP_ITEP,0;IHEP_ITEP,2;IHEP_JINR,0;SKAT,0",
/* 8 */ "BEBC,1;BEBC,3;BEBC,6;BEBC,7;BNL_7FT,1;CCFR,3;CHARM,1;CHARM,5;FNAL_15FT,4;FNAL_15FT,5;Gargamelle,1;Gargamelle,11;Gargamelle,13;IHEP_ITEP,1;IHEP_ITEP,3;IHEP_JINR,1",
/* 9 */ "ANL_12FT,11;BNL_7FT,8",
/*10 */ "ANL_12FT,12",
/*11 */ "ANL_12FT,13",
/*12 */ "CHARM,2",
/*13 */ "BEBC,11;CHARM,6;FNAL_15FT,8",
/*14 */ "BEBC,10;CHARM,7;FNAL_15FT,7",
/*15 */ "AachenPadova,0",
/*16 */ "Gargamelle,14;SKAT,3",
/*17 */ "SKAT,1",
/*18 */ "SKAT,2"
};
float kNuXSecERange[kNuXSecDataSets][2] = {
/* 0 */ { 0.1,  30.0},
/* 1 */ { 0.1,  30.0},
/* 2 */ { 0.1,  30.0},
/* 3 */ { 0.1,  30.0},
/* 4 */ { 0.1,  30.0},
/* 5 */ { 0.1,  30.0},
/* 6 */ { 0.1,  30.0},
/* 7 */ {10.0, 120.0},
/* 8 */ {10.0, 120.0},
/* 9 */ { 1.0, 120.0},
/*10 */ { 1.0, 120.0},
/*11 */ { 1.0, 120.0},
/*12 */ { 1.0, 150.0},
/*13 */ { 1.0, 150.0},
/*14 */ { 1.0, 150.0},
/*15 */ { 1.0, 150.0},
/*16 */ { 1.0, 150.0},
/*17 */ { 1.0, 150.0},
/*18 */ { 1.0, 150.0}
};
int kNuXSecLogXY[kNuXSecDataSets][2] = {
/* 0 */ { 1, 1 },
/* 1 */ { 1, 1 },
/* 2 */ { 1, 1 },
/* 3 */ { 1, 1 },
/* 4 */ { 1, 1 },
/* 5 */ { 1, 1 },
/* 6 */ { 1, 1 },
/* 7 */ { 1, 1 },
/* 8 */ { 1, 1 },
/* 9 */ { 1, 1 },
/*10 */ { 1, 1 },
/*11 */ { 1, 1 },
/*12 */ { 0, 0 },
/*13 */ { 0, 0 },
/*14 */ { 0, 0 },
/*15 */ { 0, 0 },
/*16 */ { 0, 0 },
/*17 */ { 0, 0 },
/*18 */ { 0, 0 }
};

typedef DBQueryString             DBQ;
typedef DBTable<DBNuXSecTableRow> DBT;

// function prototypes
void     Init               (void);
void     Plot               (void);
void     End                (void);
void     AddCoverPage       (void);
bool     Connect            (void);
DBQ      FormQuery          (const char * key_list, float emin,  float emax);
DBT *    Data               (int iset);
TGraph * Model              (int iset, int imodel);
void     Draw               (int iset);
TH1F *   DrawFrame          (TGraph * gr0, TGraph * gr1);
TH1F *   DrawFrame          (double xmin, double xmax, double ymin, double yman);
void     Format             (TGraph* gr, int lcol, int lsty, int lwid, int mcol, int msty, double msiz);
void     GetCommandLineArgs (int argc, char ** argv);
void     PrintSyntax        (void);

// command-line arguments
string         gOptDbURL;
string         gOptDbUser;
string         gOptDbPasswd;
VldTestInputs  gOptGenieInputs;

// dbase information
const char * kDefDbURL = "mysql://localhost/NuScat";  

// globals
bool            gCmpWithData  = true;
DBI *           gDBI          = 0;
TPostScript *   gPS           = 0;
TCanvas *       gC            = 0;
TLegend *       gLS           = 0;

// model line styles
const int kNMaxNumModels = 5;
const int kLStyle    [kNMaxNumModels] = { 
   1,       2,        3,        5,            6 
}; 
string    kLStyleTxt [kNMaxNumModels] = { 
  "solid", "dashed", "dotted", "dot-dashed", "dot-dot-dashed" 
};

//_________________________________________________________________________________
int main(int argc, char ** argv)
{
  GetCommandLineArgs (argc,argv);

  Init();
  Plot();
  End();

  LOG("gvldtest", pINFO)  << "Done!";
  return 0;
}
//_________________________________________________________________________________
void Plot(void)
{
#ifdef __GENIE_MYSQL_ENABLED__

  // connect to the NuValidator MySQL dbase
  bool ok = Connect();
  if(!ok) {
    return;
  }
 
  // loop over data sets
  for(int iset = 0; iset < kNuXSecDataSets; iset++) 
  {
    Draw(iset);
  }
#endif
}
//_________________________________________________________________________________
void Init(void)
{
  LOG("vldtest", pNOTICE) << "Initializing...";;

  gC = new TCanvas("c","",20,20,500,650);
  gC->SetBorderMode(0);
  gC->SetFillColor(0);
  gC->SetGridx();
  gC->SetGridy();

  gLS = new TLegend(0.15,0.92,0.85,0.98);
  gLS -> SetFillColor(0);
  gLS -> SetBorderSize(1);

  // output file
  gPS = new TPostScript("genie_nuxec_vs_data.ps", 111);

  AddCoverPage();

  gC->SetLogx();
  gC->SetLogy();
}
//_________________________________________________________________________________
void AddCoverPage(void)
{
  // header
  gPS->NewPage();
  gC->Range(0,0,100,100);
  TPavesText hdr(10,40,90,70,3,"tr");
  hdr.AddText(" ");
  hdr.AddText("GENIE Neutrino Cross Section Comparisons with World Data");
  hdr.AddText(" ");
  hdr.AddText(" ");
  for(int imodel=0; imodel< gOptGenieInputs.NModels(); imodel++) {
    ostringstream stream;
    stream << "model tag: " << gOptGenieInputs.ModelTag(imodel)
           << " (" << kLStyleTxt[imodel] << " line)";
    hdr.AddText(stream.str().c_str());
  }
  hdr.AddText(" ");
  hdr.Draw();
  gC->Update();
}
//_________________________________________________________________________________
void End(void)
{
  LOG("vldtest", pNOTICE) << "Cleaning up...";

  gPS->Close();

  delete gC;
  delete gLS;
  delete gPS;
}
//_________________________________________________________________________________
// Corresponding GENIE prediction for the `iset' data set 
//.................................................................................
TGraph * Model(int iset, int imodel)
{
  LOG("vldtest", pNOTICE) 
    << "Getting GENIE prediction (model ID = " 
    << imodel << ", data set ID = " << iset << ")";

  TFile * xsec_file = gOptGenieInputs.XSecFile(imodel);
  if(!xsec_file) {
     LOG("vldtest", pNOTICE) 
        << "No corresponding cross section file";
     return 0;
  }

  TChain * event_chain = gOptGenieInputs.EvtChain(imodel);
  if(!event_chain) {
     LOG("vldtest", pNOTICE) 
        << "No corresponding event chain.";
  }

  switch(iset) {

    // nu_mu CC QE 
    case (0) :
    case (1) :
    case (2) :
    {
       TDirectory * dir  = (TDirectory *) xsec_file->Get("nu_mu_n");
       if(!dir) return 0;
       TGraph * model = (TGraph*) dir->Get("qel_cc_n");
       return model;
       break;
    }

    // nu_mu_bar CC QE   
    case (3) :
    case (4) :
    case (5) :
    {
       TDirectory * dir  = (TDirectory *)xsec_file->Get("nu_mu_bar_H1");
       if(!dir) return 0;
       TGraph * model = (TGraph*) dir->Get("qel_cc_p");
       return model;
       break;
    }

    // nu_mu CC 1pi [v + p -> mu- + p + pi+]
    case (6) :
    {
      LOG("vldtest", pNOTICE)     << "Getting GENIE nu_mu CC 1pi [v + p -> mu- + p + pi+] prediction" ;

      TDirectory * dir  = (TDirectory *) xsec_file->Get("nu_mu_H1");
      if(!dir) return 0;
      TGraph * tot_cc = (TGraph*) dir->Get("tot_cc");
      if(!tot_cc) return 0;

      if(!event_chain) return 0;
      const int n = 100;
      double emin = 0.010;
      double emax = 100.0;
      TH1D * hcc    = new TH1D("hcc",   "",n, TMath::Log10(emin), TMath::Log10(emax)); // log binning
      TH1D * hcc1pi = new TH1D("hcc1pi","",n, TMath::Log10(emin), TMath::Log10(emax));
      event_chain->Draw("log10(Ev)>>hcc",    "cc&&neu==14&&Z==1&&A==1", "goff");
      event_chain->Draw("log10(Ev)>>hcc1pi", "cc&&neu==14&&Z==1&&A==1&&nfpim==0&&nfpi0==0&&nfpip==1&&nfp==1&&nfn==0", "goff");
      hcc1pi->Divide(hcc);

      double e[n], sig[n];
      for(int i = 0; i < hcc1pi->GetNbinsX(); i++) {
         int ibin = i+1;
         e  [i] = TMath::Power(10., hcc1pi->GetBinCenter(ibin));
         sig[i] = hcc1pi->GetBinContent(ibin) * tot_cc->Eval(e[i]);

         LOG("vldtest", pNOTICE) << "E = " << e[i] << "GeV , sig = " << sig[i] << " x1E-38 cm^2";

      }
      TGraph * model = new TGraph(n,e,sig);
      return model;
      break;
    }

    // nu_mu CC TOT (isoscalar target)
    case (7) :
    {
       TDirectory * dir_n  = (TDirectory *) xsec_file->Get("nu_mu_n");
       if(!dir_n) return 0;
       TGraph * model_n = (TGraph*) dir_n->Get("tot_cc_n");
       TDirectory * dir_p  = (TDirectory *) xsec_file->Get("nu_mu_H1");
       if(!dir_p) return 0;
       TGraph * model_p = (TGraph*) dir_p->Get("tot_cc_p");
       const int n = 1000;
       double e[n], sig[n];
       for(int i=0; i<n; i++) {
         e  [i] = 5 + i*0.1;
         sig[i] = 0.5*(model_n->Eval(e[i]) + model_p->Eval(e[i]));
       }
       TGraph * model = new TGraph(n,e,sig);
       return model;
       break;
    }

    // nu_mu_bar CC TOT (isoscalar target)
    case (8) :
    {
       TDirectory * dir_n  = (TDirectory *) xsec_file->Get("nu_mu_bar_n");
       if(!dir_n) return 0;
       TGraph * model_n = (TGraph*) dir_n->Get("tot_cc_n");
       TDirectory * dir_p  = (TDirectory *) xsec_file->Get("nu_mu_bar_H1");
       if(!dir_p) return 0;
       TGraph * model_p = (TGraph*) dir_p->Get("tot_cc_p");
       const int n = 1000;
       double e[n], sig[n];
       for(int i=0; i<n; i++) {
         e  [i] = 5 + i*0.1;
         sig[i] = 0.5*(model_n->Eval(e[i]) + model_p->Eval(e[i]));
       }
       TGraph * model = new TGraph(n,e,sig);
       return model;
       break;
    }

    // nu_mu CC 2pi [v + n -> l + p + pi+ + pi-]
    case (9) :
    {
      LOG("vldtest", pNOTICE) << "Getting GENIE nu_mu CC 2pi [v + n -> l + p + pi+ + pi-] prediction" ;

      TDirectory * dir  = (TDirectory *) xsec_file->Get("nu_mu_n");
      if(!dir) return 0;
      TGraph * tot_cc = (TGraph*) dir->Get("tot_cc");
      if(!tot_cc) return 0;

      if(!event_chain) return 0;
      const int n = 100;
      double emin = 0.010;
      double emax = 100.0;
      TH1D * hcc    = new TH1D("hcc",   "",n, TMath::Log10(emin), TMath::Log10(emax)); // log binning
      TH1D * hcc2pi = new TH1D("hcc2pi","",n, TMath::Log10(emin), TMath::Log10(emax));
      event_chain->Draw("log10(Ev)>>hcc",    "cc&&neu==14&&Z==1&&A==1", "goff");
      event_chain->Draw("log10(Ev)>>hcc2pi", "cc&&neu==14&&Z==1&&A==1&&nfpim==1&&nfpi0==0&&nfpip==1&&nfp==1&&nfn==0", "goff");
      hcc2pi->Divide(hcc);

      double e[n], sig[n];
      for(int i = 0; i < hcc2pi->GetNbinsX(); i++) {
         int ibin = i+1;
         e  [i] = TMath::Power(10., hcc2pi->GetBinCenter(ibin));
         sig[i] = hcc2pi->GetBinContent(ibin) * tot_cc->Eval(e[i]);

         LOG("vldtest", pNOTICE) << "E = " << e[i] << "GeV , sig = " << sig[i] << " x1E-38 cm^2";

      }
      TGraph * model = new TGraph(n,e,sig);
      return model;
      break;
    }

    // nu_mu CC 2pi [v + p -> l + p + pi+ + pi0]
    case (10) :
    {
      LOG("vldtest", pNOTICE) << "Getting GENIE nu_mu CC 2pi [v + p -> l + p + pi+ + pi0] prediction" ;

      TDirectory * dir  = (TDirectory *) xsec_file->Get("nu_mu_H1");
      if(!dir) return 0;
      TGraph * tot_cc = (TGraph*) dir->Get("tot_cc");
      if(!tot_cc) return 0;

      if(!event_chain) return 0;
      const int n = 100;
      double emin = 0.010;
      double emax = 100.0;
      TH1D * hcc    = new TH1D("hcc",   "",n, TMath::Log10(emin), TMath::Log10(emax)); // log binning
      TH1D * hcc2pi = new TH1D("hcc2pi","",n, TMath::Log10(emin), TMath::Log10(emax));
      event_chain->Draw("log10(Ev)>>hcc",    "cc&&neu==14&&Z==1&&A==1", "goff");
      event_chain->Draw("log10(Ev)>>hcc2pi", "cc&&neu==14&&Z==1&&A==1&&nfpim==0&&nfpi0==1&&nfpip==1&&nfp==1&&nfn==0", "goff");
      hcc2pi->Divide(hcc);

      double e[n], sig[n];
      for(int i = 0; i < hcc2pi->GetNbinsX(); i++) {
         int ibin = i+1;
         e  [i] = TMath::Power(10., hcc2pi->GetBinCenter(ibin));
         sig[i] = hcc2pi->GetBinContent(ibin) * tot_cc->Eval(e[i]);

         LOG("vldtest", pNOTICE) << "E = " << e[i] << "GeV , sig = " << sig[i] << " x1E-38 cm^2";

      }
      TGraph * model = new TGraph(n,e,sig);
      return model;
      break;
    }

    // nu_mu CC 2pi [v + p -> l + n + pi+ + pi+]
    case (11) :
    {
      LOG("vldtest", pNOTICE) << "Getting GENIE nu_mu CC 2pi [v + p -> l + n + pi+ + pi+] prediction" ;

      TDirectory * dir  = (TDirectory *) xsec_file->Get("nu_mu_H1");
      if(!dir) return 0;
      TGraph * tot_cc = (TGraph*) dir->Get("tot_cc");
      if(!tot_cc) return 0;

      if(!event_chain) return 0;
      const int n = 100;
      double emin = 0.010;
      double emax = 100.0;
      TH1D * hcc    = new TH1D("hcc",   "",n, TMath::Log10(emin), TMath::Log10(emax)); // log binning
      TH1D * hcc2pi = new TH1D("hcc2pi","",n, TMath::Log10(emin), TMath::Log10(emax));
      event_chain->Draw("log10(Ev)>>hcc",    "cc&&neu==14&&Z==1&&A==1", "goff");
      event_chain->Draw("log10(Ev)>>hcc2pi", "cc&&neu==14&&Z==1&&A==1&&nfpim==0&&nfpi0==0&&nfpip==2&&nfp==0&&nfn==1", "goff");
      hcc2pi->Divide(hcc);

      double e[n], sig[n];
      for(int i = 0; i < hcc2pi->GetNbinsX(); i++) {
         int ibin = i+1;
         e  [i] = TMath::Power(10., hcc2pi->GetBinCenter(ibin));
         sig[i] = hcc2pi->GetBinContent(ibin) * tot_cc->Eval(e[i]);

         LOG("vldtest", pNOTICE) << "E = " << e[i] << "GeV , sig = " << sig[i] << " x1E-38 cm^2";

      }
      TGraph * model = new TGraph(n,e,sig);
      return model;
      break;
    }

    // numu NC COH pi [A = 20] 
    case (12) :
    {
       TDirectory * dir  = (TDirectory *)xsec_file->Get("nu_mu_Ne20");
       if(!dir) return 0;
       TGraph * model = (TGraph*) dir->Get("coh_nc");
       return model;
       break;
    }

    // numu CC COH pi [A = 20] 
    case (13) :
    {
       TDirectory * dir  = (TDirectory *)xsec_file->Get("nu_mu_Ne20");
       if(!dir) return 0;
       TGraph * model = (TGraph*) dir->Get("coh_cc");
       return model;
       break;
    }

    // nu_mu_bar  CC COH pi [A = 20] 
    case (14) :
    {
       TDirectory * dir  = (TDirectory *)xsec_file->Get("nu_mu_bar_Ne20");
       if(!dir) return 0;
       TGraph * model = (TGraph*) dir->Get("coh_cc");
       return model;
       break;
    }

    // numu NC COH pi [A = 27]
    case (15) :
    {
       TDirectory * dir  = (TDirectory *)xsec_file->Get("nu_mu_Al27");
       if(!dir) return 0;
       TGraph * model = (TGraph*) dir->Get("coh_nc");
       return model;
       break;
    }

    // numu NC COH pi [A = 30] 
    case (16) :
    {
       TDirectory * dir  = (TDirectory *)xsec_file->Get("nu_mu_Si30");
       if(!dir) return 0;
       TGraph * model = (TGraph*) dir->Get("coh_nc");
       return model;
       break;
    }

    // numu CC COH pi [A = 30] 
    case (17) :
    {
       TDirectory * dir  = (TDirectory *)xsec_file->Get("nu_mu_Si30");
       if(!dir) return 0;
       TGraph * model = (TGraph*) dir->Get("coh_cc");
       return model;
       break;
    }

    // nu_mu_bar CC COH pi [A = 30] 
    case (18) :
    {
       TDirectory * dir  = (TDirectory *)xsec_file->Get("nu_mu_bar_Si30");
       if(!dir) return 0;
       TGraph * model = (TGraph*) dir->Get("coh_cc");
       return model;
       break;
    }

    default:
    {
       break;
    }
  }
  return 0;
}
//_________________________________________________________________________________
// Download cross section data from NuVld MySQL dbase 
//.................................................................................
bool Connect(void)
{
  if(!gCmpWithData) return true;

  // Get a data-base interface
  TSQLServer * sql_server = TSQLServer::Connect(
      gOptDbURL.c_str(),gOptDbUser.c_str(),gOptDbPasswd.c_str());

  if(!sql_server) return false;
  if(!sql_server->IsConnected()) return false;

  gDBI = new DBI(sql_server);
  return true;
}
//_________________________________________________________________________________
DBQ FormQuery(const char * key_list, float emin, float emax)
{
// forms a DBQueryString for extracting neutrino cross section data from the input 
// key-list and for the input energy range
//  
  ostringstream query_string;
  
  query_string 
    << "KEY-LIST:" << key_list
    << "$CUTS:Emin=" << emin << ";Emax=" << emax << "$DRAW_OPT:none$DB-TYPE:vN-XSec";
  
  DBQ query(query_string.str());
  
  return query;
}
//_________________________________________________________________________________
DBT * Data(int iset)
{
  if(!gCmpWithData) return 0;

  DBT * dbtable = new DBT;

  const char * keylist = kNuXSecKeyList[iset];
  float        e_min   = kNuXSecERange[iset][0];
  float        e_max   = kNuXSecERange[iset][1];

  DBQ query = FormQuery(keylist, e_min, e_max);
  assert( gDBI->FillTable(dbtable, query) == eDbu_OK );

  return dbtable;
}
//_________________________________________________________________________________
void Draw(int iset)
{
  // get all measurements for the current channel from the NuValidator MySQL dbase
  DBT * dbtable = Data(iset);

  // get the corresponding GENIE model prediction
  vector<TGraph*> models;
  for(int imodel=0; imodel< gOptGenieInputs.NModels(); imodel++) {
       models.push_back(Model(iset,imodel));
  }

  if(models.size()==0 && !dbtable) return;

  gPS->NewPage();

  gC->Clear();
  gC->Divide(2,1);
  gC->GetPad(1)->SetPad("mplots_pad","",0.01,0.25,0.99,0.99);
  gC->GetPad(2)->SetPad("legend_pad","",0.01,0.01,0.99,0.24);
  gC->GetPad(1)->SetFillColor(0);
  gC->GetPad(1)->SetBorderMode(0);
  gC->GetPad(2)->SetFillColor(0);
  gC->GetPad(2)->SetBorderMode(0);
  gC->GetPad(1)->cd();
  gC->GetPad(1)->SetBorderMode(0);
  gC->GetPad(1)->SetLogx( kNuXSecLogXY[iset][0] );
  gC->GetPad(1)->SetLogy( kNuXSecLogXY[iset][1] );

  gLS->SetHeader(kNuXSecDataSetLabel[iset]);

  TLegend * legend = new TLegend(0.01, 0.01, 0.99, 0.99);
  legend->SetFillColor(0);
  legend->SetTextSize(0.08);

  TH1F * hframe = 0;
  bool have_frame = false;

  // have data points to plot?
  if(dbtable) {
    TGraphAsymmErrors * graph = dbtable->GetGraph("all-noE");

    // create frame from the data point range
    double xmin  = ( graph->GetX() )[TMath::LocMin(graph->GetN(),graph->GetX())];
    double xmax  = ( graph->GetX() )[TMath::LocMax(graph->GetN(),graph->GetX())];
    double ymin  = ( graph->GetY() )[TMath::LocMin(graph->GetN(),graph->GetY())];
    double ymax  = ( graph->GetY() )[TMath::LocMax(graph->GetN(),graph->GetY())];
    hframe = (TH1F*) gC->GetPad(1)->DrawFrame(0.5*xmin, 0.4*ymin, 1.2*xmax, 2.0*ymax);
    hframe->Draw();
    have_frame = true;

    //
    // draw current data set
    //
    MultiGraph * mgraph = dbtable->GetMultiGraph("all-noE");
    for(unsigned int igraph = 0; igraph < mgraph->NGraphs(); igraph++) {
       mgraph->GetGraph(igraph)->Draw("P");
    }
    mgraph->FillLegend("LP", legend);
  }//dbtable?

  // have model prediction to plot?
  if(models.size()>0) {
     if(!have_frame) {
        // the data points have not been plotted
        // create a frame from this graph range
        double xmin  = ( models[0]->GetX() )[TMath::LocMin(models[0]->GetN(),models[0]->GetX())];
        double xmax  = ( models[0]->GetX() )[TMath::LocMax(models[0]->GetN(),models[0]->GetX())];
        double ymin  = ( models[0]->GetY() )[TMath::LocMin(models[0]->GetN(),models[0]->GetY())];
        double ymax  = ( models[0]->GetY() )[TMath::LocMax(models[0]->GetN(),models[0]->GetY())];
        hframe = (TH1F*) gC->GetPad(1)->DrawFrame(0.5*xmin, 0.4*ymin, 1.2*xmax, 2.0*ymax);
        hframe->Draw();
     }
     for(int imodel=0; imodel<gOptGenieInputs.NModels(); imodel++) {
       TGraph * plot = models[imodel];
       if(plot) {
         int lsty = kLStyle[imodel];     
         Format(plot,1,lsty,2,1,1,1);
         plot->Draw("L");
       }
     }
  }//model?

  hframe->GetXaxis()->SetTitle("E_{#nu} (GeV)");
  hframe->GetYaxis()->SetTitle("#sigma_{#nu} (1E-38 cm^{2})");

  gLS->Draw();

  gC->GetPad(2)->cd();
  legend->Draw();

  gC->GetPad(2)->Update();
  gC->Update();

  if(dbtable) {
    delete dbtable;
  }
}
//_________________________________________________________________________________
// Formatting
//.................................................................................
TH1F* DrawFrame(TGraph * gr0, TGraph * gr1)
{
  double xmin = 1E-5;
  double xmax = 1;
  double ymin = 1E-5;
  double ymax = 1;

  if(gr0) {  
    TAxis * x0 = gr0 -> GetXaxis();
    TAxis * y0 = gr0 -> GetYaxis();
    xmin = x0 -> GetXmin();
    xmax = x0 -> GetXmax();
    ymin = y0 -> GetXmin();
    ymax = y0 -> GetXmax();
  }
  if(gr1) {
     TAxis * x1 = gr1 -> GetXaxis();
     TAxis * y1 = gr1 -> GetYaxis();
     xmin = TMath::Min(xmin, x1 -> GetXmin());
     xmax = TMath::Max(xmax, x1 -> GetXmax());
     ymin = TMath::Min(ymin, y1 -> GetXmin());
     ymax = TMath::Max(ymax, y1 -> GetXmax());
  }
  xmin *= 0.5;
  xmax *= 1.5;
  ymin *= 0.5;
  ymax *= 1.5;
  xmin = TMath::Max(0.1, xmin);
  
  return DrawFrame(xmin, xmax, ymin, ymax);
}
//_________________________________________________________________________________
TH1F* DrawFrame(double xmin, double xmax, double ymin, double ymax)
{
  TH1F * hf = (TH1F*) gC->DrawFrame(xmin, ymin, xmax, ymax);
  hf->GetXaxis()->SetTitle("E (GeV)");
  hf->GetYaxis()->SetTitle("#sigma (10^{-38} cm^{2})");
  hf->GetYaxis()->SetTitleSize(0.03);
  hf->GetYaxis()->SetTitleOffset(1.3);
  hf->GetXaxis()->SetLabelSize(0.03);
  hf->GetYaxis()->SetLabelSize(0.03);
  return hf;
}
//_________________________________________________________________________________
void Format(
    TGraph* gr, int lcol, int lsty, int lwid, int mcol, int msty, double msiz)
{
  if(!gr) return;

  if (lcol >= 0) gr -> SetLineColor   (lcol);
  if (lsty >= 0) gr -> SetLineStyle   (lsty);
  if (lwid >= 0) gr -> SetLineWidth   (lwid);

  if (mcol >= 0) gr -> SetMarkerColor (mcol);
  if (msty >= 0) gr -> SetMarkerStyle (msty);
  if (msiz >= 0) gr -> SetMarkerSize  (msiz);
}
//_________________________________________________________________________________
// Parsing command-line arguments, check/form filenames, etc
//.................................................................................
void GetCommandLineArgs(int argc, char ** argv)
{
  LOG("gvldtest", pNOTICE) << "*** Parsing command line arguments";

  // get GENIE inputs
  try {
     string inputs = utils::clap::CmdLineArgAsString(argc,argv,'g');
     bool ok = gOptGenieInputs.LoadFromFile(inputs);
     if(!ok) {
        LOG("gvldtest", pFATAL) << "Could not read: " << inputs;
        exit(1);
     }
  } catch(exceptions::CmdLineArgParserException e) {
     if(!e.ArgumentFound()) {
     }
  }

  gCmpWithData = true;

  // get DB URL
  try {
     gOptDbURL = utils::clap::CmdLineArgAsString(argc,argv,'h');
  } catch(exceptions::CmdLineArgParserException e) {
     if(!e.ArgumentFound()) {
       gOptDbURL = kDefDbURL;
     }
  }

  // get DB username
  try {
     gOptDbUser = utils::clap::CmdLineArgAsString(argc,argv,'u');
  } catch(exceptions::CmdLineArgParserException e) {
     if(!e.ArgumentFound()) {
       gCmpWithData = false;
     }
  }

  // get DB passwd
  try {
     gOptDbPasswd = utils::clap::CmdLineArgAsString(argc,argv,'p');
  } catch(exceptions::CmdLineArgParserException e) {
     if(!e.ArgumentFound()) {
       gCmpWithData = false;
     }
  }

}
//_________________________________________________________________________________
void PrintSyntax(void)
{
  LOG("gvldtest", pNOTICE)
    << "\n\n" << "Syntax:" << "\n"
    << "   gvld_nuxsec_vs_world_data [-h host] [-u user] [-p passwd] -f files\n";
}
//_________________________________________________________________________________

