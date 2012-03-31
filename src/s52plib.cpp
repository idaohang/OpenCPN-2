/******************************************************************************
 *
 * Project:  OpenCPN
 * Purpose:  S52 Presentation Library
 * Author:   David Register
 *
 ***************************************************************************
 *   Copyright (C) 2010 by David S. Register                               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,  USA.             *
 ***************************************************************************
 *
 *
 */


// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#ifndef  WX_PRECOMP
#include "wx/wx.h"
#endif //precompiled headers

#include <math.h>

#include "dychart.h"

#include "georef.h"

#include "s52plib.h"
#include "s57chart.h"                   // for back function references
#include "mygeom.h"
#include "cutil.h"
#include "s52utils.h"
#include "navutil.h"                    // for LogMessageOnce()
#include "ocpn_pixel.h"

#include <stdlib.h>                             // 261, min() becomes __min(), etc.

#include "wx/image.h"                   // Missing from wxprec.h
#include "wx/tokenzr.h"

#ifdef __MSVC__
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#define DEBUG_NEW new(_NORMAL_BLOCK, __FILE__, __LINE__ )
#define new DEBUG_NEW
#endif

extern s52plib          *ps52plib;

extern bool             g_b_useStencil;
extern wxString        *g_pcsv_locn;

void DrawWuLine ( wxDC *pDC, int X0, int Y0, int X1, int Y1, wxColour clrLine, int dash, int space );
extern bool GetDoubleAttr ( S57Obj *obj, const char *AttrName, double &val );


//    Implement the Bounding Box list
#include <wx/listimpl.cpp>
WX_DEFINE_LIST ( ObjList );


//    Testing
/*
typedef struct
{
        char           colorname[6];
        unsigned char  R;
        unsigned char  G;
        unsigned char  B;
} color_sub;


color_sub color_adjust[] =
{
        {"nimes", 5,5,5},
        {"ddfkl", 4,4,4}
};
*/



//-----------------------------------------------------------------------------
//      s52plib implementation
//-----------------------------------------------------------------------------
s52plib::s52plib ( const wxString& PLib )
{
      m_plib_file = PLib;

//      Set up some buffers, etc...
      pBuf = buffer;

      pOBJLArray = new wxArrayPtrVoid;

      ColorTableArray = NULL;
      ColourHashTableArray = NULL;

      lineLUPArray = NULL;            // lines
      areaPlaineLUPArray = NULL;      // areas: PLAIN_BOUNDARIES
      areaSymbolLUPArray = NULL;      // areas: SYMBOLIZED_BOUNDARIE
      pointSimplLUPArray = NULL;      // points: SIMPLIFIED
      pointPaperLUPArray = NULL;      // points: PAPER_CHART
      condSymbolLUPArray = NULL;      // Dynamic Conditional Symbology

      _symb_sym = NULL;

      m_txf_ready = false;
      m_txf = NULL;

      m_bOK = !(S52_load_Plib ( PLib ) == 0);

      m_bShowS57Text = false;
      m_bShowS57ImportantTextOnly = false;
      m_colortable_index = 0;

      _symb_symR = NULL;
      bUseRasterSym = false;


      //      Sensible defaults
      m_nSymbolStyle = PAPER_CHART;
      m_nBoundaryStyle = PLAIN_BOUNDARIES;
      m_nDisplayCategory = OTHER;
      m_nDepthUnitDisplay = 1;                // metres

      UpdateMarinerParams();

      ledge = new int[2000];
      redge = new int[2000];

      //    Defaults
      m_VersionMajor = 3;
      m_VersionMinor = 2;

      //    Compute display scale factor
      int mmx, mmy;
      wxDisplaySizeMM ( &mmx, &mmy );
      int sx, sy;
      wxDisplaySize ( &sx, &sy );

      m_display_pix_per_mm = ( ( double ) sx ) / ( ( double ) mmx );

      //        Set up some default flags
      m_bDeClutterText = false;
      m_bShowAtonText = true;


}




s52plib::~s52plib()
{
      if ( m_bOK )
            S52_flush_Plib();

//      Free the OBJL Array Elements
      for ( unsigned int iPtr = 0 ; iPtr < pOBJLArray->GetCount() ; iPtr++ )
            free ( pOBJLArray->Item ( iPtr ) );

      delete pOBJLArray;


      delete[] ledge;
      delete[] redge;

      if(m_txf)
            txfUnloadFont(m_txf);

}


void s52plib::PrepareTxfRenderer(void)
{
      wxFileName txf_file(m_plib_file);
      txf_file.SetFullName(_T("Helvetica.txf"));

      m_txf_ready = false;

      m_txf = txfLoadFont((char *)(const char *)txf_file.GetFullPath().mb_str());
      if (m_txf == NULL)
      {
            wxString msg(_T("    Problem loading OpenGL Font Texture File "));
            msg.Append(txf_file.GetFullPath());
            msg.Append(_T("..."));
            msg.Append(wxString(txfErrorString(), wxConvUTF8));
            wxLogMessage(msg);
      }
      else
      {
            m_txf->texobj = 0;
            txfEstablishTexture(m_txf, 0, GL_TRUE);
            m_txf_ready = true;

            //    Get the width of one typical character, to build scale factors
            int w, h, descent;
            txfGetStringMetrics(m_txf, (char *)"W", 1, &w, &h, &descent);
            m_txf_avg_char_width = w;
            m_txf_avg_char_height = h;

      }
 }

/*
    Update the S52 Conditional Symbology Parameter Set to reflect the
    current state of the library member options.
*/

void s52plib::UpdateMarinerParams ( void )
{

      //      Symbol Style
      if ( SIMPLIFIED == m_nSymbolStyle )
            S52_setMarinerParam ( S52_MAR_SYMPLIFIED_PNT, 1.0 );
      else
            S52_setMarinerParam ( S52_MAR_SYMPLIFIED_PNT, 0.0 );

      //      Boundary Style
      if ( SYMBOLIZED_BOUNDARIES == m_nBoundaryStyle )
            S52_setMarinerParam ( S52_MAR_SYMBOLIZED_BND, 1.0 );
      else
            S52_setMarinerParam ( S52_MAR_SYMBOLIZED_BND, 0.0 );

}

void s52plib::GenerateStateHash()
{
      m_state_hash = ::wxGetUTCTime();
}






//------------------------
//
//      MODULES LINKING SECTION
//
//------------------------

/*  CIE->RGB color transformation matrix courtesy of......   */

/*
            The BREP Library.
            Copyright (C) 1996 Philippe Bekaert
*/



#define  CIE_x_r                0.640            // nominal CRT primaries
#define  CIE_y_r                0.330
#define  CIE_x_g                0.290
#define  CIE_y_g                0.600
#define  CIE_x_b                0.150
#define  CIE_y_b                0.060
#define  CIE_x_w                0.295 //0.3333333333          // monitor white point
#define  CIE_y_w                0.315// 0.3333333333




#define CIE_D           (       CIE_x_r*(CIE_y_g - CIE_y_b) + \
            CIE_x_g*(CIE_y_b - CIE_y_r) + \
            CIE_x_b*(CIE_y_r - CIE_y_g)     )
#define CIE_C_rD        ( (1./CIE_y_w) * \
            ( CIE_x_w*(CIE_y_g - CIE_y_b) - \
            CIE_y_w*(CIE_x_g - CIE_x_b) + \
            CIE_x_g*CIE_y_b - CIE_x_b*CIE_y_g     ) )
#define CIE_C_gD        ( (1./CIE_y_w) * \
            ( CIE_x_w*(CIE_y_b - CIE_y_r) - \
            CIE_y_w*(CIE_x_b - CIE_x_r) - \
            CIE_x_r*CIE_y_b + CIE_x_b*CIE_y_r     ) )
#define CIE_C_bD        ( (1./CIE_y_w) * \
            ( CIE_x_w*(CIE_y_r - CIE_y_g) - \
            CIE_y_w*(CIE_x_r - CIE_x_g) + \
            CIE_x_r*CIE_y_g - CIE_x_g*CIE_y_r     ) )

#define CIE_rf          (CIE_y_r*CIE_C_rD/CIE_D)
#define CIE_gf          (CIE_y_g*CIE_C_gD/CIE_D)
#define CIE_bf          (CIE_y_b*CIE_C_bD/CIE_D)

static double tmat[3][3] =       //XYZ to RGB
{
      { ( CIE_y_g - CIE_y_b - CIE_x_b*CIE_y_g + CIE_y_b*CIE_x_g ) /CIE_C_rD,
            ( CIE_x_b - CIE_x_g - CIE_x_b*CIE_y_g + CIE_x_g*CIE_y_b ) /CIE_C_rD,
            ( CIE_x_g*CIE_y_b - CIE_x_b*CIE_y_g ) /CIE_C_rD},
      { ( CIE_y_b - CIE_y_r - CIE_y_b*CIE_x_r + CIE_y_r*CIE_x_b ) /CIE_C_gD,
        ( CIE_x_r - CIE_x_b - CIE_x_r*CIE_y_b + CIE_x_b*CIE_y_r ) /CIE_C_gD,
        ( CIE_x_b*CIE_y_r - CIE_x_r*CIE_y_b ) /CIE_C_gD},
      { ( CIE_y_r - CIE_y_g - CIE_y_r*CIE_x_g + CIE_y_g*CIE_x_r ) /CIE_C_bD,
        ( CIE_x_g - CIE_x_r - CIE_x_g*CIE_y_r + CIE_x_r*CIE_y_g ) /CIE_C_bD,
        ( CIE_x_r*CIE_y_g - CIE_x_g*CIE_y_r ) /CIE_C_bD}
};

/*
            sRGB Matrix
            static double tmat[3][3] = {     // XYZ to RGB
                  {3.2410, -1.5374, -0.4986},
                  {-0.9692, 1.8760, 0.0416},
                  {.0556, -.2040, 1.0570 }};
*/


static double c_gamma = 2.20;

int s52plib::_CIE2RGB()
{
      S52color *c2;
      int R,G,B;
      colTable *ctp;
      double dR, dG, dB;
      double X,Y,Z;

      for ( unsigned int its=0 ; its < ColorTableArray->GetCount() ; its++ )
      {
            ctp = ( colTable * ) ( ColorTableArray->Item ( its ) );

            for ( unsigned int ic=0 ; ic < ctp->color->GetCount() ; ic++ )
            {

                  c2 = ( S52color * ) ( ctp->color->Item ( ic ) );

                  //    Transform CIE xyL into CIE XYZ

                  if ( c2->y != 0 )
                  {
                        X = ( c2->x * c2->L ) / c2->y;
                        Y = c2->L;
                        Z = ( ( ( 1.0 - c2->x ) - c2->y ) * c2->L ) / c2->y;
                  }
                  else
                  {
                        X=0;
                        Y=0;
                        Z=0;
                  }

                  //    Transform CIE XYZ into RGB

                  dR = ( X * tmat[0][0] ) + ( Y * tmat[0][1] ) + ( Z * tmat[0][2] );
                  dG = ( X * tmat[1][0] ) + ( Y * tmat[1][1] ) + ( Z * tmat[1][2] );
                  dB = ( X * tmat[2][0] ) + ( Y * tmat[2][1] ) + ( Z * tmat[2][2] );


                  //       Arbitrarily clip the luminance values to 100
                  if ( dR > 100 )
                        dR = 100;
                  if ( dG > 100 )
                        dB = 100;
                  if ( dB > 100 )
                        dB = 100;

                  //       And scale
                  dR /= 100;
                  dG /= 100;
                  dB /= 100;

                  dR = pow ( dR, 1.0 / c_gamma );
                  dG = pow ( dG, 1.0 / c_gamma );
                  dB = pow ( dB, 1.0 / c_gamma );

                  R = ( int ) ( dR * 255 );
                  G = ( int ) ( dG * 255 );
                  B = ( int ) ( dB * 255 );

                  c2->R = ( unsigned char ) R;
                  c2->G = ( unsigned char ) G;
                  c2->B = ( unsigned char ) B;

                  // A special case:
                  // MSW has trouble blitting with a mask if src color is 0,0,0 ????
                  if ( ( R == 0 ) && ( G == 0 ) && ( B == 0 ) )
                  {
                        c2->R = ( unsigned char ) 7;
                        c2->G = ( unsigned char ) 7;
                        c2->B = ( unsigned char ) 7;
                  }


            }
      }

//   LoadColors(_T("/home/dsr/Projects/opencpn_sf/opencpn/data/s57data/SP52COL.DAT"));

      return true;
}


void s52plib::CreateColourHash ( void )
{
      for ( unsigned int its=0 ; its < ColorTableArray->GetCount() ; its++ )
      {
            ColourHash *phash = new ColourHash;
            ColourHashTableArray->Add ( ( void * ) phash );

            colTable *ctp = ( colTable * ) ( ColorTableArray->Item ( its ) );

            for ( unsigned int ic=0 ; ic < ctp->color->GetCount() ; ic++ )
            {
                  S52color *c2 = ( S52color * ) ( ctp->color->Item ( ic ) );

                  wxColour c ( c2->R, c2->G, c2->B );
                  wxString key ( c2->colName, wxConvUTF8 );
                  ( *phash ) [key] = c;

            }
      }
}


//      Read private file to associate CIE colors to decent RGB values
/*
int s52plib::LoadColors(const wxString& ColorFile)
{
   FILE         *fp;
   int  ret = 0;
   char         buffer[1024];
   char *pBuf = (char *)&buffer[0];
   color c1;
   color *c2;
   int R,G,B;
   colTable *ct = (colTable *)(ColorTableArray->Item(0));
   colTable *ctp;

   int colIdx = 0;
   char TableName[80];
   float x, y, Y;
        char *pBuft;

   fp = fopen(ColorFile.mb_str(), "r");
   if (fp == NULL)
   {
       wxString msg(_T("   ERROR unable to open color file:"));
       msg.Append(*ColorFile);
       wxLogMessage(msg);
      return 0;
   }

   ret = fscanf(fp, "%[^\n]", pBuf);
   fgetc(fp);

   while (ret == 1) {
          if(!strncmp((const char *)pBuf, "Table", 5))
          {
                  sscanf(pBuf, "Table:%s", TableName);

                  for(unsigned int it=0 ; it < ColorTableArray->GetCount() ; it++)
                  {
                   ctp = (colTable *)(ColorTableArray->Item(it));
                   if(!strcmp(TableName, ctp->tableName->mb_str()))
                   {
                           ct = ctp;
                           colIdx = 0;
                                break;
                        }
                  }

      fgetc(fp);
          }

//      Skip over comments "#" and blank lines
      else if ((pBuf[0] != '#') && (strlen(pBuf)))
          {

         sscanf(pBuf, "%5s", c1.colName);
                 pBuft = pBuf + 6;
                 while(*pBuft != ';')
                         pBuft++;

         sscanf(pBuft, ";%f;%f;%f;%i;%i;%i", &x, &y, &Y, &R, &G, &B);
         c1.R = (char)R;
         c1.G = (char)G;
         c1.B = (char)B;


//      Search for the matching color name in the previously loaded table
                int colMatch = 0;
                for (unsigned int ic=0; ic<ct->color->GetCount(); ++ic)
                {
                c2 = (_color *)ct->color->Item(ic);
                if (!strncmp(c1.colName, c2->colName, 5))
                        {
                    c2->R = c1.R;
                c2->G = c1.G;
                    c2->B = c1.B;
                                colMatch = 1;
                                break;
                        }
                }

                if(!colMatch)
                {
                    wxString msg(_T("Color translation failed...file, ColorName"));
                    msg.Append(*ColorFile);
                    msg.Append(_T("  "));
                    msg.Append(wxString(c1.colName, wxConvUTF8));
                    wxLogMessage(msg);
                }

      }         //if valid color line


      buffer[0] = 0;
          ret = fscanf(fp, "%[^\n]", pBuf);
      if(fgetc(fp) == '\n')
                  ret = 1;


   }
   fclose(fp);

   //   Create an unused color, for bitmap mask creation
   //   Find one by searching all the color tables

   //   This alogorithm only tries to vary R for uniqueness
   //   ....could be better

   color ctent;
   ctent.R = ctent.G = ctent.B = 0;

   bool bdone = false;

        while((ctent.R < 254) && !bdone)
        {
                int match = 0;
                for(unsigned int it=0 ; it < ColorTableArray->GetCount() ; it++)
                {
                        ct = (colTable *)(ColorTableArray->Item(it));

                        for (unsigned int ic=0; ic<ct->color->GetCount(); ++ic)
                        {
                        c2 = (_color *)ct->color->Item(ic);
                                if((c2->R == ctent.R) && (c2->G == ctent.G) && (c2->B == ctent.B))
                                        match++;
                        }

                }

                if(match == 0)
                {
                        unused_color = ctent;
                        bdone = true;
                }

                ctent.R ++;
        }



   return FALSE;
}
*/

bool s52plib::FindUnusedColor ( void )
{
      //   Create an unused color, for bitmap mask creation
      //   Find one by searching all the color tables

      //   This alogorithm only tries to vary R for uniqueness
      //   ....could be better

      S52color ctent;
      ctent.R = 255;
      ctent.G = ctent.B = 0;
      S52color *c2;
      colTable *ct;

      bool bdone = false;

      while ( ( ctent.R ) && !bdone )
      {
            int match = 0;
            for ( unsigned int it=0 ; it < ColorTableArray->GetCount() ; it++ )
            {
                  ct = ( colTable * ) ( ColorTableArray->Item ( it ) );

                  for ( unsigned int ic=0; ic<ct->color->GetCount(); ++ic )
                  {
                        c2 = ( S52color * ) ct->color->Item ( ic );
                        if ( ( c2->R == ctent.R ) && ( c2->G == ctent.G ) && ( c2->B == ctent.B ) )
                              match++;
                  }

            }

            if ( match == 0 )
            {
                  m_unused_color = ctent;
                  bdone = true;
            }

            ctent.R --;
      }

      m_unused_wxColor.Set(m_unused_color.R, m_unused_color.G, m_unused_color.B);

      return true;
}




wxArrayOfLUPrec *s52plib::SelectLUPARRAY ( LUPname TNAM )
{
      switch ( TNAM )
      {
            case SIMPLIFIED:                  return pointSimplLUPArray;
            case PAPER_CHART:                 return pointPaperLUPArray;
            case LINES:                       return lineLUPArray;
            case PLAIN_BOUNDARIES:            return areaPlaineLUPArray;
            case SYMBOLIZED_BOUNDARIES:       return areaSymbolLUPArray;
            default:                          return NULL;
//          wxLogMessage(_T("S52:_selctLUP() ERROR"));
      }

//   return NULL;
}



extern Cond condTable[];



// get LUP with "best" Object attribute match
LUPrec *s52plib::FindBestLUP ( wxArrayPtrVoid *nameMatch, char *objAtt,
                               wxArrayOfS57attVal *objAttVal, bool bStrict )
{
      LUPrec *LUP = NULL;
      int nATTMatch = 0;
      int i = 0;
      int   countATT = 0;
//   double current_best_score = 0;
      bool bmatch_found = false;


      // setup default to the first LUP
      LUP = ( LUPrec* ) nameMatch->Item ( 0 );

      int nLUPCandidates = nameMatch->GetCount();

      for ( i=0; i< nLUPCandidates; ++i )
      {
            LUPrec *LUPCandidate = NULL;
            wxString *ATTC  = NULL;
            countATT = 0;
            char *currATT  = objAtt;
            int   attIdx   = 0;

            LUPCandidate = ( LUPrec* ) nameMatch->Item ( i );
            ATTC   = ( wxString* ) LUPCandidate->ATTC;


//      if (0 == strncmp(LUPtmp->OBCL, "BOYLAT", 6))
//         int eert = 8;


            if ( ATTC == NULL )
                  continue;

            if ( objAtt == NULL )
                  return LUP;                    // att match


            for ( unsigned int iLUPAtt = 0 ; iLUPAtt < LUPCandidate->ATTCArray->GetCount() ; iLUPAtt++ )
            {

                  wxString LATTC = LUPCandidate->ATTCArray->Item ( iLUPAtt );
                  wxString LATTValue(LATTC.Mid(6));

                  // debug
//                        char *lupatt = ( char * ) ( LATTC.mb_str() + 6);
//                        int luprec = LUPCandidate->RCID;

                  while ( *currATT != '\0' )
                  {
                        if ( 0 == strncmp ( LATTC.mb_str(), currATT,6 ) )
                        {
                              //OK we have an attribute match
                              //checking attribute value
                              S57attVal *v;
#define BOOL bool
                              BOOL attValMatch = FALSE;

                              // special case (i)
                              if ( LATTC.Mid(6,1) == ' ' )  // use any value
                                    attValMatch = TRUE;

                              // special case (ii)
                              //TODO  Find an ENC with "UNKNOWN" DRVAL1 or DRVAL2 and debug this code
                              /*
                                                                      if ( !strncmp ( LUPCandidate->OBCL, "DEPARE", 6 ) )
                                                                      {
                                                                              if ( LATTC[6] == '?' )  // match if value is unknown
                                                                                      attValMatch = TRUE;
                                                                      }
                              */
                              v = ( objAttVal->Item ( attIdx ) );


                              switch ( v->valType )
                              {
                                    case OGR_INT:     // S57 attribute type 'E' enumerated, 'I' integer
                                    {
                                          int a;
                                          char ss[40];
                                          strncpy(ss, LATTValue.mb_str(), 39);
                                          sscanf ( ss, "%d", &a );
//                                          sscanf ( LATTC.mb_str() + 6, "%d", &a );
                                          if ( a == * ( int* ) ( v->value ) )
                                                attValMatch = TRUE;
                                          break;
                                    }

                                    case OGR_INT_LST:    // S57 attribute type 'L' list: comma separated integer
                                    {
                                          int a;
                                          char ss[40];
                                          strncpy(ss, LATTValue.mb_str(), 39);
                                          char *s = &ss[0];

//                                          char *s = ( char * ) ( LATTC.mb_str() + 6 );
                                          int *b = ( int* ) v->value;
                                          sscanf ( s, "%d", &a );

                                          while ( *s != '\0' )
                                          {
                                                if ( a == *b )
                                                {
                                                      sscanf ( ++s, "%d", &a );
                                                      b++;
                                                      attValMatch = TRUE;

                                                }
                                                else
                                                      attValMatch = FALSE;
                                          }
                                          break;
                                    }
                                    case OGR_REAL:               // S57 attribute type'F' float
                                    {
                                          float a;
                                          if ( LATTC.Mid(6,1) != '?' )
                                          {
                                                if(LATTValue.Len())
                                                {
                                                    char ss[40];
                                                    strncpy(ss, LATTValue.mb_str(), 39);
                                                    sscanf ( ss, "%f", &a );
//                                                sscanf ( LATTC.mb_str() + 6, "%f", &a );
                                                    if ( a == * ( float* ) ( v->value ) )
                                                      attValMatch = TRUE;
                                                }
                                          }
                                          break;
                                    }

                                    case OGR_STR:    // S57 attribute type'A' code string, 'S' free text
                                    {

                                          //    Strings must be exact match
                                          //    n.b. OGR_STR is used for S-57 attribute type 'L', comma-separated list

                                          wxString cs(( char * ) v->value, wxConvUTF8);                // Attribute from object
                                          if ( LATTValue.Len() == cs.Len() )
                                                if ( LATTValue == cs )
                                                      attValMatch = TRUE;
/*
                                          char *s = ( char * ) ( LATTC.mb_str() + 6 );   // attribute from LUP candidate
                                          char *c = ( char * ) v->value;                // Attribute from object
                                          if ( strlen ( s ) == strlen ( c ) )
                                                if ( !strcmp ( s,c ) )
                                                      attValMatch = TRUE;
*/

                                          break;
                                    }

                                    default:
//                                                printf("S52:_findFromATT(): unknown attribute type\n");
                                          break;
                              }   //switch

                              // value match
                              if ( attValMatch )
                                    ++countATT;

                              goto next_LUP_Attr;
                        }  // if attribute match

                        while ( *currATT != '\037' )
                              currATT++;
                        currATT++;

                        ++attIdx;

                  }  //while

            next_LUP_Attr:
//                        continue;

                  currATT  = objAtt;            // restart the object attribute list
                  attIdx = 0;
            }             // for iLUPAtt

            //      Create a "match score", defined as fraction of candidate LUP attributes
            //      actually matched by feature.
            //      Used later for resolving "ties"

            int nattr_matching_on_candidate = countATT;
            int nattrs_on_candidate = LUPCandidate->ATTCArray->GetCount();
            double candidate_score = ( 1. * nattr_matching_on_candidate ) / ( 1. * nattrs_on_candidate );


            //       Use some "fuzzy" logic to chack for a match
            //       This method is not S52 compliant
            /*
                     //       If the number of attributes matched on this LUP candidate
                     //       is larger than the best acheived so far, adopt this candidate
                  if (countATT > nATTMatch){
                     nATTMatch = countATT;
                     current_best_score = candidate_score;
                     bmatch_found = true;
                     LUP = LUPCandidate;
                  }

                  //       Else, if the number of attributes matched on this candidate is
                  //       equal to the best obtained so far, then use the "match score" value to
                  //       resolve the tie.  Best match wins....

                  else if (countATT == nATTMatch){

                     if(candidate_score > current_best_score){
                        nATTMatch = countATT;
                        current_best_score = candidate_score;
                        bmatch_found = true;
                        LUP = LUPCandidate;
                     }

                  }
            */

            //       According to S52 specs, match must be perfect,
            //         and the first 100% match is selected
            if ( candidate_score == 1.0 )
            {
                  LUP = LUPCandidate;
                  bmatch_found = true;
                  break;                        // selects the first 100% match
            }



      }  //for loop


//  In strict mode, we require at least one attribute to match exactly

      if ( bStrict )
      {
            if ( nATTMatch == 0 )               // nothing matched
                  LUP = NULL;
      }
      else
      {
//      If no match found, return the first LUP in the list which has no attributes
            if ( !bmatch_found )
            {
                  for ( i=0; i< ( int ) nameMatch->GetCount(); ++i )
                  {
                        LUPrec *LUPtmp = NULL;

                        LUPtmp = ( LUPrec* ) nameMatch->Item ( i );
                        if ( LUPtmp->ATTCArray == NULL )
                              return LUPtmp;
                  }
            }
      }

      return LUP;
}


// scan foward stop on ; or end-of-record
#define SCANFWRD        while( !(*str == ';' || *str == '\037')) ++str;

#define INSTRUCTION(s,t)        if(0==strncmp(s,str,2)){\
                              str+=3;\
                              r->ruleType = t;\
                              r->INSTstr  = str;

Rules *s52plib::StringToRules ( const wxString& str_in )
{
      char *str0 = ( char * ) calloc ( str_in.Len() +1, 1 );
      strncpy ( str0, str_in.mb_str(), str_in.Len() );
      char *str = str0;
//    char *str = (char *)str_in;

      Rules *top;
      Rules *last;
      char strk[20];

      Rules *r = ( Rules* ) calloc ( 1, sizeof ( Rules ) );
      top = r;
      last = top;

      r->INST0 = str0;                 // save the head for later free

      while ( *str != '\0' )
      {
            if ( r->ruleType )           // in the loop, r has been used
            {
                  r = ( Rules* ) calloc ( 1, sizeof ( Rules ) );
                  last->next = r;
                  last = r;
            }


            // parse Symbology instruction in string

            // Special Case for Circular Arc,  (opencpn private)
            // Allocate a Rule structure to be used to hold a cached bitmap of the created symbol
            INSTRUCTION ( "CA",RUL_ARC_2C )
            r->razRule = ( Rule* ) calloc ( 1,sizeof ( Rule ) );
            r->b_private_razRule = true;                            // mark this raxRule to be free'd later
            SCANFWRD
      }

      // Special Case for MultPoint Soundings
      INSTRUCTION ( "MP",RUL_MUL_SG ) SCANFWRD
}

// SHOWTEXT
INSTRUCTION ( "TX",RUL_TXT_TX ) SCANFWRD
}

INSTRUCTION ( "TE",RUL_TXT_TE ) SCANFWRD
}

// SHOWPOINT

if ( 0==strncmp ( "SY",str,2 ) )
{
      str+=3;
      r->ruleType = RUL_SYM_PT;
      r->INSTstr  = str;

//              if(!strncmp(str, "BOYGEN03", 8))
//                  int kkf = 5;

      strncpy ( strk, str, 8 );
      strk[8]=0;
      wxString key ( strk,wxConvUTF8 );

      r->razRule = ( *_symb_sym ) [key];

      if ( r->razRule == NULL )
            r->razRule = ( *_symb_sym ) [_T ( "QUESMRK1" ) ];

      SCANFWRD
}


// SHOWLINE
INSTRUCTION ( "LS",RUL_SIM_LN ) SCANFWRD
}

INSTRUCTION ( "LC",RUL_COM_LN )
strncpy ( strk, str, 8 );
strk[8]=0;
        wxString key ( strk,wxConvUTF8 );

        r->razRule = ( *_line_sym ) [key];

                     if ( r->razRule == NULL )
                           r->razRule = ( *_symb_sym ) [_T ( "QUESMRK1" ) ];
                                  SCANFWRD
                                  }

                                  // SHOWAREA
                                  INSTRUCTION ( "AC",RUL_ARE_CO ) SCANFWRD
                                  }

                                  INSTRUCTION ( "AP",RUL_ARE_PA )
                                  strncpy ( strk, str, 8 );
                                  strk[8]=0;
                                          wxString key ( strk,wxConvUTF8 );
//                    key += 'R';

                                          r->razRule = ( *_patt_sym ) [key];
                                                       if ( r->razRule == NULL )
                                                             r->razRule = ( *_patt_sym ) [_T ( "QUESMRK1V" ) ];
                                                                    SCANFWRD
                                                                    }

                                                                    // CALLSYMPROC

                                                                    if ( 0==strncmp ( "CS",str,2 ) )
{
      str+=3;
      r->ruleType = RUL_CND_SY;
      r->INSTstr  = str;

//      INSTRUCTION("CS",RUL_CND_SY)
      char stt[9];
      strncpy ( stt, str, 8 );
      stt[8] = 0;
      wxString index ( stt, wxConvUTF8 );
      r->razRule = ( *_cond_sym ) [index];
      if ( r->razRule == NULL )
            r->razRule = ( *_cond_sym ) [_T ( "QUESMRK1" ) ];
      SCANFWRD
}

++str;
}

      //  If it should happen that no rule is built, delete the initially allocated rule
      if ( 0 == top->ruleType )
      {
            if ( top->INST0 )
                  free ( top->INST0 );

            free ( top );

            top = NULL;
      }

      //   Traverse the entire rule set tree, pruning after first unallocated (dead) rule
      r = top;
      while ( r )
      {
            if ( 0 == r->ruleType )
            {
                  free ( r );
                  last->next = NULL;
                  break;
            }

            last = r;
            Rules *n = r->next;
            r = n;
      }


      //   Traverse the entire rule set tree, adding sequence numbers
      r = top;
      int i = 0;
      while ( r )
      {
            r->n_sequence = i++;

            r = r->next;
      }

      return top;
}




int s52plib::_LUP2rules ( LUPrec *LUP, S57Obj *pObj )
{
      if ( NULL == LUP )
            return -1;
      // check if already parsed
      if ( LUP->ruleList != NULL )
      {
            //printf("S52parser:_LUP2rules(): rule list already existe for %s\n", LUP->OBCL);
            return 0;
      }

      if ( LUP->INST != NULL )
      {
            Rules *top  = StringToRules ( *LUP->INST );
            LUP->ruleList = top;

            return 1;
      }
      else
            return 0;
}



//-------------------------
//
// S52 PARSER SECTION
//
//-------------------------

// MAX_BUF == 1024 --for buffer overflow
#define ENDLN   "%1024[^\037]"
#define NEWLN  "%1024[^\n]"

int s52plib::ReadS52Line ( char *pBuffer, const char *delim, int nCount, FILE *fp )
{
      int ret;

      ret =  fscanf ( fp, delim, pBuffer );

      fgetc ( fp );

      if ( nCount )  // skip \n
            fgetc ( fp );

      return ret;
}


int s52plib::ChopS52Line ( char *pBuffer, char c )
{
      int i;

      for ( i=0; pBuffer[i] != '\0'; ++i )
            if ( pBuffer[i] == '\037' )
                  pBuffer[i] = c;

      return i;
}

int s52plib::ParsePos ( position *pos, char *buf, bool patt )
{
      if ( patt )
      {
            sscanf ( buf,"%5d%5d",&pos->minDist.PAMI,&pos->maxDist.PAMA );
            buf += 10;
      }

      sscanf ( buf, "%5d%5d%5d%5d%5d%5d",&pos->pivot_x.PACL,&pos->pivot_y.PARW,
               &pos->bnbox_w.PAHL,&pos->bnbox_h.PAVL,
               &pos->bnbox_x.PBXC,&pos->bnbox_y.PBXR );
      return 1;
}


int s52plib::ParseLBID ( FILE *fp )
{

      wxString s ( pBuf, wxConvUTF8 );
      wxStringTokenizer tkz ( s, _T ( '\037' ) );

      wxString token = tkz.GetNextToken();          // something like "113LI00001REVIHO"
      token = tkz.GetNextToken();                   // ESID
      token = tkz.GetNextToken();

      //    Get PLIB version number
      double version;
      if ( token.ToDouble ( &version ) )
      {
            m_VersionMajor = ( ( int ) ( version * 10 ) ) / 10;
            m_VersionMinor = ( int ) round ( ( version - m_VersionMajor ) * 10 );
      }
      else
      {
            m_VersionMajor = 0;
            m_VersionMinor = 0;
      }

      return 1;
}

int s52plib::ParseCOLS ( FILE *fp )
{
      int ret;
      colTable *ct = new colTable;

      // get color table name
      ChopS52Line ( pBuf, '\0' );

      ct->tableName = new wxString ( pBuf+19,  wxConvUTF8 );
      ct->color     = new wxArrayPtrVoid;

      ColorTableArray->Add ( ( void * ) ct );

      // read color
      ret  = ReadS52Line ( pBuf, NEWLN, 0,fp );
      while ( 0 != strncmp ( pBuf, "****",4 ) )
      {
            S52color *c = new S52color;
            ChopS52Line ( pBuf, ' ' );
            strncpy ( c->colName, pBuf+9, 5 );
            c->colName[5] = 0;

            sscanf ( pBuf+14,"%lf %lf %lf",&c->x,&c->y,&c->L );
            ct->color->Add ( c );
            ret  = ReadS52Line ( pBuf, NEWLN, 0,fp );
      }
      return ret;
}



#define MOD_REC(str)    if(0==strncmp(#str,pBuf,4))
int s52plib::ParseLUPT ( FILE *fp )
{
      int    ret;

      BOOL    inserted = FALSE;

      LUPrec  *LUP = ( LUPrec* ) calloc ( 1, sizeof ( LUPrec ) );
      pAlloc->Add ( LUP );

      LUP->nSequence = m_LUPSequenceNumber++;                              // add a sequence number

      LUP->DISC = ( enum _DisCat ) OTHER;                                  // as a default

      sscanf ( pBuf+11, "%d", &LUP->RCID );

      //   Debug hook
//   if(LUP->RCID == 92668)
//      int uuip = 8;

      strncpy ( LUP->OBCL, pBuf+19, 6 );

      //   Debug Hook
      //if(!strncmp(LUP->OBCL, "_extgn", 6))
      //      int qewr = 9;

      LUP->FTYP = ( enum _Object_t ) pBuf[25];
      LUP->DPRI = ( enum _DisPrio ) pBuf[30];
      LUP->RPRI = ( enum _RadPrio ) pBuf[31];
      LUP->TNAM = ( enum _LUPname ) pBuf[36];

      ret  = ReadS52Line ( pBuf, NEWLN, 0,fp );

      do
      {
            MOD_REC ( ATTC )
            {
                  if ( '\037' != pBuf[9] )                      // could be empty!
                  {

                        wxArrayString *pAS = new wxArrayString();
                        char *p = &pBuf[9];


                        wxString *st1 = new wxString;

                        while ( ( *p != '\r' ) && ( *p ) )
                        {
                              while ( *p != 0x1f )
                              {
                                    st1->Append ( *p );
                                    p++;
                              }

                              pAS->Add ( *st1 );
                              st1->Clear();
                              p++;
                        }

                        delete st1;

                        LUP->ATTCArray = pAS;

                        ChopS52Line ( pBuf, ' ' );
                        LUP->ATTC = new wxString ( pBuf+9, wxConvUTF8 );
                  }
            }

            MOD_REC ( INST ) LUP->INST = new wxString ( pBuf+9, wxConvUTF8 );
            MOD_REC ( DISC ) LUP->DISC = ( enum _DisCat ) pBuf[9];
            MOD_REC ( LUCM ) sscanf ( pBuf+9, "%d",&LUP->LUCM );

            MOD_REC ( **** )
            {

                  // Add LUP to array
                  wxArrayOfLUPrec *pLUPARRAYtyped = SelectLUPARRAY ( LUP->TNAM );

                  // Search the LUPArray to see if there is already a LUP with this RCID
                  // If found, replace it with the new LUP
                  // This provides a facility for updating the LUP tables after loading a basic set


                  unsigned int index = 0;

                  while ( index < pLUPARRAYtyped->GetCount() )
                  {
                        LUPrec *pLUPCandidate = pLUPARRAYtyped->Item ( index );
                        if ( LUP->RCID == pLUPCandidate->RCID )
                        {
                              DestroyLUP ( pLUPCandidate );         // empties the LUP
                              pLUPARRAYtyped->Remove ( pLUPCandidate );
                              break;
                        }
                        index++;
                  }


                  pLUPARRAYtyped->Add ( LUP );

                  inserted = TRUE;

            }         // MOD_REC

            ret = ReadS52Line ( pBuf, NEWLN, 0,fp );

      }
      while ( inserted == FALSE );

      return 1;
}


int s52plib::ParseLNST ( FILE *fp )
{
      int  ret;

      char strk[20];

      BOOL inserted = FALSE;
      Rule *lnstmp  = NULL;
      Rule *lnst = ( Rule* ) calloc ( 1, sizeof ( Rule ) );
      pAlloc->Add ( lnst );

      lnst->exposition.LXPO = new wxString;
      wxString LVCT;
      wxString LCRF;

      sscanf ( pBuf+11, "%d", &lnst->RCID );

      ret  = ReadS52Line ( pBuf, NEWLN, 0,fp );
      do
      {
            MOD_REC ( LIND )
            {
                  strncpy ( lnst->name.LINM, pBuf+9, 8 ); // could be empty!
                  ParsePos ( &lnst->pos.line, pBuf+17, FALSE );
            }

            MOD_REC ( LXPO ) lnst->exposition.LXPO->Append ( wxString ( pBuf+9, wxConvUTF8 ) );
            MOD_REC ( LCRF ) LCRF.Append ( wxString ( pBuf+9, wxConvUTF8 ) );       // CIDX + CTOK
            MOD_REC ( LVCT ) LVCT.Append ( wxString ( pBuf+9, wxConvUTF8 ) );
            MOD_REC ( **** )
            {

                  lnst->vector.LVCT = ( char * ) calloc ( LVCT.Len() +1, 1 );
                  strncpy ( lnst->vector.LVCT, LVCT.mb_str(), LVCT.Len() );

                  lnst->colRef.LCRF = ( char * ) calloc ( LCRF.Len() +1, 1 );
                  strncpy ( lnst->colRef.LCRF, LCRF.mb_str(), LCRF.Len() );

                  // check if key already there
                  strncpy ( strk, lnst->name.LINM, 8 );
                  strk[8]=0;
                  wxString key ( strk,wxConvUTF8 );

                  //wxString key((lnst->name.LINM), 8);
                  lnstmp  = ( *_line_sym ) [key];

                  // insert in Hash Table
                  if ( NULL == lnstmp )
                        ( *_line_sym ) [key] = lnst;
                  else if ( lnst->name.LINM != lnstmp->name.LINM )
                        ( *_line_sym ) [key] = lnst;
                  else
                        assert ( 0 ); // key must be unique --should not reach this

                  inserted = TRUE;
            }
            ret  = ReadS52Line ( pBuf, NEWLN, 0,fp );
            ChopS52Line ( pBuf, '\0' );
      }
      while ( inserted == FALSE );

      return ret;
}


//void DestroyPatternRuleNode(Rule *pR);

int s52plib::ParsePATT ( FILE *fp )
{
      int  ret;

      int bitmap_width;
      char pbm_line[200];                  // max bitmap width...
      char strk[20];

      BOOL inserted = FALSE;
      Rule *pattmp  = NULL;
      Rule *patt = ( Rule* ) calloc ( 1,sizeof ( Rule ) );
      pAlloc->Add ( patt );

      patt->exposition.PXPO  = new wxString;
      patt->bitmap.PBTM     = new wxString;
      wxString PVCT;
      wxString PCRF;

      sscanf ( pBuf+11, "%d", &patt->RCID );

      ret  = ReadS52Line ( pBuf, NEWLN, 0,fp );

      do
      {
            MOD_REC ( PATD )
            {
                  strncpy ( patt->name.PANM, pBuf+9, 8 );
                  patt->definition.PADF = pBuf[17];
                  patt->fillType.PATP  = pBuf[18];          // first character 'S' or 'L', for staggered or linear
                  patt->spacing.PASP   = pBuf[21];
                  ParsePos ( &patt->pos.patt, pBuf+24, TRUE );
            }

            MOD_REC ( PXPO ) patt->exposition.PXPO->Append ( wxString ( pBuf+9, wxConvUTF8 ) );
            MOD_REC ( PCRF ) PCRF.Append ( wxString ( pBuf+9, wxConvUTF8 ) );  // CIDX+CTOK
            MOD_REC ( PVCT ) PVCT.Append ( wxString ( pBuf+9, wxConvUTF8 ) );

            MOD_REC ( PBTM )
            {
                  bitmap_width = patt->pos.patt.bnbox_w.SYHL;
//                if(bitmap_width > 200)
//                        wxLogMessage(_T("ParsePatt....bitmap too wide."));
                  strncpy ( pbm_line, pBuf+9, bitmap_width );
                  pbm_line[bitmap_width] = 0;
                  patt->bitmap.SBTM->Append ( wxString ( pbm_line, wxConvUTF8 ) );
            }


            MOD_REC ( **** )
            {

                  patt->vector.PVCT = ( char * ) calloc ( PVCT.Len() +1, 1 );
                  strncpy ( patt->vector.PVCT, PVCT.mb_str(), PVCT.Len() );

                  patt->colRef.PCRF = ( char * ) calloc ( PCRF.Len() +1, 1 );
                  strncpy ( patt->colRef.PCRF, PCRF.mb_str(), PCRF.Len() );

                  // check if key already there
                  strncpy ( strk, patt->name.PANM, 8 );
                  strk[8]=0;
                  wxString key ( strk, wxConvUTF8 );

                  /*
                            char key_plus[20];
                            strncpy(key_plus, &patt->definition.SYDF, 1);
                            key_plus[1] = 0;
                            key += wxString(key_plus, wxConvUTF8);
                  */

                  pattmp  = ( *_patt_sym ) [key];

                  if ( NULL == pattmp )                  // not there, so....
                        ( *_patt_sym ) [key] = patt;        // insert in hash table


                  else                                   // already something here with same key...
                  {
                        if ( patt->name.PANM != pattmp->name.PANM )   // if the pattern names are not identical
                        {
                              ( *_patt_sym ) [key] = patt;            // replace the pattern
                              DestroyPatternRuleNode ( pattmp );      // remember to free to replaced node
                              // the node itself is destroyed as part of pAlloc
                        }

                  }

                  inserted = TRUE;
            }
            ret  = ReadS52Line ( pBuf, NEWLN, 0,fp );
            ChopS52Line ( pBuf, '\0' );

      }
      while ( inserted == FALSE );

      return ret;
}



int s52plib::ParseSYMB ( FILE *fp, RuleHash *pHash )
{
      int  ret;

      int bitmap_width;
      char pbm_line[200];                  // max bitmap width...
      BOOL inserted = FALSE;
//   Rule *symbtmp  = NULL;
      Rule *symb = ( Rule* ) calloc ( 1,sizeof ( Rule ) );
      pAlloc->Add ( symb );
      Rule *symbtmp = NULL;

      symb->exposition.SXPO = new wxString;
      symb->bitmap.SBTM     = new wxString;
      wxString SVCT;
      wxString SCRF;

      sscanf ( pBuf+11, "%d", &symb->RCID );

      // debug
//  if (symb->RCID == 3385)
//              int gghj = 5;

      ret  = ReadS52Line ( pBuf, NEWLN, 0,fp );

      do
      {
            MOD_REC ( SYMD )
            {
                  strncpy ( symb->name.SYNM, pBuf+9, 8 );
                  symb->definition.SYDF = pBuf[17];
                  ParsePos ( &symb->pos.symb, pBuf+18, FALSE );

//                       if(symb->pos.symb.bnbox_x.SBXC)
//                              int ggkl = 3;
            }

            MOD_REC ( SXPO ) symb->exposition.SXPO->Append ( wxString ( pBuf+9, wxConvUTF8 ) );

            MOD_REC ( SBTM )
            {
                  bitmap_width = symb->pos.symb.bnbox_w.SYHL;
                  if ( bitmap_width > 200 )
                        wxLogMessage ( _T ( "ParseSymb....bitmap too wide." ) );
                  strncpy ( pbm_line, pBuf+9, bitmap_width );
                  pbm_line[bitmap_width] = 0;
                  symb->bitmap.SBTM->Append ( wxString ( pbm_line, wxConvUTF8 ) );
            }

            MOD_REC ( SCRF )     SCRF.Append ( wxString ( pBuf+9, wxConvUTF8 ) );  // CIDX+CTOK

            MOD_REC ( SVCT )     SVCT.Append ( wxString ( pBuf+9, wxConvUTF8 ) );

            if ( ( 0==strncmp ( "****",pBuf,4 ) ) || ( ret == -1 ) )
            {
                  symb->vector.SVCT = ( char * ) calloc ( SVCT.Len() +1, 1 );
                  strncpy ( symb->vector.SVCT, SVCT.mb_str(), SVCT.Len() );

                  symb->colRef.SCRF = ( char * ) calloc ( SCRF.Len() +1, 1 );
                  strncpy ( symb->colRef.SCRF, SCRF.mb_str(), SCRF.Len() );

                  // Create a key
                  char keyt[20];
                  strncpy ( keyt, symb->name.SYNM, 8 );
                  keyt[8]=0;
                  wxString key ( keyt, wxConvUTF8 );

                  symbtmp  = ( *pHash ) [key];

                  if ( NULL == symbtmp )                  // not there, so....
                        ( *pHash ) [key] = symb;        // insert in hash table


                  else                                   // already something here with same key...
                  {
                        if ( symb->name.SYNM != symbtmp->name.SYNM )   // if the pattern names are not identical
                        {
                              ( *pHash ) [key] = symb;                 // replace the pattern
                              DestroyRuleNode ( symbtmp );      // remember to free to replaced node
                              // the node itself is destroyed as part of pAlloc
                        }
                  }
                  inserted = TRUE;
            }
            ret  = ReadS52Line ( pBuf, NEWLN, 0,fp );
            ChopS52Line ( pBuf, '\0' );

      }
      while ( inserted == FALSE );

      return ret;
}



//-------------------------
//
// MAIN SECTION
//
//-------------------------
#ifndef _COMPARE_LUP_DEFN_
#define _COMPARE_LUP_DEFN_
//-----------------------------------------------------------------------------
//      Comparison Function for LUPArray sorting
//      Note Global Scope
//-----------------------------------------------------------------------------
int CompareLUPObjects ( LUPrec *item1, LUPrec *item2 )
{
      // sort the items by their name...
#if wxCHECK_VERSION(2, 9, 0)
      int ir = wxStricmp ( item1->OBCL, item2->OBCL );
#else
      int ir = Stricmp ( item1->OBCL, item2->OBCL );
#endif
      if ( ir == 0 )
            return item1->nSequence - item2->nSequence;
      else
            return ir;
}

#endif

int s52plib::S52_load_Plib ( const wxString& PLib )
{

      FILE *fp = NULL;
      int  nRead;

      fp = fopen ( PLib.mb_str(), "r" );

      if ( fp == NULL )
      {
            wxString msg ( _T ( "   S52PLIB: Cannot open S52 rules file " ) );
            msg.Append ( PLib );
            wxLogMessage ( msg );
            return 0;
      }

      ColorTableArray = new wxArrayPtrVoid;
      ColourHashTableArray = new wxArrayPtrVoid;
      pAlloc = new wxArrayPtrVoid;


      //   Create the Rule Lookup Hash Tables
      _line_sym      = new RuleHash;    // line
      _patt_sym      = new RuleHash;    // pattern
      _symb_sym      = new RuleHash;    // symbol
      _cond_sym      = new RuleHash;    // conditional



      //   Build the initially empty sorted arrays of LUP Records, per LUP type.
      //   Sorted on object name, e.g. ACHARE.  Why sorted?  Helps in the S52_LUPLookup method....
      pointSimplLUPArray    = new wxArrayOfLUPrec ( CompareLUPObjects );   // point simplified
      pointPaperLUPArray    = new wxArrayOfLUPrec ( CompareLUPObjects );   // point traditional(paper)
      lineLUPArray          = new wxArrayOfLUPrec ( CompareLUPObjects );   // lines;
      areaPlaineLUPArray    = new wxArrayOfLUPrec ( CompareLUPObjects );   // area plain boundary
      areaSymbolLUPArray    = new wxArrayOfLUPrec ( CompareLUPObjects );   // area symbolized boundary
      condSymbolLUPArray    = new wxArrayOfLUPrec ( CompareLUPObjects );   // dynamic Cond Sym LUPs


      m_LUPSequenceNumber = 0;

      while ( 1 == ( nRead = ReadS52Line ( pBuf,NEWLN,0,fp ) ) )
      {
            // !!! order important !!!
            MOD_REC ( LBID ) ParseLBID ( fp );
            MOD_REC ( COLS ) ParseCOLS ( fp );
            MOD_REC ( LUPT ) ParseLUPT ( fp );
            MOD_REC ( LNST ) ParseLNST ( fp );
            MOD_REC ( PATT ) ParsePATT ( fp );
            MOD_REC ( SYMB ) ParseSYMB ( fp, _symb_sym );

            MOD_REC ( 0001 ) continue;
            MOD_REC ( **** ) continue;

      }
      fclose ( fp );

      //   Initialize the _cond_sym Hash Table from the jump table found in S52CNSY.CPP
      //   Hash Table indices are the literal CS Strings, e.g. "RESARE02"
      //   Hash Results Values are the Rule *, i.e. the CS procedure entry point

      for ( int i=0 ; condTable[i].condInst != NULL; ++i )
      {
            wxString index ( condTable[i].name, wxConvUTF8 );
            ( *_cond_sym ) [index] = ( Rule * ) ( condTable[i].condInst );
      }


      _CIE2RGB();
      FindUnusedColor();
      CreateColourHash();

     wxString oc_file(*g_pcsv_locn);
     oc_file.Append(_T("/s57objectclasses.csv"));

     PreloadOBJLFromCSV(oc_file);

      return 1;
}

void s52plib::DestroyPatternRuleNode ( Rule *pR )
{
      if ( pR )
      {
            if ( pR->exposition.LXPO )
                  delete pR->exposition.LXPO;

            free ( pR->vector.LVCT );

            if ( pR->bitmap.SBTM )
                  delete pR->bitmap.SBTM;

            free ( pR->colRef.SCRF );

            if ( pR->pixelPtr )
            {
                  if ( pR->parm0 == ID_GL_PATT_SPEC )
                  {
                        render_canvas_parms *pp = ( render_canvas_parms * ) ( pR->pixelPtr );
                        free ( pp->pix_buff );
                        delete pp;
                  }
                  else if ( pR->parm0 == ID_RGB_PATT_SPEC )
                  {
                        render_canvas_parms *pp = ( render_canvas_parms * ) ( pR->pixelPtr );
                        free ( pp->pix_buff );
                        delete pp;
                  }
            }
      }
}


void s52plib::DestroyRuleNode ( Rule *pR )
{
      if ( pR )
      {

            if ( pR->exposition.LXPO )
                  delete pR->exposition.LXPO;

            free ( pR->vector.LVCT );

            if ( pR->bitmap.SBTM )
                  delete pR->bitmap.SBTM;

            free ( pR->colRef.SCRF );

            if ( pR->parm0 && pR->pixelPtr )
            {
                  switch(pR->parm0)
                  {
                        case ID_wxBitmap:
                        {
                              wxBitmap *pbm = ( wxBitmap * ) ( pR->pixelPtr );
                              delete pbm;
                              pR->pixelPtr = NULL;
                              pR->parm0 = 0;
                              break;
                        }
                        case ID_RGBA:
                        {
                              unsigned char *p = ( unsigned char * ) ( pR->pixelPtr );
                              free ( p );
                              pR->pixelPtr = NULL;
                              pR->parm0 = 0;
                              break;
                        }
                  }
            }

            if ( pR->pixelPtr )
            {
                  if ( pR->definition.PADF == 'R' )
                  {
                        wxBitmap *pbm = ( wxBitmap * ) ( pR->pixelPtr );
                        delete pbm;
                  }
            }
      }
}


void s52plib::DestroyRules ( RuleHash *rh )
{

      RuleHash::iterator it;
      wxString key;
      Rule *pR;

      for ( it = ( *rh ).begin(); it != ( *rh ).end(); ++it )
      {
            key = it->first;
            pR = it->second;
            if ( pR )
            {

                  if ( pR->exposition.LXPO )
                        delete pR->exposition.LXPO;

                  free ( pR->vector.LVCT );

                  if ( pR->bitmap.SBTM )
                        delete pR->bitmap.SBTM;

                  free ( pR->colRef.SCRF );

                  if ( pR->parm0 && pR->pixelPtr )
                  {
                        switch(pR->parm0)
                        {
                              case ID_wxBitmap:
                              {
                                    wxBitmap *pbm = ( wxBitmap * ) ( pR->pixelPtr );
                                    delete pbm;
                                    pR->pixelPtr = NULL;
                                    pR->parm0 = 0;
                                    break;
                              }
                              case ID_RGBA:
                              {
                                    unsigned char *p = ( unsigned char * ) ( pR->pixelPtr );
                                    free ( p );
                                    pR->pixelPtr = NULL;
                                    pR->parm0 = 0;
                                    break;
                              }
                        }
                  }
            }
      }

      rh->clear();
      delete rh;
}


void s52plib::FlushSymbolCaches ( void )
{
      RuleHash *rh = _symb_sym;

      if(!rh)
            return;

      RuleHash::iterator it;
      wxString key;
      Rule *pR;

      for ( it = ( *rh ).begin(); it != ( *rh ).end(); ++it )
      {
            pR = it->second;
            if ( pR )
            {
                  if ( pR->parm0 && pR->pixelPtr )
                  {
                        switch(pR->parm0)
                        {
                              case ID_wxBitmap:
                              {
                                    wxBitmap *pbm = ( wxBitmap * ) ( pR->pixelPtr );
                                    delete pbm;
                                    pR->pixelPtr = NULL;
                                    pR->parm0 = 0;
                                    break;
                              }
                              case ID_RGBA:
                              {
                                    unsigned char *p = ( unsigned char * ) ( pR->pixelPtr );
                                    free ( p );
                                    pR->pixelPtr = NULL;
                                    pR->parm0 = 0;
                                    break;
                              }
                        }
                  }
            }
      }

      //    Flush any pattern definitions
      rh = _patt_sym;

      if(!rh)
            return;

      for ( it = ( *rh ).begin(); it != ( *rh ).end(); ++it )
      {
            pR = it->second;
            if ( pR )
            {
                  if ( pR->parm0 && pR->pixelPtr )
                  {
                        switch(pR->parm0)
                        {
                              case ID_GL_PATT_SPEC:
                              {
                                    render_canvas_parms *pp = ( render_canvas_parms * ) ( pR->pixelPtr );
                                    free ( pp->pix_buff );
                                    glDeleteTextures( 1, (GLuint *)&pp->OGL_tex_name );
                                    delete pp;
                                    pR->pixelPtr = NULL;
                                    pR->parm0 = 0;
                                    break;
                              }
                              case ID_RGB_PATT_SPEC:
                              {
                                    render_canvas_parms *pp = ( render_canvas_parms * ) ( pR->pixelPtr );
                                    free ( pp->pix_buff );
                                    delete pp;
                                    pR->pixelPtr = NULL;
                                    pR->parm0 = 0;
                                    break;
                              }
                        }
                  }
            }
      }
}



void s52plib::DestroyPattRules ( RuleHash *rh )
{

      RuleHash::iterator it;
      wxString key;
      Rule *pR;

      for ( it = ( *rh ).begin(); it != ( *rh ).end(); ++it )
      {
            key = it->first;
            pR = it->second;
            if ( pR )
            {
                  if ( pR->exposition.LXPO )
                        delete pR->exposition.LXPO;

                  free ( pR->vector.LVCT );

                  if ( pR->bitmap.SBTM )
                        delete pR->bitmap.SBTM;

                  free ( pR->colRef.SCRF );

                  if ( pR->pixelPtr )
                  {
                        if ( pR->definition.PADF == 'V' )
                        {
                              render_canvas_parms *pp = ( render_canvas_parms * ) ( pR->pixelPtr );
                              free ( pp->pix_buff );
                              delete pp;
                        }
                        else if ( pR->definition.PADF == 'R' )
                        {
                              render_canvas_parms *pp = ( render_canvas_parms * ) ( pR->pixelPtr );
                              free ( pp->pix_buff );
                              delete pp;
                        }
                  }
            }

      }

      rh->clear();
      delete rh;
}

void s52plib::DestroyLUP ( LUPrec *pLUP )
{
      Rules  *top = pLUP->ruleList;

      while ( top != NULL )
      {
            Rules *Rtmp = top->next;

            if ( top->INST0 )
                  free ( top->INST0 );        // free the Instruction string head

            if(top->b_private_razRule)          // need to free razRule?
            {
                  Rule *pR = top->razRule;
                  if ( pR->exposition.LXPO )
                        delete pR->exposition.LXPO;

                  free ( pR->vector.LVCT );

                  if ( pR->bitmap.SBTM )
                        delete pR->bitmap.SBTM;

                  free ( pR->colRef.SCRF );

                  if ( pR->parm0 && pR->pixelPtr )
                  {
                        switch(pR->parm0)
                        {
                              case ID_wxBitmap:
                              {
                                    wxBitmap *pbm = ( wxBitmap * ) ( pR->pixelPtr );
                                    delete pbm;
                                    pR->pixelPtr = NULL;
                                    pR->parm0 = 0;
                                    break;
                              }
                              case ID_RGBA:
                              {
                                    unsigned char *p = ( unsigned char * ) ( pR->pixelPtr );
                                    free ( p );
                                    pR->pixelPtr = NULL;
                                    pR->parm0 = 0;
                                    break;
                              }
                        }
                  }

                  free ( pR );
            }

            free ( top );
            top = Rtmp;
      }

      delete pLUP->ATTCArray;

      delete pLUP->ATTC;
      delete pLUP->INST;
}


void s52plib::DestroyLUPArray ( wxArrayOfLUPrec *pLUPArray )
{
      if(pLUPArray)
      {
            for ( unsigned int il = 0 ; il < pLUPArray->GetCount() ; il++ )
                  DestroyLUP ( pLUPArray->Item ( il ) );

            pLUPArray->Clear();

            delete pLUPArray;
      }
}


void s52plib::ClearCNSYLUPArray ( void )
{
      if(condSymbolLUPArray)
      {
            for ( unsigned int i = 0 ; i < condSymbolLUPArray->GetCount() ; i++ )
                  DestroyLUP ( condSymbolLUPArray->Item ( i ) );

            condSymbolLUPArray->Clear();
      }
}


bool s52plib::S52_flush_Plib()
{

//      Color Tables
      if ( ColorTableArray )
      {
            for ( unsigned int ic = 0 ; ic < ColorTableArray->GetCount() ; ic++ )
            {
                  colTable *ct = ( colTable * ) ColorTableArray->Item ( ic );

                  delete ct->tableName;
                  for ( unsigned int icc = 0 ; icc < ct->color->GetCount() ; icc++ )
                  {
                        delete ( S52color * ) ( ct->color->Item ( icc ) );
                  }

                  delete ct->color;
                  delete ct;
            }
      }

      delete ColorTableArray;

//      Color Hash Tables
      if ( ColourHashTableArray )
      {
            for ( unsigned int ich = 0 ; ich < ColourHashTableArray->GetCount() ; ich++ )
            {
                  ColourHash *ch = ( ColourHash * ) ColourHashTableArray->Item ( ich );

                  delete ch;
            }
      }

      delete ColourHashTableArray;


      //    OpenGL Hashmaps
      CARC_Hash::iterator ita;
      for( ita = m_CARC_hashmap.begin(); ita != m_CARC_hashmap.end(); ++ita )
      {
            GLuint list = ita->second;
            glDeleteLists(list, 1);
      }
      m_CARC_hashmap.clear();


      // destroy look-up tables
      DestroyLUPArray ( lineLUPArray );
      DestroyLUPArray ( pointSimplLUPArray );
      DestroyLUPArray ( pointPaperLUPArray );
      DestroyLUPArray ( areaPlaineLUPArray );
      DestroyLUPArray ( areaSymbolLUPArray );
      DestroyLUPArray ( condSymbolLUPArray );

//      Destroy Rules
      DestroyRules ( _line_sym );
      DestroyPattRules ( _patt_sym );
      DestroyRules ( _symb_sym );

      if ( _symb_symR )
            DestroyRules ( _symb_symR );

//      Special case for CS
      RuleHash::iterator it;
      Rule *pR;
      for ( it = ( *_cond_sym ).begin(); it != ( *_cond_sym ).end(); ++it )
      {
            pR = it->second;
//              delete pR;
      }
      delete ( _cond_sym );


      for ( unsigned int ipa = 0 ; ipa < pAlloc->GetCount() ; ipa++ )
      {
            void *t = pAlloc->Item ( ipa );
            free ( t );
      }

      delete pAlloc;

      return TRUE;
}



LUPrec *s52plib::S52_LUPLookup ( LUPname LUP_Name, const char * objectName, S57Obj *pObj, bool bStrict )

{
      LUPrec *LUP = NULL;
      LUPrec *LUPCandidate;

      wxArrayOfLUPrec *la = SelectLUPARRAY ( LUP_Name );

      if ( NULL == la )                 // S52PLIB probably did not load
            return NULL;

      wxArrayPtrVoid *nameMatch = new wxArrayPtrVoid;


      int ocnt = 0;

      int first_match = 0;
      int index = 0;
      int index_max = la->GetCount();

      //        This technique of extracting proper LUPs depends on the fact that
      //        the LUPs have been sorted in their array, by OBCL.
      //        Thus, all the LUPS with the same OBCL will be grouped together

      while ( !first_match && ( index < index_max ) )
      {
            LUPCandidate = la->Item ( index );
            if ( !strcmp ( objectName, LUPCandidate->OBCL ) )
            {
                  first_match = 1;
                  ocnt++;
                  nameMatch->Add ( LUPCandidate );
                  index++;
                  break;
            }
            index++;
      }


      while ( first_match && ( index < index_max ) )
      {
            LUPCandidate = la->Item ( index );
            if ( !strcmp ( objectName, LUPCandidate->OBCL ) )
            {
                  ocnt++;
                  nameMatch->Add ( LUPCandidate );
            }
            else
            {
                  break;
            }

            index++;
      }


      char *temp;

      if ( ocnt == 0 )
            goto BAILOUT;

      temp = ( char * ) calloc ( pObj->attList->Len() +1, 1 );
      strncpy ( temp, pObj->attList->mb_str(), pObj->attList->Len() );

      LUP = FindBestLUP ( nameMatch,temp, pObj->attVal, bStrict );

      free ( temp );


BAILOUT:
      nameMatch->Clear();

      delete nameMatch;

      return LUP;
}




/*
void s52plib::SetPLIBColorScheme(S52_Col_Scheme_t c)
{
    //      Only use available color schemes
    if((int)c > (ColorTableArray->GetCount() - 1))
        m_PLIBColorScheme = (S52_Col_Scheme_t)(ColorTableArray->GetCount() - 1);
    else
        m_PLIBColorScheme = c;
}
*/

void s52plib::SetPLIBColorScheme ( wxString scheme )
{
      wxString str_find;
      str_find = scheme;
      m_colortable_index = 0;       // default is the first color in the table

//      Of course, it also depends on the plib version...
//    plib version 3.2 calls "DAY" colr as "DAY_BRIGHT"

      if ( ( GetMajorVersion() == 3 ) && ( GetMinorVersion() == 2 ) )
      {
            if ( scheme.IsSameAs ( _T ( "DAY" ) ) )
                  str_find = _T ( "DAY_BRIGHT" );
      }



      //Search the color table array
      if ( ColorTableArray )
      {
            for ( unsigned int i=0 ; i< ColorTableArray->GetCount() ; i++ )
            {
                  colTable *ct = ( colTable * ) ColorTableArray->Item ( i );
                  if ( str_find.IsSameAs ( *ct->tableName ) )
                  {
                        m_colortable_index = i;
                        m_ColorScheme = scheme;

                        break;
                  }
            }
      }

}


S52color *s52plib::S52_getColor ( const char *colorName )
{
      S52color *c = NULL;

      unsigned int i;
      colTable *ct;

      ct = ( colTable * ) ColorTableArray->Item ( m_colortable_index );

      for ( i=0; i<ct->color->GetCount(); ++i )
      {

            c = ( S52color * ) ct->color->Item ( i );
            if ( 0 == strncmp ( colorName, c->colName, 5 ) )
                  return c;
      }

      /*
         c = &g_array_index(ct->color, color, 1);
         printf("S52:S52_getColor(): ERROR no color name: %s\n",colorName);
      */
      return c;
}

wxColour s52plib::S52_getwxColour ( const wxString &colorName )
{
//       wxString key(colorName, wxConvUTF8);
      if ( NULL == ColourHashTableArray )
            return wxNullColour;

      ColourHash *pcurrentcolorhash = ( ColourHash * ) ColourHashTableArray->Item ( m_colortable_index );

      wxColour c = ( *pcurrentcolorhash ) [colorName];

      return c;
}



//----------------------------------------------------------------------------------
//
//              Object Rendering Module
//
//----------------------------------------------------------------------------------


//-----------------------------
//
// S52 TEXT COMMAND WORD PARSER
//
//-----------------------------
#define APOS   '\047'
#define MAXL       512

#if 0
wxString GetGenericAttr ( S57Obj *obj, char *AttrName )
{
      wxString str;
      char *attList = ( char * ) calloc ( obj->attList->Len() +1, 1 );
      strncpy ( attList, obj->attList->mb_str(), obj->attList->Len() );
//        char *attList = (char *)(obj->attList->);        //attList is wxString

      char *patl = attList;
      char *patr;
      int idx = 0;
      while ( *patl )
      {
            patr = patl;
            while ( *patr != '\037' )
                  patr++;

            if ( !strncmp ( patl, AttrName, 6 ) )
                  break;

            patl = patr + 1;
            idx++;
      }

      if ( !*patl )
      {
            free ( attList );
            return str;
      }

//      using idx to get the attribute value
      wxArrayOfS57attVal      *pattrVal = obj->attVal;

      S57attVal *v = pattrVal->Item ( idx );

      switch ( v->valType )
      {
            case OGR_STR:
            {
                  char *val = ( char * ) ( v->value );
                  str.Append ( wxString ( val,wxConvUTF8 ) );
                  break;
            }
            case OGR_REAL:
            {
                  double dval = * ( double* ) ( v->value );
                  str.Printf ( _T ( "%g" ), dval );
                  break;
            }
            case OGR_INT:
            {
                  int ival = * ( ( int * ) v->value );
                  str.Printf ( _T ( "%d" ), ival );
                  break;
            }
            default:
            {
                  str.Printf ( _T ( "Unknown attribute type" ) );
                  break;
            }
      }



      free ( attList );
      return str;
}


bool GetFloatAttr ( S57Obj *obj, char *AttrName, float &val )
{
      char *attList = ( char * ) calloc ( obj->attList->Len() +1, 1 );
      strncpy ( attList, obj->attList->mb_str(), obj->attList->Len() );
//    char *attList = (char *)(obj->attList->);        //attList is wxString

      char *patl = attList;
      char *patr;
      int idx = 0;
      while ( *patl )
      {
            patr = patl;
            while ( *patr != '\037' )
                  patr++;

            if ( !strncmp ( patl, AttrName, 6 ) )
                  break;

            patl = patr + 1;
            idx++;
      }

      if ( !*patl )                                                // Requested Attribute not found
      {
            free ( attList );
            return false;                                           // so don't return a value
      }

//      using idx to get the attribute value
      wxArrayOfS57attVal      *pattrVal = obj->attVal;

      S57attVal *v = pattrVal->Item ( idx );
      val = * ( float* ) ( v->value );

      free ( attList );
      return true;
}


bool GetDoubleAttr ( S57Obj *obj, char *AttrName, double &val )
{
      char *attList = ( char * ) calloc ( obj->attList->Len() +1, 1 );
      strncpy ( attList, obj->attList->mb_str(), obj->attList->Len() );

      char *patl = attList;
      char *patr;
      int idx = 0;
      while ( *patl )
      {
            patr = patl;
            while ( *patr != '\037' )
                  patr++;

            if ( !strncmp ( patl, AttrName, 6 ) )
                  break;

            patl = patr + 1;
            idx++;
      }

      if ( !*patl )                                                // Requested Attribute not found
      {
            free ( attList );
            return false;                                           // so don't return a value
      }

//      using idx to get the attribute value
      wxArrayOfS57attVal      *pattrVal = obj->attVal;

      S57attVal *v = pattrVal->Item ( idx );
      val = * ( double* ) ( v->value );

      return true;
}

#endif


char      *_getParamVal ( ObjRazRules *rzRules, char *str, char *buf, int bsz )
// Symbology Command Word Parameter Value Parser.
//
//      str: psz to Attribute of interest
//
//      Results:Put in 'buf' one of:
//  1- LUP constant value,
//  2- ENC value,
//  3- LUP default value.
// Return pointer to the next field in the string (delim is ','), NULL to abort
{
      char    *tmp    = buf;
      wxString value;
      int      defval = 0;    // default value
      int      len    = 0;

      // parse constant parameter with concatenation operator "'"
      if ( str != NULL )
      {
            if ( *str == APOS )
            {
                  str++;
                  while ( *str != APOS )
                  {
                        *buf++ = *str++;
                  }
                  *buf = '\0';
                  str++;  // skip "'"
                  str++;  // skip ","

                  return str;
            }

            while ( *str!=',' && *str!=')' && *str!='\0' && len<(bsz-1) )
            {
                  *tmp++ = *str++;
                  ++len;
            }

            if (len >= bsz)
                printf("ERROR: chopping input S52 line !? \n");

            *tmp = '\0';
            str++;        // skip ',' or ')'
      }
      if ( len<6 )
            return str;

      // chop string if default value present
      if ( len > 6 && * ( buf+6 ) == '=' )
      {
            * ( buf+6 ) = '\0';
            defval = 1;
      }

      value = rzRules->obj->GetAttrValueAsString ( buf );

      if ( value.IsNull() )
      {
            if ( defval )
                  _getParamVal ( rzRules, buf+7, buf, bsz-7 ); // default value --recursion
            else
            {
                  // PRINTF("NOTE: skipping TEXT no value for attribute:%s\n", buf);
                  return NULL;                        // abort
            }
      }
      else
      {
            int vallen = value.Len();

            if ( vallen >= bsz )
            {
                  vallen =  bsz-1;
//            PRINTF("ERROR: chopping attribut value !? \n");
            }

            //    Special case for conversion of some vertical (height) attributes to feet
            if (( !strncmp ( buf, "VERCLR", 6 ) ) || ( !strncmp ( buf, "VERCCL", 6 ) ))
            {
                  switch(ps52plib->m_nDepthUnitDisplay)
                  {
                        case 0:                       // feet
                        case 2:                       // fathoms
                              double ft_val;
                              value.ToDouble(&ft_val);
                              ft_val = ft_val * 3 * 39.37 / 36;              // feet
                              value.Printf(_T("%4.1f"), ft_val);
                              vallen = value.Len();
                              break;
                        default:
                              break;
                  }
            }

            // special case when ENC returns an index for particular attribute types
            if ( !strncmp ( buf, "NATSUR", 6 ) )
            {

                  wxString natsur_att ( _T ( "NATSUR" ) );
                  wxString result;
                  wxString svalue  = value;
                  wxStringTokenizer tkz ( svalue, _T ( "," ) );

                  int icomma = 0;
                  while ( tkz.HasMoreTokens() )
                  {
                        if ( icomma )
                              result += _T ( "," );

                        wxString token = tkz.GetNextToken();
                        int i = atoi ( token.mb_str() );
                        wxString nat = rzRules->chart->GetAttributeDecode ( natsur_att, i );
                        if ( !nat.IsEmpty() )
                              result += nat;            // value from ENC
                        else
                              result += _T ( "unk" );

                        icomma++;
                  }

                  int count = result.Len();
                  if ( count > bsz-1 )
                        count = bsz-1;
                  strncpy ( buf, result.mb_str(), count );
                  buf[count] = 0;



            }
            else
            {
                  strncpy ( buf, value.mb_str(), vallen );         // value from ENC
                  buf[vallen] = '\0';
            }
      }

      return str;
}



char *_parseTEXT ( ObjRazRules *rzRules, S52_TextC *text, char *str0 )
{
      char buf[MAXL] = {'\0'};   // output string

      char *str = str0;

      if(text)
      {
            str = _getParamVal ( rzRules, str, &text->hjust, MAXL );   // HJUST
            str = _getParamVal ( rzRules, str, &text->vjust, MAXL );   // VJUST
            str = _getParamVal ( rzRules, str, &text->space, MAXL );   // SPACE

            // CHARS
            str         = _getParamVal ( rzRules, str, buf, 5 );
            text->style = buf[0];
            text->weight= buf[1];
            text->width = buf[2];
            text->bsize = atoi ( buf+3 );

            str         = _getParamVal ( rzRules, str, buf, MAXL );
            text->xoffs = atoi ( buf );          // XOFFS
            str         = _getParamVal ( rzRules, str, buf, MAXL );
            text->yoffs = atoi ( buf );          // YOFFS
            str         = _getParamVal ( rzRules, str, buf, MAXL );
            text->pcol   = ps52plib->S52_getColor ( buf );  // COLOUR
            str         = _getParamVal ( rzRules, str, buf, MAXL );
            text->dis   = atoi ( buf );          // Text Group, used for "Important" text detection
      }
      return str;
}


S52_TextC   *S52_PL_parseTX ( ObjRazRules *rzRules, Rules *rules, char *cmd )
{
      S52_TextC *text = NULL;
      char *str      = NULL;
      char val[MAXL] = {'\0'};   // value of arg

      str = ( char* ) rules->INSTstr;

      str = _getParamVal ( rzRules, str, val, MAXL );   // get ATTRIB list
      if ( NULL == str )
            return 0;   // abort this command word if mandatory param absent

      val[MAXL - 1] = '\0';                               // make sure the string terminates

      text = new S52_TextC;
      str = _parseTEXT ( rzRules, text, str );
      if ( NULL != text )
            text->frmtd = wxString ( val, wxConvUTF8 );

      return text;
}


S52_TextC   *S52_PL_parseTE ( ObjRazRules *rzRules, Rules *rules, char *cmd )
// same as S52_PL_parseTX put parse 'C' format first
{
      char arg[MAXL] = {'\0'};   // ATTRIB list
      char fmt[MAXL] = {'\0'};   // FORMAT
      char buf[MAXL] = {'\0'};   // output string
      char *b    = buf;
      char *parg = arg;
      char *pf   = fmt;
      S52_TextC *text = NULL;

      char *str = ( char* ) rules->INSTstr;

      if(str && *str)
      {
            str = _getParamVal ( rzRules, str, fmt, MAXL );   // get FORMAT

            str = _getParamVal ( rzRules, str, arg, MAXL );   // get ATTRIB list
            if ( NULL == str )
                  return 0;   // abort this command word if mandatory param absent


            //*b = *pf;
            while ( *pf != '\0' )
            {

                  // begin a convertion specification
                  if ( *pf == '%' )
                  {
                        char val[MAXL] = {'\0'};   // value of arg
                        char tmp[MAXL] = {'\0'};   // temporary format string
                        char *t = tmp;
                        int  cc        = 0;        // 1 == Conversion Character found
                        //*t = *pf;

                        // get value for this attribute
                        parg = _getParamVal ( rzRules, parg, val, MAXL );
                        if ( NULL == parg )
                              return 0;   // abort

                        if ( 0==strcmp ( val, "2147483641" ) )
                              return 0;

                        *t = *pf;       // stuff the '%'

                        // scan for end at convertion character
                        do
                        {
                              *++t = *++pf;   // fill conver spec

                              switch ( *pf )
                              {
                                    case 'c':
                                    case 's': b += sprintf ( b, tmp, val );       cc = 1; break;
                                    case 'f': b += sprintf ( b, tmp, atof ( val ) ); cc = 1; break;
                                    case 'd':
                                    case 'i': b += sprintf ( b, tmp, atoi ( val ) ); cc = 1; break;
                              }
                        }
                        while ( !cc );
                        pf++;             // skip conv. char

                  }
                  else
                        *b++ = *pf++;
            }

            text = new S52_TextC;
            str = _parseTEXT ( rzRules, text, str );
            if ( NULL != text )
                  text->frmtd = wxString ( buf, wxConvUTF8 );
      }

      return text;
}

bool s52plib::RenderText ( wxDC *pdc, S52_TextC *ptext, int x, int y, wxRect *pRectDrawn, S57Obj *pobj, bool bCheckOverlap )
{
#ifdef DrawText
#undef DrawText
#define FIXIT
#endif
      bool bdraw = true;

      if(!pdc)                // OpenGL
      {
#if 1
            if(1)
            {
                  if(!ptext->m_pRGBA)            // is RGBA bitmap ready?
                  {
                        wxMemoryDC mdc;

                        mdc.SetFont ( * ( ptext->pFont ) );

                        wxCoord w, h, descent, exlead;
                        mdc.GetTextExtent ( ptext->frmtd, &w, &h, &descent, &exlead ); // measure the text
                        ptext->rendered_char_height = h - descent;

                        wxBitmap bmp(w,h);
                        mdc.SelectObject(bmp);

                        if(mdc.IsOk())
                        {
                              //  Render the text as white on black, so that underlying anti-aliasing of
                              //  wxDC text rendering can be extracted and converted to alpha-channel values.

                              mdc.SetBackground(wxBrush(wxColour(0,0,0)));
                              mdc.SetBackgroundMode ( wxTRANSPARENT );

                              mdc.SetTextForeground ( wxColour(255,255,255) );

                              mdc.Clear();

                              mdc.DrawText (   ptext->frmtd, 0, 0 );

                              wxImage image = bmp.ConvertToImage();
                              int ws = image.GetWidth(), hs = image.GetHeight();

                              ptext->RGBA_width = ws;
                              ptext->RGBA_height = hs;
                              ptext->m_pRGBA = (unsigned char *)malloc(4*ws*hs);

                              unsigned char *d = image.GetData();
                              unsigned char *pdest = ptext->m_pRGBA;
                              S52color *ccolor = ptext->pcol;

                              for(int y=0; y<hs; y++)
                                    for(int x=0; x<ws; x++)
                                    {
                                          unsigned char r, g, b;
                                          int off = (y*ws+x);
                                          r = d[off*3 + 0];
                                          g = d[off*3 + 1];
                                          b = d[off*3 + 2];

                                          pdest[off*4 + 0] = ccolor->R;
                                          pdest[off*4 + 1] = ccolor->G;
                                          pdest[off*4 + 2] = ccolor->B;

                                          int alpha = (r + g + b)/3;
                                          pdest[off*4 + 3] = (unsigned char)(alpha & 0xff);
                                    }

                              mdc.SelectObject(wxNullBitmap);
                        }     // mdc OK

                  }    // Building m_RGBA

                  //    Render the bitmap
                  if(ptext->m_pRGBA)
                  {
                        //  Adjust the y position to account for the convention that S52 text is drawn
                        //  with the lower left corner at the specified point, instead of the wx convention
                        //  using upper right corner
                        int yp = y  - ( ptext->rendered_char_height );
                        int xp = x;

                        //  Add in the offsets, specified in units of nominal font height
                        yp += ptext->yoffs * ( ptext->rendered_char_height );
                        xp += ptext->xoffs * ( ptext->rendered_char_height );

                        pRectDrawn->SetX ( xp );
                        pRectDrawn->SetY ( yp );
                        pRectDrawn->SetWidth ( ptext->RGBA_width );
                        pRectDrawn->SetHeight ( ptext->RGBA_height );

                        if ( bCheckOverlap )
                        {
                              if ( CheckTextRectList ( *pRectDrawn, pobj ) )
                                    bdraw = false;
                        }

                        if(bdraw)
                        {
                              int x_offset = 0;
                              int y_offset = 0;
                              int draw_width = ptext->RGBA_width;
                              int draw_height = ptext->RGBA_height;

                              if(xp < 0)
                              {
                                    x_offset = -xp;
                                    draw_width += xp;
                              }
                              if(yp < 0)
                              {
                                    y_offset = -yp;
                                    draw_height += yp;
                              }

                              glColor4f(1, 1, 1, 1);

                              glEnable(GL_BLEND);
                              glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                              glPixelZoom(1, -1);

                              glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);

                              glPixelStorei(GL_UNPACK_ROW_LENGTH, ptext->RGBA_width);

                              glRasterPos2i(xp + x_offset, yp + y_offset);

                              glPixelStorei(GL_UNPACK_SKIP_PIXELS, x_offset);
                              glPixelStorei(GL_UNPACK_SKIP_ROWS, y_offset);

                              glDrawPixels(draw_width, draw_height, GL_RGBA, GL_UNSIGNED_BYTE, ptext->m_pRGBA);
                              glPixelZoom(1, 1);
                              glDisable(GL_BLEND);

                              glPopClientAttrib();
                        }     // bdraw

                  }

            } // 1

#endif
#if 0
            if(!m_txf_ready)
                  PrepareTxfRenderer();

            if(m_txf_ready)
            {
                  int w, h, descent;
                  txfGetStringMetrics(m_txf, (char *)(const char *)ptext->frmtd.mb_str(), ptext->frmtd.Len(), &w, &h, &descent);
                  {
                        glEnable(GL_TEXTURE_2D);
                        glColor3f(0, 0, 0);

                        txfBindFontTexture(m_txf);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);

                        glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
//                        glAlphaFunc(GL_GREATER, 0.0625);
                        glAlphaFunc(GL_GREATER, (float)0.15);
                        glEnable(GL_ALPHA_TEST);
                        glEnable(GL_BLEND);
                        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

                        glPushMatrix();

                        int yp = y;
                        int xp = x;

                        //    Calculate pixel size from pica point size
                        double pixel_per_mm = (double)::wxGetDisplaySize().x / ::wxGetDisplaySizeMM().x;
                        double pix_size = ptext->bsize * .351 * pixel_per_mm;

                        //    Font will be scaled, so scale width and height too
                        double scale_factor = pix_size/(m_txf_avg_char_height - 4);

                        //  Add in the offsets, specified in units of nominal font height
                        xp += ptext->xoffs * pix_size;
                        yp += ptext->yoffs * pix_size;


                        pRectDrawn->SetX ( xp );
                        pRectDrawn->SetY ( yp );
                        pRectDrawn->SetWidth ( w * scale_factor );
                        pRectDrawn->SetHeight ( h * scale_factor );

                        if ( bCheckOverlap )
                        {
                              if ( CheckTextRectList ( *pRectDrawn, pobj ) )
                                    bdraw = false;
                        }

                        if(bdraw)
                        {
                              glTranslatef(xp-4, yp+4, 0);
                              glScalef(1.0, -1.0, 1.0);
                              glScalef(scale_factor, scale_factor, 1.0);

                              txfRenderString(m_txf, (char *)(const char *)ptext->frmtd.mb_str(), ptext->frmtd.Len());
                        }

                        glPopMatrix();

                        glDisable(GL_TEXTURE_2D);
                        glDisable(GL_ALPHA_TEST);
                        glDisable(GL_BLEND);
/*
                        glBegin(GL_LINE_LOOP);
                        glVertex2i(xp, yp);
                        glVertex2i(xp+w* scale_factor, yp);
                        glVertex2i(xp+w* scale_factor, yp-h* scale_factor);
                        glVertex2i(xp, yp-h* scale_factor);
                        glEnd();
*/
                  }
            }
            else
                  bdraw = false;
#endif
      }
      else
      {
            wxFont oldfont = pdc->GetFont(); // save current font

            pdc->SetFont ( * ( ptext->pFont ) );

            wxCoord w, h, descent, exlead;
            pdc->GetTextExtent ( ptext->frmtd, &w, &h, &descent, &exlead ); // measure the text

            //  Adjust the y position to account for the convention that S52 text is drawn
            //  with the lower left corner at the specified point, instead of the wx convention
            //  using upper right corner
            int yp = y  - ( h - descent );
            int xp = x;

            //  Add in the offsets, specified in units of nominal font height
            yp += ptext->yoffs * ( h - descent );
            xp += ptext->xoffs * ( h - descent );

            pRectDrawn->SetX ( xp );
            pRectDrawn->SetY ( yp );
            pRectDrawn->SetWidth ( w );
            pRectDrawn->SetHeight ( h );

            if ( bCheckOverlap )
            {
                  if ( CheckTextRectList ( *pRectDrawn, pobj ) )
                        bdraw = false;
            }

            if ( bdraw )
            {
                  S52color *bcolor = S52_getColor ( "CHGRF" );
                  wxColour color ( bcolor->R, bcolor->G, bcolor->B );

                  pdc->SetTextForeground ( color );
                  pdc->SetBackgroundMode ( wxTRANSPARENT );

                  pdc->DrawText (  ptext->frmtd, xp, yp+1 );
                  pdc->DrawText (  ptext->frmtd, xp, yp-1 );
                  pdc->DrawText (  ptext->frmtd, xp+1, yp );
                  pdc->DrawText (  ptext->frmtd, xp-1, yp );

                  bcolor = ptext->pcol;
                  wxColour wcolor ( bcolor->R, bcolor->G, bcolor->B );
                  pdc->SetTextForeground ( wcolor );

                  pdc->DrawText (   ptext->frmtd, xp, yp );

      //   TODO Remove Debug
/*
                      pdc->SetBrush(*wxTRANSPARENT_BRUSH);
                      pdc->SetPen(*wxBLACK_PEN);
                      pdc->DrawRectangle(xp, yp, w, h);
*/
            }

            pdc->SetFont ( oldfont );              // restore last font

      }
      return bdraw;

#ifdef FIXIT
#undef FIXIT
#define DrawText DrawTextA
#endif


}


//    Return true if test_rect overlaps any rect in the current text rectangle list, except itself
bool s52plib::CheckTextRectList ( const wxRect &test_rect, S57Obj *pobj )
{
      //    Iterate over the current object list, looking at rText

      for ( ObjList::Node *node = m_textObjList.GetFirst(); node; node = node->GetNext() )
      {
            wxRect *pcurrent_rect = & ( node->GetData()->rText );

            if ( pcurrent_rect->Intersects ( test_rect ) )
            {
                  if ( node->GetData() != pobj )
                        return true;

            }
      }
      return false;
}

bool s52plib::TextRenderCheck ( ObjRazRules *rzRules )
{
      if ( !m_bShowS57Text )
            return false;

      //    This logic:  if Aton text is off, but "light description" is on, then show light description anyway
      if ( ( rzRules->obj->bIsAton ) && ( !m_bShowAtonText ) )
      {
            if ( !strncmp ( rzRules->obj->FeatureName, "LIGHTS", 6 ) )
            {
                  if ( !m_bShowLdisText )
                        return false;
            }
            else
                  return false;
      }

      //    An optimization for CM93 charts.
      //    Don't show the text associated with some objects, since CM93 database includes _texto objects aplenty
      if ( ( rzRules->chart->GetChartType() == CHART_TYPE_CM93 ) || ( rzRules->chart->GetChartType() == CHART_TYPE_CM93COMP ) )
      {
            if ( !strncmp ( rzRules->obj->FeatureName, "BUAARE", 6 ) )
                  return false;
            else if ( !strncmp ( rzRules->obj->FeatureName, "SEAARE", 6 ) )
                  return false;
            else if ( !strncmp ( rzRules->obj->FeatureName, "LNDRGN", 6 ) )
                  return false;
      }

      return true;
}

int s52plib::RenderT_All ( ObjRazRules *rzRules, Rules *rules, ViewPort *vp, bool bTX )
{
      if ( !TextRenderCheck ( rzRules ) )
            return 0;

      S52_TextC *text = NULL;
      bool b_free_text = false;

      //  The first Ftext object is cached in the S57Obj.
      //  If not present, create it on demand
      if ( !rzRules->obj->bFText_Added )
      {
            if ( bTX )
                  text = S52_PL_parseTX ( rzRules, rules, NULL );
            else
                  text = S52_PL_parseTE ( rzRules, rules, NULL );

            if ( text )
            {
                  rzRules->obj->bFText_Added = true;
                  rzRules->obj->FText = text;
                  rzRules->obj->FText->rul_seq_creator = rules->n_sequence;
            }
      }

      //    S57Obj already contains a cached text object
      //    If it was created by this Rule earlier, then render it
      //    Otherwise, create a new text object, render it, and delete when done
      //    This will be slower, obviously, but happens infrequently enough?
      else
      {
            if(rules->n_sequence == rzRules->obj->FText->rul_seq_creator)
                  text =  rzRules->obj->FText;
            else
            {
                  if ( bTX )
                        text = S52_PL_parseTX ( rzRules, rules, NULL );
                  else
                        text = S52_PL_parseTE ( rzRules, rules, NULL );

                  b_free_text = true;
            }

      }


      if ( text )
      {
            if ( m_bShowS57ImportantTextOnly && ( text->dis >= 20 ) )
            {
                  if(b_free_text)
                        delete text;
                  return 0;
            }

            //    Establish a font
            if(!text->pFont)
            {
                  int spec_weight = text->weight - 0x30;
                  wxFontWeight fontweight;
                  if ( spec_weight < 5 )
                        fontweight = wxFONTWEIGHT_LIGHT;
                  else if ( spec_weight == 5 )
                        fontweight = wxFONTWEIGHT_NORMAL;
                  else
                        fontweight = wxFONTWEIGHT_BOLD;

                  text->pFont = wxTheFontList->FindOrCreateFont ( text->bsize, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, fontweight );

            }


            //  Render text at declared x/y of object
            wxPoint r;
            rzRules->chart->GetPointPix ( rzRules, rzRules->obj->y, rzRules->obj->x, &r );

            wxRect rect;

            bool bwas_drawn = RenderText ( m_pdc, text, r.x, r.y, &rect, rzRules->obj, m_bDeClutterText );

            //    If this is an un-cached text object render, then do not update the S57Obj in any way
            if(b_free_text)
            {
                  delete text;
                  return 1;
            }

            rzRules->obj->rText = rect;

            //      If this text was actually drawn, add a pointer to its rect to the de-clutter list if it doesn't already exist
            if ( m_bDeClutterText )
            {
                  if ( bwas_drawn )
                  {
                        bool b_found = false;
                        for ( ObjList::Node *node = m_textObjList.GetFirst(); node; node = node->GetNext() )
                        {
                              S57Obj *oc = node->GetData();

                              if ( oc == rzRules->obj )
                              {
                                    b_found = true;
                                    break;
                              }
                        }
                        if ( !b_found )
                              m_textObjList.Append ( rzRules->obj );
                  }
            }

            //  Update the object Bounding box
            //  so that subsequent drawing operations will redraw the item fully
            //  and so that cursor hit testing includes both the text and the object

//            if ( rzRules->obj->Primitive_type == GEO_POINT )
            {
                  wxBoundingBox bbtext;
                  double plat, plon;

                  rzRules->chart->GetPixPoint ( rect.GetX(), rect.GetY() + rect.GetHeight(), &plat, &plon, vp );
                  bbtext.SetMin ( plon, plat );

                  rzRules->chart->GetPixPoint ( rect.GetX() + rect.GetWidth(), rect.GetY(), &plat, &plon, vp );
                  bbtext.SetMax ( plon, plat );

                  if(rzRules->obj->bBBObj_valid)
                        rzRules->obj->BBObj.Expand ( bbtext );
                  else
                  {
                        rzRules->obj->BBObj = bbtext;
                        rzRules->obj->bBBObj_valid = true;
                  }


            }
      }

      return 1;
}


// Text
int s52plib::RenderTX ( ObjRazRules *rzRules, Rules *rules, ViewPort *vp )
{
      return RenderT_All ( rzRules, rules, vp, true );
}


// Text formatted
int s52plib::RenderTE ( ObjRazRules *rzRules, Rules *rules, ViewPort *vp )
{
      return RenderT_All ( rzRules, rules, vp, false );
}

bool s52plib::RenderHPGLtoDC ( char *str, char *col, wxDC *pdc, wxPoint &r, wxPoint &pivot, double rot_angle )
{
      int width = 1;
      double radius = 0.0;
      int    tessObj       = FALSE;
      int    polyMode      = FALSE;
      int    inBegEnd      = FALSE;
      S52color *newColor = NULL;
      float trans = 1.0;

      int x1 = 0, x2 = 0, y1 = 0, y2 = 0;
      int x,y;

      float sin_rot = 0., cos_rot = 1.;

      if ( rot_angle )
      {
            sin_rot = sin ( rot_angle * PI / 180. );
            cos_rot = cos ( rot_angle * PI / 180. );
      }


#define MAX_POINTS 100
      wxPoint PointArray[MAX_POINTS];
      int vIdx = 1;


      float fsf = 100 / canvas_pix_per_mm;
      int scaleFac = ( int ) floor ( fsf );




#define INST(cmd)               if(0==strncmp(str,#cmd,2)) {str+=2;

      while ( *str != '\0' )
      {

            // Select (Pen) color
            INST ( SP )
//         GET_COL()
            {
                  char *c=col;
                  while ( *c != '\0' )
                  {
                        if ( *c == *str )
                              break;
                        else
                              c+=6;
                  }
                  newColor =  S52_getColor ( c+1 );
            }
      }

      // Select Transparency
      else INST ( ST )
            trans = atof ( str );
      trans = ( trans == 0 ) ? 1 : trans*0.25;

      //test
      trans = 1.0;
}

// Select pen Width
else INST ( SW )
      width = atoi ( str++ );

        /*      if (inBegEnd)
              {
                    wxLogMessage(_T("bogus BegEnd in SW"));
                    inBegEnd = FALSE;
              }
        */
        }

        // Pen Up
        else INST ( PU )
            wxColour color ( newColor->R, newColor->G, newColor->B );
            wxPen *pthispen = wxThePenList->FindOrCreatePen ( color, width, wxSOLID );

            pdc->SetPen ( *pthispen );
            sscanf ( str, "%u,%u", &x, &y );
            x1 = x - pivot.x;
            y1 = y - pivot.y;

      //              Rotation
            if ( rot_angle )
            {
                  float xp = ( x1 * cos_rot ) - ( y1 * sin_rot );
                  float yp = ( x1 * sin_rot ) + ( y1 * cos_rot );

                  x1 = ( int ) xp;
                  y1 = ( int ) yp;
            }


            x1 /= scaleFac;
            y1 /= scaleFac;

            x1 += r.x;
            y1 += r.y;

            while ( *str != ';' ) str++;
      }     //INST

      // Pen Down
      else INST ( PD )
//            do{
            if ( !inBegEnd )
            inBegEnd = TRUE;

            if ( *str == ';' )
            {
                  x2 = x1+1;
                  y2 = y1;
            }

            else
            {
                  sscanf ( str, "%u,%u", &x, &y );
                  x2 = x - pivot.x;
                  y2 = y - pivot.y;
                  if ( rot_angle )
                  {
                        float xp = ( x2 * cos_rot ) - ( y2 * sin_rot );
                        float yp = ( x2 * sin_rot ) + ( y2 * cos_rot );

                        x2 = ( int ) xp;
                        y2 = ( int ) yp;
                  }

                  x2 /= scaleFac;
                  y2 /= scaleFac;

                  x2 += r.x;
                  y2 += r.y;
            }

            pdc->DrawLine ( x1, y1, x2, y2 );

            x1=x2;                  // set for pu;pd;pd....
            y1=y2;

//                  while(*str++ != ',' );                      // specs: could repeat x,y,x, ..
//                  while(*str   != ',' && *str != ';')str++;  // but never do!
            while ( *str != ';' ) str++;

//            }while(*str != ';');           // do

//            if(0 != strncmp(str,";PD",3))
//            { // not very smart!!
//                  inBegEnd = FALSE;                           // ie put glEnd in PU !?
//            }
      }     //INST

      else INST ( CI )
            radius = ( double ) atoi ( str );
            wxColour color ( newColor->R, newColor->G, newColor->B );
            wxBrush *pthisbrush = wxTheBrushList->FindOrCreateBrush ( color, wxTRANSPARENT );
            wxPen *pthispen = wxThePenList->FindOrCreatePen ( color, width, wxSOLID );

            pdc->SetPen ( *pthispen );
            pdc->SetBrush ( *pthisbrush );

            int r1 = ( int ) radius / scaleFac;

            pdc->DrawCircle ( x1, y1, r1 );

            inBegEnd = FALSE;

            while ( *str != ';' ) ++str;

      }           //INST

//-- poly mode ---

                                                                           // trans. is 1 for CI, AA, EP, PD


                                                                           // Polygon Mode
      else INST ( PM )
            tessObj = FALSE;
            polyMode= TRUE;
            do
            {
                  if ( *str == '0' )          // start a new poly
                  {
                        str+=2;

                        // CIrcle
                        INST ( CI )
                        radius = ( double ) atoi ( str );
                        wxColour color ( newColor->R, newColor->G, newColor->B );
                        wxBrush *pthisbrush = wxTheBrushList->FindOrCreateBrush ( color, wxSOLID );
                        wxPen *pthispen = wxThePenList->FindOrCreatePen ( color, width, wxSOLID );

                        pdc->SetPen ( *pthispen );
                        pdc->SetBrush ( *pthisbrush );

                        int r1 = ( int ) radius / scaleFac;

                        pdc->DrawCircle ( x1, y1, r1 );

                        inBegEnd = FALSE;

                        while ( *str != ';' ) ++str;

                        // Arc Angle --never used!
                  }

                  else INST ( AA )
                        wxLogMessage ( _T ( "SEQuencer:_renderHPGL(): fixme AA instruction not implemented" ) );
                        inBegEnd = FALSE;
                  }     // INST AA
                  else
                  {
                        tessObj = TRUE;
                        inBegEnd = FALSE;
                  }
            }     //do

            if ( *str == '1' )
            {
      // sub poly --never used!
            str++;
            }

            if ( tessObj )
            {
                  str+=2;    // skip PD
      do
      {
            if ( !inBegEnd )
            {
                  PointArray[0].x =x1;
                  PointArray[0].y =y1;
                  vIdx = 1;
                  inBegEnd = TRUE;
            }                                               // no need to remember PU!

            // read tess vertex
            sscanf ( str, "%u,%u", &x, &y );
            x2 = x - pivot.x;
            y2 = y - pivot.y;
            if ( rot_angle )
            {
                  float xp = ( x2 * cos_rot ) - ( y2 * sin_rot );
                  float yp = ( x2 * sin_rot ) + ( y2 * cos_rot );

                  x2 = ( int ) xp;
                  y2 = ( int ) yp;
            }
            x2 /= scaleFac;
            y2 /= scaleFac;

            x2 += r.x;
            y2 += r.y;

            if ( vIdx < MAX_POINTS )
            {
                  PointArray[vIdx].x =x2;
                  PointArray[vIdx].y =y2;
            }

            ++vIdx;

            while ( *str++ != ',' );               // specs: could repeat x,y,x, ..
            while ( *str   != ',' && *str != ';' ) str++;
            if ( *str == ',' ) str++;
            if ( 0 == strncmp ( str,";PD",3 ) ) str += 3;
      }
      while ( *str != ';' );
}
}
while ( 0 != strncmp ( str,";PM2",4 ) );

// exit polygon mode
str++;
while ( *str != ';' ) ++str;


} // PM

// Edge Polygon --draw polygon with lines
// never called --not tested
else INST ( EP )
      if ( tessObj )
      {
            wxString msg = _T ( "SEQuencer:_renderHPGL(): fixme EP instruction not implemented " );
            LogMessageOnce ( msg );
      }

}

// Fill Polygon
else INST ( FP )
      if ( tessObj )
      {
            wxColour color ( newColor->R, newColor->G, newColor->B );
            wxBrush *pthisbrush = wxTheBrushList->FindOrCreateBrush ( color, wxSOLID );

            pdc->SetBrush ( *pthisbrush );
            pdc->DrawPolygon ( vIdx, PointArray );
            inBegEnd = FALSE;
      }
}

// Symbol Call    --never used
else INST ( SC )
{
      wxString msg = _T ( "SEQuencer:_renderHPGL(): fixme SC instruction not implemented " );
      LogMessageOnce ( msg );
}
}
++str;

} /* while */

return true;
       }


bool s52plib::RenderHPGLtoGL ( char *str, char *col, wxPoint &r, wxPoint &pivot, double rot_angle )
{
      int width = 1;
      double radius = 0.0;
//      int    tessObj       = FALSE;
//      int    polyMode      = FALSE;
      int    inBegEnd      = FALSE;
      S52color *newColor = NULL;
      float trans = 1.0;

      int x1 = 0, x2 = 0, y1 = 0, y2 = 0;
      int x,y;

      float sin_rot = 0., cos_rot = 1.;

      if ( rot_angle )
      {
            sin_rot = sin ( rot_angle * PI / 180. );
            cos_rot = cos ( rot_angle * PI / 180. );
      }


#define MAX_HPGL_GL_POINTS 100
//      wxPoint PointArray[MAX_HPGL_GL_POINTS];


      float fsf = 100 / canvas_pix_per_mm;
      int scaleFac = ( int ) floor ( fsf );




#define INST(cmd)               if(0==strncmp(str,#cmd,2)) {str+=2;

      while ( *str != '\0' )
      {

            // Select (Pen) color
            INST ( SP )
                  char *c=col;
                  while ( *c != '\0' )
                  {
                        if ( *c == *str )
                              break;
                        else
                              c+=6;
                  }
                  newColor =  S52_getColor ( c+1 );

            }

      // Select Transparency
	        else INST ( ST )
                trans = atof ( str );
                trans = ( trans == 0 ) ? 1 : trans*0.25;
      //test
                trans = 1.0;
	        }

// Select pen Width
            else INST ( SW )
                width = atoi ( str++ );
            }

        // Pen Up
            else INST ( PU )
                wxColour color ( newColor->R, newColor->G, newColor->B );

                glColor4ub(color.Red(), color.Green(), color.Blue(), color.Alpha());
                glLineWidth(width);

                sscanf ( str, "%u,%u", &x, &y );
                x1 = x - pivot.x;
                y1 = y - pivot.y;

      //              Rotation
                if ( rot_angle )
                {
                  float xp = ( x1 * cos_rot ) - ( y1 * sin_rot );
                  float yp = ( x1 * sin_rot ) + ( y1 * cos_rot );

                  x1 = ( int ) xp;
                  y1 = ( int ) yp;
                }


                x1 /= scaleFac;
                y1 /= scaleFac;

                x1 += r.x;
                y1 += r.y;

                while ( *str != ';' ) str++;
            }     //INST

      // Pen Down
            else INST ( PD )
                if ( !inBegEnd )
                    inBegEnd = TRUE;

                if ( *str == ';' )
                {
                  x2 = x1+1;
                  y2 = y1;
                }

                else
                {
                  sscanf ( str, "%u,%u", &x, &y );
                  x2 = x - pivot.x;
                  y2 = y - pivot.y;
                  if ( rot_angle )
                  {
                        float xp = ( x2 * cos_rot ) - ( y2 * sin_rot );
                        float yp = ( x2 * sin_rot ) + ( y2 * cos_rot );

                        x2 = ( int ) xp;
                        y2 = ( int ) yp;
                  }

                  x2 /= scaleFac;
                  y2 /= scaleFac;

                  x2 += r.x;
                  y2 += r.y;
                }

                glPushAttrib(GL_COLOR_BUFFER_BIT | GL_LINE_BIT | GL_HINT_BIT);      //Save state
          //      Enable anti-aliased lines, at best quality
                glEnable(GL_LINE_SMOOTH);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

                //if(m_pen.GetWidth() > 1)
                //  DrawThickLine(x1, y1, x2, y2, m_pen.GetWidth());
                //else
                {
                  glBegin(GL_LINES);
                  glVertex2i(x1, y1);
                  glVertex2i(x2, y2);
                  glEnd();
                }
                glPopAttrib();

                x1=x2;                  // set for pu;pd;pd....
                y1=y2;

                while ( *str != ';' ) str++;

            }     //INST

            else INST ( CI )
                radius = ( double ) atoi ( str );
                wxColour color ( newColor->R, newColor->G, newColor->B );
                glColor4ub(color.Red(), color.Green(), color.Blue(), color.Alpha());
                glLineWidth(width);

                int r1 = ( int ) radius / scaleFac;

                glPushAttrib(GL_COLOR_BUFFER_BIT | GL_LINE_BIT | GL_HINT_BIT);      //Save state

          //      Enable anti-aliased lines, at best quality
                glEnable(GL_LINE_SMOOTH);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

                glBegin(GL_LINE_STRIP);
                for(double a = 0; a <= 2*M_PI; a+=2*M_PI/200)
                    glVertex2d(x1 + r1*sin(a), y1 + r1*cos(a));
                glEnd();

                glPopAttrib();            // restore state

                inBegEnd = FALSE;
                while ( *str != ';' ) ++str;
            }           //INST

//-- poly mode ---

                                                                           // trans. is 1 for CI, AA, EP, PD


/*                                                                           // Polygon Mode
      else INST ( PM )
            tessObj = FALSE;
            polyMode= TRUE;
            do
            {
                  if ( *str == '0' )          // start a new poly
                  {
                        str+=2;

                        // CIrcle
                        INST ( CI )
                        radius = ( double ) atoi ( str );
                        wxColour color ( newColor->R, newColor->G, newColor->B );
                        wxBrush *pthisbrush = wxTheBrushList->FindOrCreateBrush ( color, wxSOLID );
                        wxPen *pthispen = wxThePenList->FindOrCreatePen ( color, width, wxSOLID );

                        pdc->SetPen ( *pthispen );
                        pdc->SetBrush ( *pthisbrush );

                        int r1 = ( int ) radius / scaleFac;

                        pdc->DrawCircle ( x1, y1, r1 );

                        inBegEnd = FALSE;

                        while ( *str != ';' ) ++str;

                        // Arc Angle --never used!
                  }

                  else INST ( AA )
                        wxLogMessage ( _T ( "SEQuencer:_renderHPGL(): fixme AA instruction not implemented" ) );
                        inBegEnd = FALSE;
                  }     // INST AA
                  else
                  {
                        tessObj = TRUE;
                        inBegEnd = FALSE;
                  }
            }     //do

            if ( *str == '1' )
            {
      // sub poly --never used!
            str++;
            }

            if ( tessObj )
            {
                  str+=2;    // skip PD
      do
      {
            if ( !inBegEnd )
            {
                  PointArray[0].x =x1;
                  PointArray[0].y =y1;
                  vIdx = 1;
                  inBegEnd = TRUE;
            }                                               // no need to remember PU!

            // read tess vertex
            sscanf ( str, "%u,%u", &x, &y );
            x2 = x - pivot.x;
            y2 = y - pivot.y;
            if ( rot_angle )
            {
                  float xp = ( x2 * cos_rot ) - ( y2 * sin_rot );
                  float yp = ( x2 * sin_rot ) + ( y2 * cos_rot );

                  x2 = ( int ) xp;
                  y2 = ( int ) yp;
            }
            x2 /= scaleFac;
            y2 /= scaleFac;

            x2 += r.x;
            y2 += r.y;

            if ( vIdx < MAX_POINTS )
            {
                  PointArray[vIdx].x =x2;
                  PointArray[vIdx].y =y2;
            }

            ++vIdx;

            while ( *str++ != ',' );               // specs: could repeat x,y,x, ..
            while ( *str   != ',' && *str != ';' ) str++;
            if ( *str == ',' ) str++;
            if ( 0 == strncmp ( str,";PD",3 ) ) str += 3;
      }
      while ( *str != ';' );
}
}
while ( 0 != strncmp ( str,";PM2",4 ) );

// exit polygon mode
str++;
while ( *str != ';' ) ++str;


} // PM
*/

/*
// Fill Polygon
else INST ( FP )
      if ( tessObj )
      {
            wxColour color ( newColor->R, newColor->G, newColor->B );
            wxBrush *pthisbrush = wxTheBrushList->FindOrCreateBrush ( color, wxSOLID );

            pdc->SetBrush ( *pthisbrush );
            pdc->DrawPolygon ( vIdx, PointArray );
            inBegEnd = FALSE;
      }
}

// Symbol Call    --never used
else INST ( SC )
{
      wxString msg = _T ( "SEQuencer:_renderHPGL(): fixme SC instruction not implemented " );
      LogMessageOnce ( msg );
}
}
*/

    ++str;

    } /* while */

    return true;
}


unsigned char *GetRGBA_Array(wxImage &Image)
{
      //    Get the glRGBA format data from the wxImage
      unsigned char *d = Image.GetData();
      unsigned char *a = Image.GetAlpha();

      unsigned char mr, mg, mb;
      if(!Image.GetOrFindMaskColour(&mr, &mg, &mb) && !a)
            printf("trying to use mask to draw a bitmap without alpha or mask\n");

      int w = Image.GetWidth();
      int h = Image.GetHeight();

      unsigned char *e = (unsigned char *)malloc(w * h * 4);
      for(int y=0; y<h; y++)
      {
            for(int x=0; x<w; x++)
            {
                  unsigned char r, g, b;
                  int off = (y*Image.GetWidth()+x);
                  r = d[off*3 + 0];
                  g = d[off*3 + 1];
                  b = d[off*3 + 2];

                  e[off*4 + 0] = r;
                  e[off*4 + 1] = g;
                  e[off*4 + 2] = b;

                  e[off*4 + 3] = a ? a[off] :
                             ((r==mr)&&(g==mg)&&(b==mb) ? 0 : 255);
            }
      }

      return e;
}




bool s52plib::RenderHPGL ( ObjRazRules *rzRules,  Rule *prule, wxDC *pdc, wxPoint &r, ViewPort *vp, float rot_angle )
{
      float fsf = 100 / canvas_pix_per_mm;

      int width  = prule->pos.symb.bnbox_x.SBXC + prule->pos.symb.bnbox_w.SYHL;
      width *= 4;           // Grow the drawing bitmap to allow for rotation of symbols with highly offset pivot points
      width = ( int ) ( width/fsf );

      int height = prule->pos.symb.bnbox_y.SBXR + prule->pos.symb.bnbox_h.SYVL;
      height *= 4;
      height = ( int ) ( height/fsf );

      int pivot_x = prule->pos.symb.pivot_x.SYCL;
      int pivot_y = prule->pos.symb.pivot_y.SYRW;

      //Instantiate the symbol if necessary
      if ( ( prule->pixelPtr == NULL ) || ( prule->parm1 != m_colortable_index ) )
      {
             // delete any old private data
            if ( prule->parm0 && prule->pixelPtr )
            {
                  switch(prule->parm0)
                  {
                        case ID_wxBitmap:
                        {
                              wxBitmap *pbm = ( wxBitmap * ) ( prule->pixelPtr );
                              delete pbm;
                              prule->pixelPtr = NULL;
                              prule->parm0 = 0;
                              break;
                        }
                        case ID_RGBA:
                        {
                              unsigned char *p = ( unsigned char * ) ( prule->pixelPtr );
                              free ( p );
                              prule->pixelPtr = NULL;
                              prule->parm0 = 0;
                              break;
                        }
                  }
            }

//            if((width == 0) || (height == 0))
//                  int yyp = 4;

            wxBitmap *pbm = new wxBitmap ( width, height );
            wxMemoryDC mdc;
            mdc.SelectObject ( *pbm );
            mdc.SetBackground ( wxBrush ( m_unused_wxColor ) );
            mdc.Clear();

            char *str = prule->vector.LVCT;
            char *col = prule->colRef.LCRF;
            wxPoint pivot ( pivot_x, pivot_y );
            wxPoint r0 ( ( int ) ( pivot_x/fsf ), ( int ) ( pivot_y/fsf ) );
            RenderHPGLtoDC ( str, col, &mdc, r0, pivot, ( double ) rot_angle );

            int bm_width  = ( mdc.MaxX() - mdc.MinX() ) + 1;
            int bm_height = ( mdc.MaxY() - mdc.MinY() ) + 1;
            int bm_orgx = wxMax ( 0, mdc.MinX() );
            int bm_orgy = wxMax ( 0, mdc.MinY() );

            //      Pre-clip the sub-bitmap to avoid assert errors
            if ( ( bm_height + bm_orgy ) > height )
                  bm_height = height - bm_orgy;
            if ( ( bm_width + bm_orgx ) > width )
                  bm_width = width - bm_orgx;

            //   TODO Remove Debug
//                mdc.SetBrush(*wxTRANSPARENT_BRUSH);
//                mdc.SetPen(*wxGREEN_PEN);
//                mdc.DrawRectangle(bm_orgx, bm_orgy, bm_width, bm_height);

            mdc.SelectObject ( wxNullBitmap );

            //          Get smallest containing bitmap
            wxBitmap *sbm = new wxBitmap ( pbm->GetSubBitmap ( wxRect ( bm_orgx, bm_orgy, bm_width, bm_height ) ) );

            delete pbm;

            //      Make the mask
            wxMask *pmask = new wxMask ( *sbm, m_unused_wxColor);
            //      Associate the mask with the bitmap
            sbm->SetMask ( pmask );

            if(!pdc)          // opengl
            {
            //    Create a byte data accessible wxImage from the wxBitmap
                  wxImage Image = sbm->ConvertToImage();
                  delete sbm;                               // done with this

            //    Get the glRGBA format data from the wxImage
                  unsigned char *e = GetRGBA_Array(Image);

            //      Save the byte array ptr and aux parms in the rule
                  prule->pixelPtr = e;
                  prule->parm0 = ID_RGBA;
                  prule->parm1 = m_colortable_index;
                  prule->parm2 = bm_orgx- ( int ) ( pivot_x/fsf );
                  prule->parm3 = bm_orgy- ( int ) ( pivot_y/fsf );
                  prule->parm4 = ( int ) rot_angle;
                  prule->parm5 = Image.GetWidth();
                  prule->parm6 = Image.GetHeight();
            }
            else        // wxDC render
            {
            //      Save the bitmap ptr and aux parms in the rule
                  prule->pixelPtr = sbm;
                  prule->parm0 = ID_wxBitmap;
                  prule->parm1 = m_colortable_index;
                  prule->parm2 = bm_orgx- ( int ) ( pivot_x/fsf );
                  prule->parm3 = bm_orgy- ( int ) ( pivot_y/fsf );
                  prule->parm4 = ( int ) rot_angle;
                  prule->parm5 = bm_width;
                  prule->parm6 = bm_height;
            }


      }               // instantiation

      //    If the rotation angle of the cached symbol is not equal to the request,
      //    then render the symbol directly in HPGL
      if ( ( int ) rot_angle != prule->parm4 )
      {
            if(pdc)
            {
                  char *str = prule->vector.LVCT;
                  char *col = prule->colRef.LCRF;
                  wxPoint pivot ( prule->pos.line.pivot_x.LICL, prule->pos.line.pivot_y.LIRW );
                  RenderHPGLtoDC ( str, col, pdc, r, pivot, ( double ) rot_angle );
            }
            else
            {
                  wxBitmap *pbm = new wxBitmap ( width, height );
                  wxMemoryDC mdc;
                  mdc.SelectObject ( *pbm );
                  mdc.SetBackground ( wxBrush ( m_unused_wxColor ) );
                  mdc.Clear();

                  char *str = prule->vector.LVCT;
                  char *col = prule->colRef.LCRF;
                  wxPoint pivot ( pivot_x, pivot_y );
                  wxPoint r0 ( ( int ) ( pivot_x/fsf ), ( int ) ( pivot_y/fsf ) );
                  RenderHPGLtoDC ( str, col, &mdc, r0, pivot, ( double ) rot_angle );

                  int bm_width  = ( mdc.MaxX() - mdc.MinX() ) + 1;
                  int bm_height = ( mdc.MaxY() - mdc.MinY() ) + 1;
                  int bm_orgx = wxMax ( 0, mdc.MinX() );
                  int bm_orgy = wxMax ( 0, mdc.MinY() );

            //      Pre-clip the sub-bitmap to avoid assert errors
                  if ( ( bm_height + bm_orgy ) > height )
                        bm_height = height - bm_orgy;
                  if ( ( bm_width + bm_orgx ) > width )
                        bm_width = width - bm_orgx;

                  mdc.SelectObject ( wxNullBitmap );

                  //          Get smallest containing bitmap
                  wxBitmap *sbm = new wxBitmap ( pbm->GetSubBitmap ( wxRect ( bm_orgx, bm_orgy, bm_width, bm_height ) ) );
                  delete pbm;

                  //      Make the mask
                  wxMask *pmask = new wxMask ( *sbm, m_unused_wxColor);
                  //      Associate the mask with the bitmap
                  sbm->SetMask ( pmask );

                  //    Create a byte data accessible wxImage from the wxBitmap
                  wxImage Image = sbm->ConvertToImage();
                  delete sbm;                               // done with this

                  unsigned char *e = GetRGBA_Array(Image);

                  //   And Render
                  glColor4f(1, 1, 1, 1);

                  glEnable(GL_BLEND);
                  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                  glRasterPos2i(r.x + (bm_orgx- ( int ) ( pivot_x/fsf )), r.y + (bm_orgy- ( int ) ( pivot_y/fsf )));
                  glPixelZoom(1, -1);
                  glDrawPixels(Image.GetWidth(), Image.GetHeight(), GL_RGBA, GL_UNSIGNED_BYTE, e);
                  glPixelZoom(1, 1);
                  glDisable(GL_BLEND);

                  free (e);

            }

            return true;
      }


      //        Get the bounding box for the as-drawn symbol
      int b_width  =  prule->parm5;
      int b_height =  prule->parm6;

      wxBoundingBox symbox;
      double plat, plon;

      rzRules->chart->GetPixPoint ( r.x + prule->parm2, r.y + prule->parm3 + b_height, &plat, &plon, vp );
      symbox.SetMin ( plon, plat );

      rzRules->chart->GetPixPoint ( r.x + prule->parm2 + b_width,  r.y + prule->parm3, &plat, &plon, vp );
      symbox.SetMax ( plon, plat );


/*
      //  Special case for GEO_AREA objects with centred symbols
      if ( rzRules->obj->Primitive_type == GEO_AREA )
      {
            if ( rzRules->obj->BBObj.Intersect ( symbox, 0 ) == _OUT ) // Symbol is wholly outside base object
                  return true;
      }
*/


     //      Now render the symbol
      if(!m_pdc)          // opengl
      {
            glColor4f(1, 1, 1, 1);

            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glRasterPos2i(r.x + prule->parm2, r.y + prule->parm3);
            glPixelZoom(1, -1);
            glDrawPixels(b_width, b_height, GL_RGBA, GL_UNSIGNED_BYTE, prule->pixelPtr);
            glPixelZoom(1, 1);
            glDisable(GL_BLEND);
      }
      else
      {
            //      Get the bitmap into a memory dc
            wxMemoryDC mdc;
            mdc.SelectObject ( ( wxBitmap & ) ( * ( ( wxBitmap * ) ( prule->pixelPtr ) ) ) );

            //      Blit  it into the target dc
            pdc->Blit ( r.x + prule->parm2, r.y + prule->parm3, b_width, b_height, &mdc, 0, 0, wxCOPY,  true );

      // Debug
      //    pdc->SetPen(wxPen(*wxGREEN, 1));
      //    pdc->SetBrush(wxBrush(*wxGREEN, wxTRANSPARENT));
      //    pdc->DrawRectangle(r.x + prule->parm2, r.y + prule->parm3, b_width, b_height);

            mdc.SelectObject ( wxNullBitmap );
      }

      //  Update the object Bounding box
      //  so that subsequent drawing operations will redraw the item fully
//      if ( rzRules->obj->Primitive_type == GEO_POINT )
      {
            if(rzRules->obj->bBBObj_valid)
                  rzRules->obj->BBObj.Expand ( symbox );
            else
            {
                  rzRules->obj->BBObj = symbox;
                  rzRules->obj->bBBObj_valid = true;
            }

      }

      //Debug
/*
      wxPoint ra, rb;
      ra = vp->GetPixFromLL(rzRules->obj->BBObj.GetMaxY(), rzRules->obj->BBObj.GetMinX() );
      rb = vp->GetPixFromLL(rzRules->obj->BBObj.GetMinY(), rzRules->obj->BBObj.GetMaxX() );
      pdc->SetPen(wxPen(*wxGREEN, 1));
      pdc->SetBrush(wxBrush(*wxGREEN, wxTRANSPARENT));
      pdc->DrawRectangle(ra.x, ra.y, rb.x - ra.x, rb.y - ra.y);
*/

      return true;
}


//-----------------------------------------------------------------------------------------
//      Instantiate a Symbol or Pattern stored as XBM ascii in a rule
//      Producing a wxImage
//-----------------------------------------------------------------------------------------
wxImage s52plib::RuleXBMToImage ( Rule *prule )
{
      //      Decode the color definitions
      wxArrayPtrVoid *pColorArray = new wxArrayPtrVoid;

      /*
          wxString cstr(*prule->colRef.SCRF);
          unsigned int i = 0;

          char colname[6];
          while(i < (unsigned int)cstr.Len())
          {
                  i++;
                  wxString thiscolor = cstr(i, 5);

                  strncpy(colname, thiscolor.mb_str(), 5);
                  colname[5]=0;
                  color *pColor =  S52_getColor(colname);

                  pColorArray->Add((void *) pColor);

                  i+=5;
          }
      */
      int i = 0;
      char *cstr = prule->colRef.SCRF;

      char colname[6];
      int nl = strlen ( cstr );

      while ( i < nl )
      {
            i++;

            strncpy ( colname, &cstr[i], 5 );
            colname[5]=0;
            S52color *pColor =  S52_getColor ( colname );

            pColorArray->Add ( ( void * ) pColor );

            i+=5;
      }



      //      Get geometry
      int width  = prule->pos.line.bnbox_w.SYHL;
      int height = prule->pos.line.bnbox_h.SYVL;

      wxString gstr ( *prule->bitmap.SBTM );                  // the bit array

//    wxImage *pImage = new wxImage(width, height );          // put the bits here temporarily
      wxImage Image ( width, height );

      for ( int iy = 0 ; iy < height ; iy++ )
      {
            wxString thisrow = gstr ( iy * width, width );          // extract a row

            for ( int ix = 0 ; ix < width ; ix++ )
            {
                  int cref = ( int ) ( thisrow[ix] - 'A' );       // make an index
                  if ( cref >= 0 )
                  {
                        S52color *pthisbitcolor = ( S52color * ) ( pColorArray->Item ( cref ) );
                        Image.SetRGB ( ix, iy, pthisbitcolor->R, pthisbitcolor->G, pthisbitcolor->B );
                  }
                  else
                  {
                        Image.SetRGB ( ix, iy, m_unused_color.R, m_unused_color.G, m_unused_color.B );
                  }

            }
      }

      pColorArray->Clear();
      delete pColorArray;

      return Image;
}




//
//      Render Raster Symbol
//      Symbol is instantiated as a bitmap the first time it is needed
//      and re-built on color scheme change
//
bool s52plib::RenderRasterSymbol ( ObjRazRules *rzRules, Rule *prule, wxDC *pdc, wxPoint &r, ViewPort *vp, float rot_angle )
{

//        int width  = prule->pos.line.bnbox_w.SYHL;
//        int height = prule->pos.line.bnbox_h.SYVL;

      int pivot_x = prule->pos.line.pivot_x.SYCL;
      int pivot_y = prule->pos.line.pivot_y.SYRW;

      //Instantiate the symbol if necessary
      if ( ( prule->pixelPtr == NULL ) || ( prule->parm1 != m_colortable_index ) )
      {
            wxImage Image = RuleXBMToImage ( prule );

            // delete any old private data
            if ( prule->parm0 && prule->pixelPtr )
            {
                  switch(prule->parm0)
                  {
                        case ID_wxBitmap:
                        {
                              wxBitmap *pbm = ( wxBitmap * ) ( prule->pixelPtr );
                              delete pbm;
                              prule->pixelPtr = NULL;
                              prule->parm0 = 0;
                              break;
                        }
                        case ID_RGBA:
                        {
                              unsigned char *p = ( unsigned char * ) ( prule->pixelPtr );
                              free ( p );
                              prule->pixelPtr = NULL;
                              prule->parm0 = 0;
                              break;
                        }
                  }
            }

            if(!pdc)          // opengl
            {
            //    Get the glRGBA format data from the wxImage
                  unsigned char *d = Image.GetData();
                  unsigned char *a = Image.GetAlpha();

                  Image.SetMaskColour(m_unused_wxColor.Red(), m_unused_wxColor.Green(), m_unused_wxColor.Blue());
                  unsigned char mr, mg, mb;
                  if(!Image.GetOrFindMaskColour(&mr, &mg, &mb) && !a)
                        printf("trying to use mask to draw a bitmap without alpha or mask\n");

                  int w = Image.GetWidth();
                  int h = Image.GetHeight();

                  unsigned char *e = (unsigned char *)malloc(w * h * 4);
                  for(int y=0; y<h; y++)
                  {
                        for(int x=0; x<w; x++)
                        {
                              unsigned char r, g, b;
                              int off = (y*Image.GetWidth()+x);
                              r = d[off*3 + 0];
                              g = d[off*3 + 1];
                              b = d[off*3 + 2];

                              e[off*4 + 0] = r;
                              e[off*4 + 1] = g;
                              e[off*4 + 2] = b;

                              e[off*4 + 3] = a ? a[off] :
                                    ((r==mr)&&(g==mg)&&(b==mb) ? 0 : 255);
                        }
                  }

            //      Save the bitmap ptr and aux parms in the rule
                  prule->pixelPtr = e;
                  prule->parm0 = ID_RGBA;
                  prule->parm1 = m_colortable_index;
                  prule->parm2 = w;
                  prule->parm3 = h;
            }
            else
            {
            //      Make the wxBitmap

            //TODO Study this problem, use conditional build?
#ifdef __WXMSWF__
//      On some versions of wxMSW, on Windows XP, conversion from wxImage to wxBitmap fails at the ::CreateDIBitmap() call
//      unless a "compatible" dc is provided.  Why??
//      As a workaround, just make a simple wxDC for temporary use

                  wxMemoryDC dwxdc;
                  wxBitmap *pbm = new wxBitmap ( Image, dwxdc );
#else
                  wxBitmap *pbm = new wxBitmap ( Image );
#endif
                  //      Make the mask
                  wxMask *pmask = new wxMask ( *pbm, m_unused_wxColor);
                  //      Associate the mask with the bitmap
                  pbm->SetMask ( pmask );


            //      Save the bitmap ptr and aux parms in the rule
                  prule->pixelPtr = pbm;
                  prule->parm0 = ID_wxBitmap;
                  prule->parm1 = m_colortable_index;
            }
      }               // instantiation


      //        Get the bounding box for the to-be-drawn symbol
      int b_width, b_height;

      if(!pdc)          // opengl
      {
            b_width  = prule->parm2;
            b_height = prule->parm3;
      }
      else
      {
            b_width  = ( ( wxBitmap * ) ( prule->pixelPtr ) )->GetWidth();
            b_height = ( ( wxBitmap * ) ( prule->pixelPtr ) )->GetHeight();
      }

      wxBoundingBox symbox;
      double plat, plon;

      rzRules->chart->GetPixPoint ( r.x -pivot_x, r.y - pivot_y + b_height, &plat, &plon, vp );
      symbox.SetMin ( plon, plat );

      rzRules->chart->GetPixPoint ( r.x - pivot_x + b_width,  r.y - pivot_y, &plat, &plon, vp );
      symbox.SetMax ( plon, plat );



      //  Special case for GEO_AREA objects with centred symbols
      if ( rzRules->obj->Primitive_type == GEO_AREA )
      {
            if ( rzRules->obj->BBObj.Intersect ( symbox, 0 ) != _IN ) // Symbol is wholly outside base object
                  return true;
      }


      //      Now render the symbol

      if(!pdc)          // opengl
      {
            glColor4f(1, 1, 1, 1);

            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glRasterPos2i(r.x - pivot_x, r.y - pivot_y);
            glPixelZoom(1, -1);
            glDrawPixels(b_width, b_height, GL_RGBA, GL_UNSIGNED_BYTE, prule->pixelPtr);
            glPixelZoom(1, 1);
            glDisable(GL_BLEND);

      }
      else
      {
            //      Get the bitmap into a memory dc
            wxMemoryDC mdc;

            mdc.SelectObject ( ( wxBitmap & ) ( * ( ( wxBitmap * ) ( prule->pixelPtr ) ) ) );

            //      Blit it into the target dc
            pdc->Blit ( r.x - pivot_x, r.y - pivot_y, b_width, b_height, &mdc, 0, 0, wxCOPY,  true );
// Debug
//    pdc->SetPen(wxPen(*wxGREEN, 1));
//    pdc->SetBrush(wxBrush(*wxGREEN, wxTRANSPARENT));
//    pdc->DrawRectangle(r.x - pivot_x, r.y - pivot_y, b_width, b_height);


            mdc.SelectObject ( wxNullBitmap );
      }

      //  Update the object Bounding box
      //  so that subsequent drawing operations will redraw the item fully
      //  We expand the object's BBox to account for objects rendered by multiple symbols, such as SOUNGD.
      //  so that expansions are cumulative.
      if ( rzRules->obj->Primitive_type == GEO_POINT )
      {
            if(rzRules->obj->bBBObj_valid)
                  rzRules->obj->BBObj.Expand ( symbox );
            else
            {
                  rzRules->obj->BBObj = symbox;
                  rzRules->obj->bBBObj_valid = true;
            }
      }

      return true;
}



// SYmbol
int s52plib::RenderSY ( ObjRazRules *rzRules, Rules *rules, ViewPort *vp )
{
      float angle = 0;
      double orient;


      //      Debug
//    if(!strncmp(rzRules->obj->FeatureName, "TOPMAR", 6))
//          int ggk = 3;

      if ( rules->razRule != NULL )
      {
            if ( rules->INSTstr[8] == ',' )              // supplementary parameter assumed to be angle, seen in LIGHTSXX
            {
                  char sangle[10];
                  int cp = 0;
                  while ( rules->INSTstr[cp + 9] && ( rules->INSTstr[cp + 9] != ')' ) )
                  {
                        sangle[cp] = rules->INSTstr[cp + 9];
                        cp++;
                  }
                  sangle[cp] = 0;
                  int angle_i = atoi ( sangle );
                  angle = angle_i;
            }

            if ( GetDoubleAttr ( rzRules->obj, "ORIENT", orient ) )       // overriding any LIGHTSXX angle, probably TSSLPT
            {
                  angle = orient;
                  if(strncmp(rzRules->obj->FeatureName, "LIGHTS", 6) == 0)
                  {
                        angle += 180;
                        if ( angle > 360)
                               angle -= 360;
                  }
            }

            //  Render symbol at object's x/y
            wxPoint r, r1;
            rzRules->chart->GetPointPix ( rzRules, rzRules->obj->y, rzRules->obj->x, &r );


            //  Render a raster or vector symbol, as specified by LUP rules
            if ( rules->razRule->definition.SYDF == 'V' )
                  RenderHPGL ( rzRules, rules->razRule, m_pdc, r, vp, angle );

            else if ( rules->razRule->definition.SYDF == 'R' )
                  RenderRasterSymbol ( rzRules, rules->razRule, m_pdc, r, vp, angle );

      }

      return 0;

}

// Line Simple Style
int s52plib::RenderLS ( ObjRazRules *rzRules, Rules *rules, ViewPort *vp )
{
      //    Debug
//      if(rzRules->obj->Index != 5108)
//            return 1;

      wxPoint         *ptp;
      int     npt;
      S52color   *c;
      int     w;

      char *str = ( char* ) rules->INSTstr;
      c = S52_getColor ( str+7 );             // Colour
      w = atoi ( str+5 );                     // Width

      if(m_pdc)         //DC mode
      {
            wxColour color ( c->R, c->G, c->B );
            int style = wxSOLID;                    // Style default
            if ( !strncmp ( str, "DASH", 4 ) )
                  style = wxSHORT_DASH;

      // Windows98 cannot reliably draw dashed lines
            #ifdef __WXMSW__
            int verMaj, verMin;
            wxGetOsVersion ( &verMaj, &verMin );
            if ( verMaj == 5 && verMin == 0 )             // windows 98
                  style = wxSOLID;
            #endif

            wxPen *pthispen = wxThePenList->FindOrCreatePen ( color, w, style );
            m_pdc->SetPen ( *pthispen );
      }
      else              // OpenGL mode
      {
            glPushAttrib(GL_COLOR_BUFFER_BIT | GL_LINE_BIT | GL_HINT_BIT | GL_ENABLE_BIT);      //Save state
            glColor3ub( c->R, c->G, c->B );
            glLineWidth(w);

            if ( !strncmp ( str, "DASH", 4 ) )
            {
                  glLineStipple(1, 0x3F3F);
                  glEnable(GL_LINE_STIPPLE);
            }

      }

      //    Get a true pixel clipping/bounding box from the vp
      wxPoint pbb = vp->GetPixFromLL(vp->clat, vp->clon);
      int xmin_ = pbb.x - vp->rv_rect.width/2;
      int xmax_ = xmin_ + vp->rv_rect.width;
      int ymin_ = pbb.y - vp->rv_rect.height/2;
      int ymax_ = ymin_ + vp->rv_rect.height;

      int x0, y0, x1, y1;

      //  Get the current display priority from the LUP
      int priority_current = rzRules->LUP->DPRI - '0';          //TODO fix this hack by putting priority into object during _insertRules

      if ( rzRules->obj->m_n_lsindex )
      {
            VE_Hash &ve_hash = rzRules->chart->Get_ve_hash();
            VC_Hash &vc_hash = rzRules->chart->Get_vc_hash();

            int nls_max;
            if(rzRules->obj->m_n_edge_max_points > 0)             // size has been precalculated on SENC load
                  nls_max = rzRules->obj->m_n_edge_max_points;
            else
            {
            //  Calculate max malloc size required
                  nls_max = -1;
                  int *index_run_x = rzRules->obj->m_lsindex_array;
                  for ( int imseg=0 ; imseg < rzRules->obj->m_n_lsindex ; imseg++ )
                  {
                        index_run_x++;      //Skip cNode
                  //  Get the edge
                        int enode = *index_run_x;
                        VE_Element *pedge = ve_hash[enode];
                        if ( pedge->nCount > nls_max )
                              nls_max = pedge->nCount;
                        index_run_x += 2;
                  }
                  rzRules->obj->m_n_edge_max_points = nls_max;    // Got it, cache for next time
            }


            //  Allocate some storage for converted points
            wxPoint *ptp = ( wxPoint * ) malloc ( ( nls_max + 2 ) * sizeof ( wxPoint ) );   // + 2 allows for end nodes

            int *index_run;
            double *ppt;
            double easting, northing;

            wxPoint pra(0,0);
            VC_Element *pnode;

            for ( int iseg=0 ; iseg < rzRules->obj->m_n_lsindex ; iseg++ )
            {
                  int seg_index = iseg * 3;
                  index_run = &rzRules->obj->m_lsindex_array[seg_index];

                  //  Get first connected node
                  int inode = *index_run++;
                  if (( inode >= 0 ))
                  {
                        pnode = vc_hash[inode];
                        if(pnode)
                        {
                              ppt = pnode->pPoint;
                              easting = *ppt++;
                              northing = *ppt;
                              rzRules->chart->GetPointPix ( rzRules, ( float ) northing, ( float ) easting, &pra );
                        }
                        ptp[0] = pra;                     // insert beginning node
                  }

                  //  Get the edge
                  int enode = *index_run++;
                  VE_Element *pedge;
                  pedge = ve_hash[enode];

                  //  Here we decide to draw or not based on the highest priority seen for this segment
                  //  That is, if this segment is going to be drawn at a higher priority later, then "continue", and don't draw it here.
                  if ( pedge->max_priority != priority_current )
                        continue;

                  int nls = pedge->nCount;

                  ppt = pedge->pPoints;
                  for ( int ip = 0 ; ip < nls  ; ip++ )
                  {
                        easting = *ppt++;
                        northing = *ppt++;
                        rzRules->chart->GetPointPix ( rzRules, ( float ) northing, ( float ) easting, &ptp[ip + 1] );
                  }

                  //  Get last connected node
                  int jnode = *index_run++;
                  if (( jnode >= 0 ))
                  {
                        pnode = vc_hash[jnode];
                        if(pnode)
                        {
                              ppt = pnode->pPoint;
                              easting = *ppt++;
                              northing = *ppt;
                              rzRules->chart->GetPointPix ( rzRules, ( float ) northing, ( float ) easting, &pra );
                        }
                        ptp[nls + 1] = pra;                     // insert ending node
                  }

                  int istart, ndraw;

                  if ( ( inode >= 0 ) && ( jnode >= 0 ) )
                  {
                        istart = 0;
                        ndraw = nls+1;
                  }
                  else
                  {
                        istart = 1;
                        ndraw = nls;
                  }


                  //        Draw the edge as point-to-point
                  for ( int ipc=istart ; ipc < ndraw ; ipc++ )
                  {
                        x0 = ptp[ipc].x;
                        y0 = ptp[ipc].y;
                        x1 = ptp[ipc+1].x;
                        y1 = ptp[ipc+1].y;

                        // Do not draw null segments
                        if ( ( x0 == x1 ) && ( y0 == y1 ) )
                              continue;

                        ClipResult res = cohen_sutherland_line_clip_i ( &x0, &y0, &x1, &y1,
                                         xmin_, xmax_, ymin_, ymax_ );

                        if ( res != Invisible )
                        {
                              if(m_pdc)
                                    m_pdc->DrawLine ( x0,y0,x1,y1 );
                              else
                              {
                                    glBegin(GL_LINES);
                                    glVertex2i(x0, y0);
                                    glVertex2i(x1, y1);
                                    glEnd();

                              }
                        }
                  }
            }
            free ( ptp );
      }


      else if ( rzRules->obj->pPolyTessGeo )
      {
            if(!rzRules->obj->pPolyTessGeo->IsOk())               // perform deferred tesselation
                  rzRules->obj->pPolyTessGeo->BuildTessGL();

            PolyTriGroup *pptg = rzRules->obj->pPolyTessGeo->Get_PolyTriGroup_head();

            float *ppolygeo = pptg->pgroup_geom;

            int ctr_offset = 0;
            for ( int ic = 0; ic < pptg->nContours ; ic++ )
            {

                  npt = pptg->pn_vertex[ic];
                  wxPoint *ptp = ( wxPoint * ) malloc ( ( npt + 1 ) * sizeof ( wxPoint ) );
                  wxPoint *pr = ptp;

                  float *pf = &ppolygeo[ctr_offset];
                  for ( int ip=0 ; ip < npt ; ip++ )
                  {
                        float plon = *pf++;
                        float plat = *pf++;

                        rzRules->chart->GetPointPix ( rzRules, plat, plon, pr );
                        pr++;
                  }
                  float plon = ppolygeo[ ctr_offset];             // close the polyline
                  float plat = ppolygeo[ ctr_offset + 1];
                  rzRules->chart->GetPointPix ( rzRules, plat, plon, pr );


                  for ( int ipc=0 ; ipc < npt ; ipc++ )
                  {
                        x0 = ptp[ipc].x;
                        y0 = ptp[ipc].y;
                        x1 = ptp[ipc+1].x;
                        y1 = ptp[ipc+1].y;

                        // Do not draw null segments
                        if ( ( x0 == x1 ) && ( y0 == y1 ) )
                              continue;

                        ClipResult res = cohen_sutherland_line_clip_i ( &x0, &y0, &x1, &y1,
                                         xmin_, xmax_, ymin_, ymax_ );

                        if ( res != Invisible )
                        {
                              if(m_pdc)
                                    m_pdc->DrawLine ( x0,y0,x1,y1 );
                              else
                              {
                                    glBegin(GL_LINES);
                                    glVertex2i(x0, y0);
                                    glVertex2i(x1, y1);
                                    glEnd();
                              }
                        }
                  }

//                    pdc->DrawLines(npt + 1, ptp);
                  free ( ptp );
                  ctr_offset += npt*2;
            }
      }

      else if ( rzRules->obj->pPolyTrapGeo )
      {
            if(!rzRules->obj->pPolyTrapGeo->IsOk())
                  rzRules->obj->pPolyTrapGeo->BuildTess();

            PolyTrapGroup *pptg = rzRules->obj->pPolyTrapGeo->Get_PolyTrapGroup_head();

            wxPoint2DDouble *ppolygeo = pptg->ptrapgroup_geom;

            int ctr_offset = 0;
            for ( int ic = 0; ic < pptg->nContours ; ic++ )
            {

                  npt = pptg->pn_vertex[ic];
                  wxPoint *ptp = ( wxPoint * ) malloc ( ( npt + 1 ) * sizeof ( wxPoint ) );
                  wxPoint *pr = ptp;
                  /*
                                      double *pf = &ppolygeo[ctr_offset];
                                      for ( int ip=0 ; ip < npt ; ip++ )
                                      {
                                            double plon = *pf++;
                                            double plat = *pf++;

                                            rzRules->chart->GetPointPix ( rzRules, plat, plon, pr );
                                            pr++;
                                      }
                                      double plon = ppolygeo[ ctr_offset];             // close the polyline
                                      double plat = ppolygeo[ ctr_offset + 1];
                                      rzRules->chart->GetPointPix ( rzRules, plat, plon, pr );
                  */
                  for ( int ip=0 ; ip < npt ; ip++, pr++ )
                        rzRules->chart->GetPointPix ( rzRules,  ppolygeo[ctr_offset + ip].m_y, ppolygeo[ctr_offset + ip].m_x, pr );

                  //  Close polyline
                  rzRules->chart->GetPointPix ( rzRules,  ppolygeo[ctr_offset].m_y, ppolygeo[ctr_offset].m_x, pr );


                  for ( int ipc=0 ; ipc < npt ; ipc++ )
                  {
                        x0 = ptp[ipc].x;
                        y0 = ptp[ipc].y;
                        x1 = ptp[ipc+1].x;
                        y1 = ptp[ipc+1].y;

                        // Do not draw null segments
                        if ( ( x0 == x1 ) && ( y0 == y1 ) )
                              continue;

                        ClipResult res = cohen_sutherland_line_clip_i ( &x0, &y0, &x1, &y1,
                                         xmin_, xmax_, ymin_, ymax_ );

                        if ( ( res != Invisible ) )
                              m_pdc->DrawLine ( x0,y0,x1,y1 );

                  }

                  free ( ptp );
                  ctr_offset += ( npt + 1 ) *2;
            }
      }

      else if ( rzRules->obj->geoPt )
      {
            pt *ppt = rzRules->obj->geoPt;
            npt = rzRules->obj->npt;
            ptp = ( wxPoint * ) malloc ( npt * sizeof ( wxPoint ) );
            wxPoint *pr = ptp;
            wxPoint p;
            for ( int ip=0 ; ip<npt ; ip++ )
            {
                  float plat = ppt->y;
                  float plon = ppt->x;

                  rzRules->chart->GetPointPix ( rzRules, plat, plon, &p );

                  *pr = p;

                  pr++;
                  ppt++;
            }

            if(m_pdc)
                  m_pdc->DrawLines ( npt, ptp );

            else
            {
                  glBegin(GL_LINE_STRIP);       // or GL_LINE_LOOP?????
                  for ( int ip=0 ; ip<npt ; ip++ )
                        glVertex2i(ptp[ip].x, ptp[ip].y);
                  glEnd();
            }

            free ( ptp );
      }

      if(!m_pdc)
            glPopAttrib();

      return 1;
}


// Line Complex
int s52plib::RenderLC ( ObjRazRules *rzRules, Rules *rules, ViewPort *vp )
{
      wxPoint   *ptp;
      int       npt;
      wxPoint   r;

//    Debug
//        if(rzRules->obj->Index == 649)
//             int oop = 4;

      int isym_len = rules->razRule->pos.line.bnbox_w.SYHL;
      float sym_len = isym_len * canvas_pix_per_mm / 100;
      float sym_factor = 1.0 ; ///1.50;                        // gives nicer effect


//      Create a color for drawing adjustments outside of HPGL renderer
      char *tcolptr = rules->razRule->colRef.LCRF;
      S52color *c = S52_getColor ( tcolptr + 1 );       // +1 skips "n" in HPGL SPn format
      int w = 1;        // arbitrary width
      wxColour color ( c->R, c->G, c->B );

      //  Get the current display priority from the LUP
      int priority_current = rzRules->LUP->DPRI - '0';          //TODO fix this hack by putting priority into object during _insertRules

      if ( rzRules->obj->m_n_lsindex )
      {
            VE_Hash &ve_hash = rzRules->chart->Get_ve_hash();
            VC_Hash &vc_hash = rzRules->chart->Get_vc_hash();

            int nls_max;
            if(rzRules->obj->m_n_edge_max_points > 0)             // size has been precalculated on SENC load
                  nls_max = rzRules->obj->m_n_edge_max_points;
            else
            {
            //  Calculate max malloc size required
                  nls_max = -1;
                  int *index_run_x = rzRules->obj->m_lsindex_array;
                  for ( int imseg=0 ; imseg < rzRules->obj->m_n_lsindex ; imseg++ )
                  {
                        index_run_x++;      //Skip cNode
                  //  Get the edge
                        int enode = *index_run_x;
                        VE_Element *pedge = ve_hash[enode];
                        if ( pedge->nCount > nls_max )
                              nls_max = pedge->nCount;
                        index_run_x += 2;
                  }
                  rzRules->obj->m_n_edge_max_points = nls_max;    // Got it, cache for next time
            }


            //  Allocate some storage for converted points
            wxPoint *ptp = ( wxPoint * ) malloc ( ( nls_max + 2 ) * sizeof ( wxPoint ) );   // + 2 allows for end nodes

            int *index_run;
            double *ppt;
            double easting, northing;
            wxPoint pra(0,0);
            VC_Element *pnode;

            for ( int iseg=0 ; iseg < rzRules->obj->m_n_lsindex ; iseg++ )
            {
                  int seg_index = iseg * 3;
                  index_run = &rzRules->obj->m_lsindex_array[seg_index];

                  //  Get first connected node
                  int inode = *index_run++;
                  if ( inode >= 0 )
                  {
                        pnode = vc_hash[inode];
                        if(pnode)
                        {
                              ppt = pnode->pPoint;
                              easting = *ppt++;
                              northing = *ppt;
                              rzRules->chart->GetPointPix ( rzRules, ( float ) northing, ( float ) easting, &pra );
                        }
                        ptp[0] = pra;                     // insert beginning node
                  }

                  //  Get the edge
                  int enode = *index_run++;
                  VE_Element *pedge;
                  pedge = ve_hash[enode];

                  //  Here we decide to draw or not based on the highest priority seen for this segment
                  //  That is, if this segment is going to be drawn at a higher priority later, then don't draw it here.
                  if ( pedge->max_priority != priority_current )
                        continue;

                  int nls = pedge->nCount;



                  ppt = pedge->pPoints;
                  for ( int ip = 0 ; ip < nls  ; ip++ )
                  {
                        easting = *ppt++;
                        northing = *ppt++;
                        rzRules->chart->GetPointPix ( rzRules, ( float ) northing, ( float ) easting, &ptp[ip + 1] );
                  }

                  //  Get last connected node
                  int jnode = *index_run++;
                  if (jnode >= 0)
                  {
                        pnode = vc_hash[jnode];
                        if(pnode)
                        {
                              ppt = pnode->pPoint;
                              easting = *ppt++;
                              northing = *ppt;
                              rzRules->chart->GetPointPix ( rzRules, ( float ) northing, ( float ) easting, &pra );
                        }
                        ptp[nls + 1] = pra;                     // insert ending node
                  }

                  if ( ( inode >= 0 ) && ( jnode >= 0 ) )
                        draw_lc_poly ( m_pdc, color, w, ptp, nls + 2, sym_len, sym_factor, rules->razRule, vp );
                  else
                        draw_lc_poly ( m_pdc, color, w, &ptp[1], nls, sym_len, sym_factor, rules->razRule, vp );


            }
            free ( ptp );
      }


      else if ( rzRules->obj->pPolyTessGeo )
      {
            if(!rzRules->obj->pPolyTessGeo->IsOk())               // perform deferred tesselation
                  rzRules->obj->pPolyTessGeo->BuildTessGL();

            PolyTriGroup *pptg = rzRules->obj->pPolyTessGeo->Get_PolyTriGroup_head();
            float *ppolygeo = pptg->pgroup_geom;

            int ctr_offset = 0;
            for ( int ic = 0; ic < pptg->nContours ; ic++ )
            {

                  int npt = pptg->pn_vertex[ic];
                  wxPoint *ptp = ( wxPoint * ) malloc ( ( npt + 1 ) * sizeof ( wxPoint ) );
                  wxPoint *pr = ptp;
                  for ( int ip=0 ; ip < npt ; ip++ )
                  {
                        float plon = ppolygeo[ ( 2 * ip ) + ctr_offset];
                        float plat = ppolygeo[ ( 2 * ip ) + ctr_offset + 1];

                        rzRules->chart->GetPointPix ( rzRules, plat, plon, pr );
                        pr++;
                  }
                  float plon = ppolygeo[ ctr_offset];             // close the polyline
                  float plat = ppolygeo[ ctr_offset + 1];
                  rzRules->chart->GetPointPix ( rzRules, plat, plon, pr );


                  draw_lc_poly ( m_pdc, color, w, ptp, npt + 1, sym_len, sym_factor, rules->razRule, vp );

                  free ( ptp );

                  ctr_offset += npt*2;
            }
      }

      else if ( rzRules->obj->pPolyTrapGeo )
      {
            if(!rzRules->obj->pPolyTrapGeo->IsOk())
                  rzRules->obj->pPolyTrapGeo->BuildTess();

            PolyTrapGroup *pptg = rzRules->obj->pPolyTrapGeo->Get_PolyTrapGroup_head();

            wxPoint2DDouble *ppolygeo = pptg->ptrapgroup_geom;

            int ctr_offset = 0;
            for ( int ic = 0; ic < pptg->nContours ; ic++ )
            {

                  npt = pptg->pn_vertex[ic];
                  wxPoint *ptp = ( wxPoint * ) malloc ( ( npt + 1 ) * sizeof ( wxPoint ) );
                  wxPoint *pr = ptp;
                  for ( int ip=0 ; ip < npt ; ip++, pr++ )
                        rzRules->chart->GetPointPix ( rzRules,  ppolygeo[ctr_offset + ip].m_y, ppolygeo[ctr_offset + ip].m_x, pr );

                  //  Close polyline
                  rzRules->chart->GetPointPix ( rzRules,  ppolygeo[ctr_offset].m_y, ppolygeo[ctr_offset].m_x, pr );

                  draw_lc_poly ( m_pdc, color, w, ptp, npt + 1, sym_len, sym_factor, rules->razRule, vp );

                  free ( ptp );
                  ctr_offset += ( npt + 1 ) *2;
            }
      }

      else if ( rzRules->obj->geoPt )                         // if the object is not described by a poly structure
      {
            pt *ppt = rzRules->obj->geoPt;


            npt = rzRules->obj->npt;
            ptp = ( wxPoint * ) malloc ( npt * sizeof ( wxPoint ) );
            wxPoint *pr = ptp;
            wxPoint p;
            for ( int ip=0 ; ip<npt ; ip++ )
            {
                  float plat = ppt->y;
                  float plon = ppt->x;

                  rzRules->chart->GetPointPix ( rzRules, plat, plon, &p );

                  *pr = p;

                  pr++;
                  ppt++;
            }

            draw_lc_poly ( m_pdc, color, w, ptp, npt, sym_len, sym_factor, rules->razRule, vp );

            free ( ptp );
      }

      return 1;
}


//      Render Line Complex Polyline

void s52plib::draw_lc_poly ( wxDC *pdc, wxColor &color, int width, wxPoint *ptp, int npt,
                             float sym_len, float sym_factor, Rule *draw_rule, ViewPort *vp )
{
      wxPoint   r;

      //    Get a true pixel clipping/bounding box from the vp
      wxPoint pbb = vp->GetPixFromLL(vp->clat, vp->clon);
      int xmin_ = pbb.x - vp->rv_rect.width/2;
      int xmax_ = xmin_ + vp->rv_rect.width;
      int ymin_ = pbb.y - vp->rv_rect.height/2;
      int ymax_ = ymin_ + vp->rv_rect.height;

      int x0, y0, x1, y1;

      if(pdc)
      {
             wxPen *pthispen = wxThePenList->FindOrCreatePen ( color, width, wxSOLID );
             m_pdc->SetPen ( *pthispen );

      for ( int iseg = 0 ; iseg < npt - 1 ; iseg++ )
      {
            // Do not bother with segments that are invisible

            x0 = ptp[iseg].x;
            y0 = ptp[iseg].y;
            x1 = ptp[iseg+1].x;
            y1 = ptp[iseg+1].y;

            ClipResult res = cohen_sutherland_line_clip_i ( &x0, &y0, &x1, &y1,
                             xmin_, xmax_, ymin_, ymax_ );

            if ( res == Invisible )
                  continue;

            float dx = ptp[iseg + 1].x - ptp[iseg].x;
            float dy = ptp[iseg + 1].y - ptp[iseg].y;
            float seg_len = sqrt ( dx*dx + dy*dy );
            float theta = atan2 ( dy,dx );
            float cth = cos ( theta );
            float sth = sin ( theta );
            float tdeg = theta * 180. / PI;

            if ( seg_len >= 1.0 )
            {
                  if ( seg_len <= sym_len * sym_factor )
                  {
                        if ( seg_len >= sym_len )
                        {
                              int xst1 = ptp[iseg].x;
                              float xst2 = xst1 + ( sym_len * cth );

                              int yst1 = ptp[iseg].y;
                              float yst2 = yst1 + ( sym_len * sth );

                              pdc->DrawLine ( xst1, yst1, ( wxCoord ) floor ( xst2 ), ( wxCoord ) floor ( yst2 ) );
                        }
                        else
                        {
                              pdc->DrawLines ( 2, &ptp[iseg] );
                        }
                  }

                  else
                  {
                        float s=0;
                        float xs = ptp[iseg].x;
                        float ys = ptp[iseg].y;

                        while ( s + ( sym_len * sym_factor ) < seg_len )
                        {
                              r.x = ( int ) xs;
                              r.y = ( int ) ys;
                              char *str = draw_rule->vector.LVCT;
                              char *col = draw_rule->colRef.LCRF;
                              wxPoint pivot ( draw_rule->pos.line.pivot_x.LICL, draw_rule->pos.line.pivot_y.LIRW );

                              RenderHPGLtoDC ( str, col, pdc, r, pivot, ( double ) tdeg );


                              xs += sym_len * cth * sym_factor;
                              ys += sym_len * sth * sym_factor;
                              s += sym_len * sym_factor;
                        }

                        pdc->DrawLine ( ( int ) xs, ( int ) ys, ptp[iseg+1].x, ptp[iseg+1].y );
                  }
            }
      } // for
      } // if pdc
      else          // opengl
      {
          //    Set up the color
          glColor4ub(color.Red(), color.Green(), color.Blue(), color.Alpha());
          glLineWidth(width);

          for ( int iseg = 0 ; iseg < npt - 1 ; iseg++ )
          {
            // Do not bother with segments that are invisible

            x0 = ptp[iseg].x;
            y0 = ptp[iseg].y;
            x1 = ptp[iseg+1].x;
            y1 = ptp[iseg+1].y;

            ClipResult res = cohen_sutherland_line_clip_i ( &x0, &y0, &x1, &y1,
                             xmin_, xmax_, ymin_, ymax_ );

            if ( res == Invisible )
                  continue;

            float dx = ptp[iseg + 1].x - ptp[iseg].x;
            float dy = ptp[iseg + 1].y - ptp[iseg].y;
            float seg_len = sqrt ( dx*dx + dy*dy );
            float theta = atan2 ( dy,dx );
            float cth = cos ( theta );
            float sth = sin ( theta );
            float tdeg = theta * 180. / PI;

            if ( seg_len >= 1.0 )
            {
                  if ( seg_len <= sym_len * sym_factor )
                  {
                        int xst1 = ptp[iseg].x;
                        int yst1 = ptp[iseg].y;
                        float xst2, yst2;
                        if ( seg_len >= sym_len )
                        {
                             xst2 = xst1 + ( sym_len * cth );
                             yst2 = yst1 + ( sym_len * sth );
                        }
                        else
                        {
                            xst2 = ptp[iseg+1].x;
                            yst2 = ptp[iseg+1].y;
                        }

                        glPushAttrib(GL_COLOR_BUFFER_BIT | GL_LINE_BIT | GL_HINT_BIT);      //Save state

                      //      Enable anti-aliased lines, at best quality
                        glEnable(GL_LINE_SMOOTH);
                        glEnable(GL_BLEND);
                        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                        glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

                       // if(m_pen.GetWidth() > 1)
                        //   DrawThickLine(x1, y1, x2, y2, m_pen.GetWidth());
                      //  else
                            {
                                glBegin(GL_LINES);
                                glVertex2i(xst1, yst1);
                                glVertex2i(( wxCoord ) floor ( xst2 ), ( wxCoord ) floor ( yst2 ));
                                glEnd();
                            }
                        glPopAttrib();
                  }
                  else
                  {
                        float s=0;
                        float xs = ptp[iseg].x;
                        float ys = ptp[iseg].y;

                        while ( s + ( sym_len * sym_factor ) < seg_len )
                        {
                              r.x = ( int ) xs;
                              r.y = ( int ) ys;
                              char *str = draw_rule->vector.LVCT;
                              char *col = draw_rule->colRef.LCRF;
                              wxPoint pivot ( draw_rule->pos.line.pivot_x.LICL, draw_rule->pos.line.pivot_y.LIRW );

                              RenderHPGLtoGL ( str, col, r, pivot, (double) tdeg );

                              xs += sym_len * cth * sym_factor;
                              ys += sym_len * sth * sym_factor;
                              s += sym_len * sym_factor;
                        }

                        // finish segment
                        glPushAttrib(GL_COLOR_BUFFER_BIT | GL_LINE_BIT | GL_HINT_BIT);      //Save state

                      //      Enable anti-aliased lines, at best quality
                        glEnable(GL_LINE_SMOOTH);
                        glEnable(GL_BLEND);
                        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                        glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

                       // if(m_pen.GetWidth() > 1)
                        //   DrawThickLine(x1, y1, x2, y2, m_pen.GetWidth());
                      //  else
                            {
                                glBegin(GL_LINES);
                                glVertex2i(xs, ys);
                                glVertex2i(ptp[iseg+1].x, ptp[iseg+1].y);
                                glEnd();
                            }
                        glPopAttrib();
                  }
            }
      } // for

      } //opengl
}




// Multipoint Sounding
int s52plib::RenderMPS ( ObjRazRules *rzRules, Rules *rules, ViewPort *vp )
{
      if ( !m_bShowSoundg )
            return 0;

      int npt = rzRules->obj->npt;

      wxPoint p;
      double *pd = rzRules->obj->geoPtz;            // the SM points
      double *pdl = rzRules->obj->geoPtMulti;       // and corresponding lat/lon
      if ( NULL == rzRules->child )
      {
            ObjRazRules *previous_rzRules = NULL;

            for ( int ip=0 ; ip<npt ; ip++ )
            {
                  double east = *pd++;
                  double nort = *pd++;;
                  double depth = *pd++;

                  ObjRazRules *point_rzRules = new ObjRazRules;
                  *point_rzRules = *rzRules;              // take a copy of attributes, etc

                  //  Need a new LUP
                  LUPrec *NewLUP = new LUPrec;

                  *NewLUP = * ( rzRules->LUP );       // copy the parent's LUP
                  NewLUP->ATTCArray = NULL;           //
                  NewLUP->ATTC = NULL;
                  NewLUP->INST = NULL;

                  point_rzRules->LUP = NewLUP;

                  //  Need a new S57Obj
                  S57Obj *point_obj = new S57Obj;
                  *point_obj = * ( rzRules->obj );
                  point_rzRules->obj = point_obj;

                  //  Touchup the new items
                  point_rzRules->obj->bCS_Added = false;
                  point_rzRules->obj->bIsClone = true;
                  point_rzRules->obj->npt = 1;

                  point_rzRules->next = previous_rzRules;
                  Rules *ru = StringToRules ( _T ( "CS(SOUNDG03;" ) );
                  point_rzRules->LUP->ruleList = ru;


                  point_obj->x = east;
                  point_obj->y = nort;
                  point_obj->z = depth;

                  double lon = *pdl++;
                  double lat = *pdl++;
                  point_obj->BBObj.SetMin ( lon, lat );
                  point_obj->BBObj.SetMax ( lon, lat );

                  previous_rzRules = point_rzRules;
            }

            //   Top of the chain is previous_rzRules
            rzRules->child = previous_rzRules;
      }


      //   Walk the chain, drawing..
      ObjRazRules *current = rzRules->child;
      while ( current )
      {
            if(m_pdc)
                  RenderObjectToDC ( m_pdc, current, vp );
            else
            {
                  wxRect NullRect;
                  RenderObjectToGL ( *m_glcc, current, vp, NullRect );
            }

            current = current->next;
      }

      return 1;
}


int s52plib::RenderCARC ( ObjRazRules *rzRules, Rules *rules, ViewPort *vp )
{
      char *str = ( char* ) rules->INSTstr;

      //    extract the parameters from the string
      //    And creating a unique string hash as we go
      wxString inst ( str, wxConvUTF8 );
      wxString carc_hash;

      wxStringTokenizer tkz ( inst, _T ( ",;" ) );

      //    outline color
      wxString outline_color = tkz.GetNextToken();
      carc_hash += outline_color;
      carc_hash += _T(".");

      //    outline width
      wxString slong = tkz.GetNextToken();
      long outline_width;
      slong.ToLong ( &outline_width );
      carc_hash += slong;
      carc_hash += _T(".");

      //    arc color
      wxString arc_color = tkz.GetNextToken();
      carc_hash += arc_color;
      carc_hash += _T(".");

      //    arc width
      slong = tkz.GetNextToken();
      long arc_width;
      slong.ToLong ( &arc_width );
      carc_hash += slong;
      carc_hash += _T(".");

      //    sectr1
      slong = tkz.GetNextToken();
      double sectr1;
      slong.ToDouble ( &sectr1 );
      carc_hash += slong;
      carc_hash += _T(".");

      //    sectr2
      slong = tkz.GetNextToken();
      double sectr2;
      slong.ToDouble ( &sectr2 );
      carc_hash += slong;
      carc_hash += _T(".");

      //    arc radius
      slong = tkz.GetNextToken();
      long radius;
      slong.ToLong ( &radius );
      carc_hash += slong;
      carc_hash += _T(".");

      //    sector radius
      slong = tkz.GetNextToken();
      long sector_radius;
      slong.ToLong ( &sector_radius );
      carc_hash += slong;
      carc_hash += _T(".");

      slong.Printf(_T("%d"),m_colortable_index);
      carc_hash += slong;

      int width;
      int height;
      int rad;
      int bm_width;
      int bm_height;
      int bm_orgx;
      int bm_orgy;

      Rule *prule = rules->razRule;

      //Instantiate the symbol if necessary
      if ( ( rules->razRule->pixelPtr == NULL ) || ( rules->razRule->parm1 != m_colortable_index ) )
      {
            //  Render the sector light to a bitmap

            rad = ( int ) ( radius * m_display_pix_per_mm );

            width = ( rad * 2 ) + 28;
            height = ( rad * 2 ) + 28;
            wxBitmap *pbm = new wxBitmap ( width, height, -1 );
            wxMemoryDC mdc;
            mdc.SelectObject ( *pbm );
            mdc.SetBackground ( wxBrush ( m_unused_wxColor ) );
            mdc.Clear();


            //    Adjust sector math for wxWidgets API
            double sb;
            double se;

            //      For some reason, the __WXMSW__ build flips the sense of
            //      start and end angles on DrawEllipticArc()
#ifndef __WXMSW__
            if ( sectr2 > sectr1 )
            {
                  sb = 90 - sectr1;
                  se = 90 - sectr2;
            }
            else
            {
                  sb = 360 + ( 90 - sectr1 );
                  se = 90 - sectr2;
            }
#else
            if ( sectr2 > sectr1 )
            {
                  se = 90 - sectr1;
                  sb = 90 - sectr2;
            }
            else
            {
                  se = 360 + ( 90 - sectr1 );
                  sb = 90 - sectr2;
            }
#endif


            //      Here is a goofy way of computing the dc drawing extents exactly
            //      Draw a series of fat line segments approximating the arc using dc.DrawLine()
            //      This will properly establish the drawing box in the dc

            int border_fluff = 4;                      // by how much should the blit bitmap be "fluffed"
            if ( fabs ( sectr2 - sectr1 ) != 360 )   // not necessary for all-round lights
            {
                  mdc.ResetBoundingBox();

                  wxPen *pblockpen = wxThePenList->FindOrCreatePen ( *wxBLACK, 10, wxSOLID );
                  mdc.SetPen ( *pblockpen );

                  double start_angle, end_angle;
                  if ( se < sb )
                  {
                        start_angle = se;
                        end_angle = sb;
                  }
                  else
                  {
                        start_angle = sb;
                        end_angle = se;
                  }

                  int x0 = ( width / 2 ) + ( int ) ( rad * cos ( start_angle * PI / 180. ) );
                  int y0 = ( height / 2 ) - ( int ) ( rad * sin ( start_angle * PI / 180. ) );
                  for ( double a = start_angle + .1 ; a <= end_angle ; a+= 2.0 )
                  {
                        int x = ( width / 2 ) + ( int ) ( rad * cos ( a * PI / 180. ) );
                        int y = ( height / 2 ) - ( int ) ( rad * sin ( a * PI / 180. ) );
                        mdc.DrawLine ( x0,y0,x,y );
                        x0=x;
                        y0=y;
                  }

                  bm_width  = ( mdc.MaxX() - mdc.MinX() ) + ( border_fluff * 2 );
                  bm_height = ( mdc.MaxY() - mdc.MinY() ) + ( border_fluff * 2 );
                  bm_orgx = wxMax ( 0, mdc.MinX()-border_fluff );
                  bm_orgy = wxMax ( 0, mdc.MinY()-border_fluff );

                  mdc.Clear();
            }

            else
            {
                  bm_width  = rad * 2 + ( border_fluff * 2 );
                  bm_height = rad * 2 + ( border_fluff * 2 );
                  bm_orgx = wxMax ( 0, ( width / 2 - rad ) - border_fluff );
                  bm_orgy = wxMax ( 0, ( width / 2 - rad ) - border_fluff );

            }

            wxBitmap *sbm = NULL;

            //    Do not need to actually render the symbol for OpenGL mode
            //    We just need the extents calculated above...
            if(m_pdc)
            {
                  //    Draw the outer border
                  wxColour color = S52_getwxColour ( outline_color );

                  wxPen *pthispen = wxThePenList->FindOrCreatePen ( color, outline_width, wxSOLID );
                  mdc.SetPen ( *pthispen );
                  wxBrush *pthisbrush = wxTheBrushList->FindOrCreateBrush ( color, wxTRANSPARENT );
                  mdc.SetBrush ( *pthisbrush );

                  mdc.DrawEllipticArc ( width/2 - rad, height/2 - rad, rad * 2, rad * 2, sb, se );

                  if ( arc_width )
                  {
                        wxColour colorb = S52_getwxColour ( arc_color );

                        if(!colorb.IsOk())
                              colorb = S52_getwxColour ( _T("CHMGD") );

                        pthispen = wxThePenList->FindOrCreatePen ( colorb, arc_width, wxSOLID );
                        mdc.SetPen ( *pthispen );

                        mdc.DrawEllipticArc ( width/2 - rad, height/2 - rad, rad * 2, rad * 2, sb, se );

                  }


                  mdc.SelectObject ( wxNullBitmap );

                  //          Get smallest containing bitmap
                  sbm = new wxBitmap ( pbm->GetSubBitmap ( wxRect ( bm_orgx, bm_orgy, bm_width, bm_height ) ) );

                  delete pbm;

                  //      Make the mask
                  wxMask *pmask = new wxMask ( *sbm, m_unused_wxColor );

                  //      Associate the mask with the bitmap
                  sbm->SetMask ( pmask );

                  // delete any old private data
                  if ( rules->razRule->parm0 && rules->razRule->pixelPtr )
                  {
                        switch(rules->razRule->parm0)
                        {
                              case ID_wxBitmap:
                              {
                                    wxBitmap *pbm = ( wxBitmap * ) ( rules->razRule->pixelPtr );
                                    delete pbm;
                                    rules->razRule->pixelPtr = NULL;
                                    rules->razRule->parm0 = 0;
                                    break;
                              }
                              case ID_RGBA:
                              {
                                    unsigned char *p = ( unsigned char * ) ( rules->razRule->pixelPtr );
                                    free ( p );
                                    rules->razRule->pixelPtr = NULL;
                                    rules->razRule->parm0 = 0;
                                    break;
                              }
                        }
                  }
            }

            //      Save the bitmap ptr and aux parms in the rule
            prule->pixelPtr = sbm;
            prule->parm0 = ID_wxBitmap;
            prule->parm1 = m_colortable_index;
            prule->parm2 = bm_orgx - width/2;
            prule->parm3 = bm_orgy - height/2;
            prule->parm5 = bm_width;
            prule->parm6 = bm_height;
      }


      if(!m_pdc)        // opengl
      {
            //    Is there not already an generated display list in the CARC_hashmap for this object?
            if(m_CARC_hashmap.find(carc_hash) == m_CARC_hashmap.end())
            {
                  // Generate a Display list
                  GLuint carc_list = glGenLists (1);
                  glNewList(carc_list, GL_COMPILE);

                  glEnable(GL_LINE_SMOOTH);
                  glEnable(GL_BLEND);
                  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                  glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

                  rad = ( int ) ( radius * m_display_pix_per_mm );

      //    Render the symbology as a zero based Display List

      //    Draw wide outline arc
                  wxColour colorb = S52_getwxColour ( outline_color );
                  glColor4ub(colorb.Red(), colorb.Green(), colorb.Blue(), 255);
                  glLineWidth(outline_width);

                  glBegin(GL_LINE_STRIP);
                  for(double a = sectr1 * M_PI / 180.0; a <= sectr2 * M_PI / 180.; a+=2*M_PI/200)
                        glVertex2d(rad*sin(a), -rad*cos(a));
                  glEnd();

      //    Draw narrower color arc, overlaying the drawn outline.
                  colorb = S52_getwxColour ( arc_color );
                  glColor4ub(colorb.Red(), colorb.Green(), colorb.Blue(), 255);
                  glLineWidth(arc_width);

                  glBegin(GL_LINE_STRIP);
                  for(double a = sectr1 * M_PI / 180.0; a <= sectr2 * M_PI / 180.; a+=2*M_PI/200)
                        glVertex2d(rad*sin(a), -rad*cos(a));
                  glEnd();

      //    Draw the sector legs
                  if ( sector_radius > 0 )
                  {
                        int leg_len = ( int ) ( sector_radius * m_display_pix_per_mm );

                        wxColour c = GetGlobalColor ( _T ( "CHBLK" ) );
                        glColor4ub(c.Red(), c.Green(), c.Blue(), c.Alpha());
                        glLineWidth(1);

                        glLineStipple(1, 0x3F3F);
                        glEnable(GL_LINE_STIPPLE);

                        double a = ( sectr1-90 ) * PI / 180;
                        int x =  ( int ) ( leg_len * cos ( a ) );
                        int y =  ( int ) ( leg_len * sin ( a ) );
                        glBegin(GL_LINES);
                        glVertex2i(0,0);
                        glVertex2i(x, y);
                        glEnd();

                        a = ( sectr2-90 ) * PI / 180;
                        x =  ( int ) ( leg_len * cos ( a ) );
                        y =  ( int ) ( leg_len * sin ( a ) );
                        glBegin(GL_LINES);
                        glVertex2i(0,0);
                        glVertex2i(x, y);
                        glEnd();

                        glDisable(GL_LINE_STIPPLE);

                  }

                  glEndList();

                  //    Record the existence of this display list in the searchable hashmap
                  m_CARC_hashmap[carc_hash] = carc_list;
            }

            //      Save the list and OpenGL specific parameters in the rule
            prule->pixelPtr = (void *)1;
            prule->parm0 = ID_GLIST;
            prule->parm7 = m_CARC_hashmap[carc_hash];

      }               // instantiation

      int b_width  = prule->parm5;
      int b_height = prule->parm6;

      //  Render arcs at object's x/y
      wxPoint r;
      rzRules->chart->GetPointPix ( rzRules, rzRules->obj->y, rzRules->obj->x, &r );

      //      Now render the symbol
      if(!m_pdc)          // opengl
      {
            glPushAttrib(GL_COLOR_BUFFER_BIT | GL_LINE_BIT | GL_HINT_BIT);      //Save state

            glTranslatef(r.x, r.y, 0);
            glCallList( rules->razRule->parm7 );
            glTranslatef(-r.x, -r.y, 0);

            glPopAttrib();

      }
      else
      {
            //      Get the bitmap into a memory dc
            wxMemoryDC mdc;
            mdc.SelectObject ( ( wxBitmap & ) ( * ( ( wxBitmap * ) ( rules->razRule->pixelPtr ) ) ) );

            //      Blit it into the target dc, using mask
            m_pdc->Blit ( r.x + rules->razRule->parm2, r.y + rules->razRule->parm3, b_width, b_height, &mdc, 0, 0, wxCOPY,  true );
      // Debug
      //        pdc->SetPen(wxPen(*wxGREEN, 1));
      //        pdc->SetBrush(wxBrush(*wxGREEN, wxTRANSPARENT));
      //        pdc->DrawRectangle(r.x + rules->razRule->parm2, r.y + rules->razRule->parm3, b_width, b_height);

            mdc.SelectObject ( wxNullBitmap );

            //    Draw the sector legs directly on the target DC
            //    so that anti-aliasing works against the drawn image (cannot be cached...)
            if ( sector_radius > 0 )
            {
                  int leg_len = ( int ) ( sector_radius * m_display_pix_per_mm );

                  wxDash dash1[2];
                  dash1[0] = ( int ) ( 3.6 * m_display_pix_per_mm );  //8// Long dash  <---------+
                  dash1[1] = ( int ) ( 1.8 * m_display_pix_per_mm );  //2// Short gap            |

                  /*
                              wxPen *pthispen = new wxPen(*wxBLACK_PEN);
                              pthispen->SetStyle(wxUSER_DASH);
                              pthispen->SetDashes( 2, dash1 );
                              //      Undocumented "feature":  Pen must be fully specified <<<BEFORE>>> setting into DC
                              pdc->SetPen ( *pthispen );
                  */
                  wxColour c = GetGlobalColor ( _T ( "CHBLK" ) );
                  double a = ( sectr1-90 ) * PI / 180;
                  int x = r.x + ( int ) ( leg_len * cos ( a ) );
                  int y = r.y + ( int ) ( leg_len * sin ( a ) );
                  DrawWuLine ( m_pdc, r.x, r.y, x, y, c, dash1[0], dash1[1] );

                  a = ( sectr2-90 ) * PI / 180;
                  x = r.x + ( int ) ( leg_len * cos ( a ) );
                  y = r.y + ( int ) ( leg_len * sin ( a ) );
                  DrawWuLine ( m_pdc, r.x, r.y, x, y, c, dash1[0], dash1[1] );
            }
      }


      //  Update the object Bounding box,
      //  so that subsequent drawing operations will redraw the item fully

      double plat, plon;
      wxBoundingBox symbox;

      rzRules->chart->GetPixPoint ( r.x + rules->razRule->parm2, r.y + rules->razRule->parm3 + b_height, &plat, &plon, vp );
      symbox.SetMin ( plon, plat );
      rzRules->chart->GetPixPoint ( r.x + rules->razRule->parm2 + b_width, r.y + rules->razRule->parm3, &plat, &plon, vp );
      symbox.SetMax ( plon, plat );

      if(rzRules->obj->bBBObj_valid)
            rzRules->obj->BBObj.Expand ( symbox );
      else
      {
            rzRules->obj->BBObj = symbox;
            rzRules->obj->bBBObj_valid = true;
      }


      return 1;
}








// Conditional Symbology
char *s52plib::RenderCS ( ObjRazRules *rzRules, Rules *rules )
{
      void *ret;
      void* ( *f ) ( void* );

      static int f05;

      if ( rules->razRule == NULL )
      {
            if ( !f05 )
//                  CPLError ( ( CPLErr ) 0, 0,"S52plib:_renderCS(): ERROR no conditional symbology for: %s\n", rules->INSTstr );
            f05++;
            return 0;
      }


      void *g = ( void * ) rules->razRule;

#ifdef FIX_FOR_MSVC  //__WXMSW__
//#warning Fix this cast, somehow...
//      dsr             sigh... can't get the cast right
      _asm
      {
            mov eax,[dword ptr g]
            mov [dword ptr f],eax
      }
      ret = f ( ( void * ) rzRules );    // call cond symb
#else

      f= ( void * ( * ) ( void * ) ) g;
      ret = f ( ( void * ) rzRules );

#endif

      return ( char * ) ret;
}

int s52plib::RenderObjectToDC ( wxDC *pdcin, ObjRazRules *rzRules, ViewPort *vp )
{
      return DoRenderObject ( pdcin, rzRules, vp );
}

int s52plib::RenderObjectToGL ( const wxGLContext &glcc, ObjRazRules *rzRules, ViewPort *vp, wxRect &render_rect )
{
      m_glcc = (wxGLContext *)&glcc;
      return DoRenderObject ( NULL, rzRules, vp );
}

int s52plib::DoRenderObject ( wxDC *pdcin, ObjRazRules *rzRules, ViewPort *vp )
{
      //  Debug Hook
//   if(!strncmp(rzRules->LUP->OBCL, "SOUNDG", 6))
//        int yyrt = 4;
//      if(rzRules->obj->Index == 1596)
//            int rrt = 5;

      if ( !ObjectRenderCheckPos ( rzRules, vp ) )
            return 0;

      if ( !ObjectRenderCheckCat ( rzRules, vp ) )
      {
            //  This is an "out-of-spec" optimization
            //    Conditional symbology for rocks and wrecks is expensive
            //    because we have to find the DEPARE or DRGARE that it is in,
            //    and compare the depths to establish safety limits,
            //    and then potentially move the hazard to DISPLAYBASE category.
            //
            //    Lets only consider and allow this case for large scale chart views....
            if( (!strncmp(rzRules->LUP->OBCL, "UWTROC", 6)) ||
                 (!strncmp(rzRules->LUP->OBCL, "WRECKS", 6)) )
            {
                  if(vp->chart_scale > 20000.)
                        return 0;
            }

            if(!ObjectRenderCheckCS( rzRules, vp ))
                  return 0;
      }


      m_pdc = pdcin;                    // use this DC
      Rules *rules = rzRules->LUP->ruleList;


//  Debug Hooks
//   if(rzRules->obj->Index == 1596315)
//         int rrt = 5;

//    if(!strncmp(rzRules->LUP->OBCL, "RDOCAL", 6))
//            int yyrkt = 4;

      while ( rules != NULL )
      {
            switch ( rules->ruleType )
            {
                  case RUL_TXT_TX:       if(ObjectRenderCheckCat ( rzRules, vp )) { RenderTX ( rzRules,rules, vp );} break;          // TX
                  case RUL_TXT_TE:       if(ObjectRenderCheckCat ( rzRules, vp )) { RenderTE ( rzRules,rules, vp );} break;          // TE
                  case RUL_SYM_PT:       if(ObjectRenderCheckCat ( rzRules, vp )) { RenderSY ( rzRules,rules, vp );} break;          // SY
                  case RUL_SIM_LN:       if(ObjectRenderCheckCat ( rzRules, vp )) { RenderLS ( rzRules,rules, vp );} break;          // LS
                  case RUL_COM_LN:       if(ObjectRenderCheckCat ( rzRules, vp )) { RenderLC ( rzRules,rules, vp );} break;          // LC
                  case RUL_MUL_SG:       if(ObjectRenderCheckCat ( rzRules, vp )) { RenderMPS ( rzRules,rules, vp );} break;         // MultiPoint Sounding
                  case RUL_ARC_2C:       if(ObjectRenderCheckCat ( rzRules, vp )) { RenderCARC ( rzRules,rules, vp );} break;        // Circular Arc, 2 colors

                  case RUL_CND_SY:
                  {
                        if ( !rzRules->obj->bCS_Added )
                        {
                              rzRules->obj->CSrules = NULL;
                              GetAndAddCSRules ( rzRules, rules );
                              rzRules->obj->bCS_Added = 1;                // mark the object
                        }

                        //    The CS procedure may have changed the Display Category of the Object, need to check again for visibility
                        if(ObjectRenderCheckCat ( rzRules, vp ))
                        {

                              Rules *rules_last = rules;
                              rules = rzRules->obj->CSrules;

                              while ( NULL != rules )
                              {
                                    switch ( rules->ruleType )
                                    {
                                          case RUL_SIM_LN:
                                          case RUL_COM_LN:
                                          {
                                                PrioritizeLineFeature ( rzRules, rzRules->LUP->DPRI - '0');
                                                break;
                                          }
                                          default:
                                                break;
                                    }

                                    switch ( rules->ruleType )
                                    {
      //                                        case RUL_ARE_CO:       RenderAC(rzRules,rules, vp);break;
                                          case RUL_TXT_TX:       RenderTX ( rzRules,rules, vp );break;
                                          case RUL_TXT_TE:       RenderTE ( rzRules,rules, vp );break;
                                          case RUL_SYM_PT:       RenderSY ( rzRules,rules, vp );break;
                                          case RUL_SIM_LN:       RenderLS ( rzRules,rules, vp );break;
                                          case RUL_COM_LN:       RenderLC ( rzRules,rules, vp );break;
                                          case RUL_MUL_SG:       RenderMPS ( rzRules,rules, vp );break;   // MultiPoint Sounding
                                          case RUL_ARC_2C:       RenderCARC ( rzRules,rules, vp );break;   // Circular Arc, 2 colors
                                          case RUL_NONE:
                                          default:
                                                break; // no rule type (init)
                                    }
                                    rules_last = rules;
                                    rules = rules->next;
                              }

                              rules = rules_last;
                              break;
                        }
                  }

                  case RUL_NONE:
                  default:
                        break; // no rule type (init)
            }                                     // switch

            rules = rules->next;
      }

      return 1;
}

bool s52plib::PreloadOBJLFromCSV(wxString &csv_file)
{
      wxTextFile file(csv_file);
      if(!file.Exists())
            return false;

      file.Open();

      wxString str;
      str = file.GetFirstLine();
      wxChar quote[] = {'\"', 0};

      while(!file.Eof())
      {
            str = file.GetNextLine();

            wxStringTokenizer tkz(str, _T(","));
            wxString token = tkz.GetNextToken();    // code
            token = tkz.GetNextToken();             // object class
            if(!token.EndsWith(quote))
                  token = tkz.GetNextToken();       // object class, post comma

            token = tkz.GetNextToken();             // Acronym

            if(token.Len())
            {
                  //    Filter out any duplicates, in a case insensitive way
                  //    i.e. only the first of "DEPARE" and "depare" is added
                  bool bdup = false;
                  for(unsigned int iPtr = 0 ; iPtr < pOBJLArray->GetCount() ; iPtr++)
                  {
                        OBJLElement *pOLEt = (OBJLElement *)(pOBJLArray->Item(iPtr));
                        if(!token.CmpNoCase(wxString(pOLEt->OBJLName, wxConvUTF8)))
                        {
                              bdup = true;
                              break;
                        }
                  }

                  if(!bdup)
                  {
                        OBJLElement *pOLE = (OBJLElement *)calloc(sizeof(OBJLElement), 1);
                        strncpy(pOLE->OBJLName, token.mb_str(), 6);
                        pOLE->nViz = 0;

                        pOBJLArray->Add((void *)pOLE);
                  }
            }

      }

      return true;
}


void s52plib::UpdateOBJLArray(S57Obj *obj)
{
      //    Search the array for this object class

      bool bNeedNew = true;
      OBJLElement *pOLE;

      for(unsigned int iPtr = 0 ; iPtr < pOBJLArray->GetCount() ; iPtr++)
      {
            pOLE = (OBJLElement *)(pOBJLArray->Item(iPtr));
            if(!strncmp(pOLE->OBJLName, obj->FeatureName, 6))
            {
                  obj->iOBJL = iPtr;
                  bNeedNew = false;
                  break;
            }
      }

      //    Not found yet, so add an element
      if(bNeedNew)
      {
            pOLE = (OBJLElement *)calloc(sizeof(OBJLElement), 1);
            strncpy(pOLE->OBJLName, obj->FeatureName, 6);
            pOLE->nViz = 0;

            pOBJLArray->Add((void *)pOLE);
            obj->iOBJL  = pOBJLArray->GetCount() - 1;
      }


}


int s52plib::SetLineFeaturePriority ( ObjRazRules *rzRules, int npriority )
{
      //  Debug Hook
//   if(!strncmp(rzRules->LUP->OBCL, "FAIRWY", 6))
//        int yyrt = 4;


      int priority_set = npriority;             // may be adjusted

      Rules *rules = rzRules->LUP->ruleList;


      //      Do Object Type Filtering
      //    If the object s not currently visible (i.e. on a not-currently visible layer),
      //    then do not set the line segment priorities at all

      bool b_catfilter = true;

      if ( m_nDisplayCategory == MARINERS_STANDARD )
      {
            if(-1 == rzRules->obj->iOBJL)
                  UpdateOBJLArray(rzRules->obj);

            if ( ! ( ( OBJLElement * ) ( pOBJLArray->Item ( rzRules->obj->iOBJL ) ) )->nViz )
                  b_catfilter = false;
      }

      if ( m_nDisplayCategory == OTHER )
      {
            if ( ( DISPLAYBASE != rzRules->LUP->DISC )
                    && ( STANDARD != rzRules->LUP->DISC )
                    && ( OTHER != rzRules->LUP->DISC ) )
            {
                  b_catfilter = false;
            }
      }

      else if ( m_nDisplayCategory == STANDARD )
      {
            if ( ( DISPLAYBASE != rzRules->LUP->DISC ) && ( STANDARD != rzRules->LUP->DISC ) )
            {
                  b_catfilter = false;
            }
      }
      else if ( m_nDisplayCategory == DISPLAYBASE )
      {
            if ( DISPLAYBASE != rzRules->LUP->DISC )
            {
                  b_catfilter = false;
            }
      }

      if ( !b_catfilter )
            return 0;


//    Debug Hook
//      if(rzRules->obj->Index == 219)
//            int rrt = 5;

      while ( rules != NULL )
      {
            switch ( rules->ruleType )
            {

                  case RUL_SIM_LN:       PrioritizeLineFeature ( rzRules, priority_set );break;          // LS
                  case RUL_COM_LN:       PrioritizeLineFeature ( rzRules, priority_set );break;          // LC

                  case RUL_CND_SY:
                  {
                        if ( !rzRules->obj->bCS_Added )
                        {
                              rzRules->obj->CSrules = NULL;
                              GetAndAddCSRules ( rzRules, rules );
                              rzRules->obj->bCS_Added = 1;                // mark the object
                        }
                        Rules *rules_last = rules;
                        rules = rzRules->obj->CSrules;

                        while ( NULL != rules )
                        {
                              switch ( rules->ruleType )
                              {
                                    case RUL_SIM_LN:       PrioritizeLineFeature ( rzRules, priority_set );break;
                                    case RUL_COM_LN:       PrioritizeLineFeature ( rzRules, priority_set );break;
                                    case RUL_NONE:
                                    default:
                                          break; // no rule type (init)
                              }
                              rules_last = rules;
                              rules = rules->next;
                        }

                        rules = rules_last;
                        break;
                  }

                  case RUL_NONE:
                  default:
                        break; // no rule type (init)
            }                                     // switch

            rules = rules->next;
      }

      return 1;
}

int s52plib::PrioritizeLineFeature ( ObjRazRules *rzRules, int npriority )
{
//    Debug
//      if(rzRules->obj->Index != 549)
//            return 1;

      if ( rzRules->obj->m_n_lsindex )
      {
            VE_Hash &edge_hash = rzRules->chart->Get_ve_hash();
            int *index_run = rzRules->obj->m_lsindex_array;

            for ( int iseg=0 ; iseg < rzRules->obj->m_n_lsindex ; iseg++ )
            {
                  //  Get first connected node
                  int inode = *index_run++;

                  //  Get the edge
                  int enode = *index_run++;

//                  if(enode >= 0)
                  {
//                        if ( enode < rzRules->chart->Get_nve_elements() )
                        {
                              VE_Element *pedge = edge_hash[enode];

                              //    Set priority
                              pedge->max_priority = npriority;
                        }
      //                  else
      //                        int rrty = 9;                       // index is out of bounds
                  }
//                  else
//                        int rttu = 9;                             //  enode is negative, some init error

                  //  Get last connected node
                  inode = *index_run++;

            }
      }

      return 1;
}

class XPOINT
{
public:
      float x, y;
};

class XLINE
{
public:
      XPOINT o, p;
      float m;
      float c;
};

bool TestLinesIntersection( XLINE &a, XLINE &b )
{
      XPOINT i;

      if ( (a.p.x == a.o.x) && (b.p.x == b.o.x) )     // both vertical
      {
            return(a.p.x == b.p.x);
      }

      if  (a.p.x == a.o.x)                // a line a is vertical
      {
            // calculate b gradient
            b.m = (b.p.y - b.o.y) / (b.p.x - b.o.x);
            // calculate axis intersect values
            b.c = b.o.y - (b.m * b.o.x);
            // calculate y point of intercept
            i.y = b.o.y + ((a.o.x - b.o.x) * b.m);
            if(i.y < wxMin(a.o.y, a.p.y) || i.y > wxMax(a.o.y, a.p.y))
                  return false;
            return true;
      }

      if  (b.p.x == b.o.x)                // line b is vertical
      {
            // calculate b gradient
            a.m = (a.p.y - a.o.y) / (a.p.x - a.o.x);
            // calculate axis intersect values
            a.c = a.o.y - (a.m * a.o.x);
            // calculate y point of intercept
            i.y = a.o.y + ((b.o.x - a.o.x) * a.m);
            if(i.y < wxMin(b.o.y, b.p.y) || i.y > wxMax(b.o.y, b.p.y))
                  return false;
            return true;
      }


// calculate gradients
      a.m = (a.p.y - a.o.y) / (a.p.x - a.o.x);
      b.m = (b.p.y - b.o.y) / (b.p.x - b.o.x);
// parallel lines can't intercept
      if (a.m == b.m)
      {
            return false;
      }
// calculate axis intersect values
      a.c = a.o.y - (a.m * a.o.x);
      b.c = b.o.y - (b.m * b.o.x);
// calculate x point of intercept
      i.x = (b.c - a.c) / (a.m - b.m);
// is intersection point in segment
      if ( i.x < wxMin(a.o.x, a.p.x) || i.x >wxMax(a.o.x, a.p.x) )
      {
            return false;
      }
      if ( i.x < wxMin(b.o.x, b.p.x) || i.x > wxMax(b.o.x, b.p.x) )
      {
            return false;
      }
// points intercept
      return true;
}

//-----------------------------------------------------------------------
//    Check a triangle described by point array, and rectangle described by render_canvas_parms
//    for intersection
//    Return false if no intersection
//-----------------------------------------------------------------------
bool s52plib::inter_tri_rect(wxPoint *ptp, render_canvas_parms *pb_spec)
{
      //    First stage
      //    Check all three points of triangle to see it any are within the render rectangle

      wxBoundingBox rect(pb_spec->lclip, pb_spec->y, pb_spec->rclip, pb_spec->y + pb_spec->height);

      for(int i=0 ; i < 3 ; i++)
      {
            if(rect.PointInBox( ptp[i].x, ptp[i].y))
                  return true;
      }

      //    Next stage
      //    Check all four points of rectangle to see it any are within the render triangle

      double p[6];
      MyPoint *pmp = (MyPoint *)p;

      for(int i=0 ; i<3 ; i++)
      {
            pmp[i].x = ptp[i].x;
            pmp[i].y = ptp[i].y;
      }


      if(G_PtInPolygon(pmp, 3, pb_spec->lclip, pb_spec->y))
            return true;

      if(G_PtInPolygon(pmp, 3, pb_spec->lclip, pb_spec->y + pb_spec->height))
            return true;

      if(G_PtInPolygon(pmp, 3, pb_spec->rclip, pb_spec->y))
            return true;

      if(G_PtInPolygon(pmp, 3, pb_spec->rclip, pb_spec->y + pb_spec->height))
            return true;

      //    last step
      //    Check triangle lines against rect lines for line intersect

      for(int i = 0 ; i < 3 ; i++)
      {
            XLINE a;
            a.o.x = ptp[i].x;
            a.o.y = ptp[i].y;
            if(i == 2)
            {
                  a.p.x = ptp[0].x;
                  a.p.y = ptp[0].y;
            }
            else
            {
                  a.p.x = ptp[i+1].x;
                  a.p.y = ptp[i+1].y;
            }

            XLINE b;

            //    top line
            b.o.x = pb_spec->lclip;
            b.o.y = pb_spec->y;
            b.p.x = pb_spec->rclip;
            b.p.y = pb_spec->y;

            if(TestLinesIntersection(a, b))
                  return true;

            //    right line
            b.o.x = pb_spec->rclip;
            b.o.y = pb_spec->y;
            b.p.x = pb_spec->rclip;
            b.p.y = pb_spec->y + pb_spec->height;

            if(TestLinesIntersection(a, b))
                  return true;

            //    bottom line
            b.o.x = pb_spec->rclip;
            b.o.y = pb_spec->y + pb_spec->height;
            b.p.x = pb_spec->lclip;
            b.p.y = pb_spec->y + pb_spec->height;

            if(TestLinesIntersection(a, b))
                  return true;

            //    left line
            b.o.x = pb_spec->lclip;
            b.o.y = pb_spec->y + pb_spec->height;
            b.p.x = pb_spec->lclip;
            b.p.y = pb_spec->y;

            if(TestLinesIntersection(a, b))
                  return true;
      }


      return false;                       // no Intersection

}

//----------------------------------------------------------------------------------
//
//              Fast Basic Canvas Rendering
//              Render triangle
//
//----------------------------------------------------------------------------------
int s52plib::dda_tri ( wxPoint *ptp, S52color *c, render_canvas_parms *pb_spec, render_canvas_parms *pPatt_spec )
{
      unsigned char r = 0;
      unsigned char g = 0;
      unsigned char b = 0;

      if(!inter_tri_rect(ptp, pb_spec))
            return 0;

      if ( NULL != c )
      {
#ifdef ocpnUSE_ocpnBitmap
            r = c->R;
            g = c->G;
            b = c->B;
#else
            b = c->R;
            g = c->G;
            r = c->B;
#endif
      }

      //      Color Debug
      /*    int fc = rand();
          b = fc & 0xff;
          g = fc & 0xff;
          r = fc & 0xff;
      */

      int color_int = 0;
      if ( NULL != c )
            color_int = ( ( r ) << 16 ) + ( ( g ) << 8 ) + ( b );

      //      Determine ymin and ymax indices

      int ymax = ptp[0].y;
      int ymin = ymax;
      int xmin, xmax, xmid, ymid;
      int imin = 0;
      int imax = 0;
      int imid;

      for ( int ip=1 ; ip < 3 ; ip++ )
      {
            if ( ptp[ip].y > ymax )
            {
                  imax = ip;
                  ymax = ptp[ip].y;
            }
            if ( ptp[ip].y <= ymin )
            {
                  imin = ip;
                  ymin= ptp[ip].y;
            }
      }


      imid = 3 - ( imin + imax );          // do the math...

      xmax = ptp[imax].x;
      xmin = ptp[imin].x;
      xmid = ptp[imid].x;
      ymid = ptp[imid].y;

      //      Create edge arrays using fast integer DDA
      int m, x, dy, count;
      bool dda8 = false;
      bool cw;

      if (( abs( xmax-xmin ) > 32768 ) || ( abs ( xmid-xmin ) > 32768 ) || ( abs ( xmax-xmid ) > 32768 )
            ||  (abs( ymax-ymin ) > 32768 ) || ( abs ( ymid-ymin ) > 32768 ) || ( abs ( ymax-ymid ) > 32768 )
            ||  (xmin > 32768) || (xmid > 32768))
      {
            dda8 = true;

            dy = ( ymax - ymin );
            if ( dy )
            {
                  m = ( xmax - xmin ) << 8;
                  m /= dy;

                  x = xmin << 8;

                  for ( count = ymin; count <= ymax; count++ )
                  {
                        if ( ( count >= 0 ) && ( count < 1500 ) )
                              ledge[count] = x >> 8;
                        x += m;
                  }
            }

            dy = ( ymid - ymin );
            if ( dy )
            {
                  m = ( xmid - xmin ) << 8;
                  m /= dy;

                  x = xmin << 8;

                  for ( count = ymin; count <= ymid; count++ )
                  {
                        if ( ( count >= 0 ) && ( count < 1500 ) )
                              redge[count] = x >> 8;
                        x += m;
                  }
            }

            dy = ( ymax - ymid );
            if ( dy )
            {
                  m = ( xmax - xmid ) << 8;
                  m /= dy;

                  x = xmid << 8;

                  for ( count = ymid; count <=ymax; count++ )
                  {
                        if ( ( count >= 0 ) && ( count < 1500 ) )
                              redge[count] = x >> 8;
                        x += m;
                  }
            }

            double ddfSum = 0;
            //      Check the triangle edge winding direction
            ddfSum += (xmin/1) * (ymax/1) - (ymin/1) * (xmax/1);
            ddfSum += (xmax/1) * (ymid/1) - (ymax/1) * (xmid/1);
            ddfSum += (xmid/1) * (ymin/1) - (ymid/1) * (xmin/1);
            cw = ddfSum < 0;

      }
      else
      {

      dy = ( ymax - ymin );
      if ( dy )
      {
            m = ( xmax - xmin ) << 16;
            m /= dy;

            x = xmin << 16;

            for ( count = ymin; count <= ymax; count++ )
            {
                  if ( ( count >= 0 ) && ( count < 1500 ) )
                        ledge[count] = x >> 16;
                  x += m;
            }
      }

      dy = ( ymid - ymin );
      if ( dy )
      {
            m = ( xmid - xmin ) << 16;
            m /= dy;

            x = xmin << 16;

            for ( count = ymin; count <= ymid; count++ )
            {
                  if ( ( count >= 0 ) && ( count < 1500 ) )
                        redge[count] = x >> 16;
                  x += m;
            }
      }

      dy = ( ymax - ymid );
      if ( dy )
      {
            m = ( xmax - xmid ) << 16;
            m /= dy;

            x = xmid << 16;

            for ( count = ymid; count <=ymax; count++ )
            {
                  if ( ( count >= 0 ) && ( count < 1500 ) )
                        redge[count] = x >> 16;
                  x += m;
            }
      }

            //      Check the triangle edge winding direction
      long dfSum = 0;
      dfSum += xmin * ymax - ymin * xmax;
      dfSum += xmax * ymid - ymax * xmid;
      dfSum += xmid * ymin - ymid * xmin;

      cw = dfSum < 0;

      }     // else

      //      if cw is true, redge is actually on the right


      int y1 = ymax;
      int y2 = ymin;

      int ybt = pb_spec->y;
      int yt = pb_spec->y + pb_spec->height;

      if ( y1 > yt )
            y1 = yt;
      if ( y1 < ybt )
            y1 = ybt;

      if ( y2 > yt )
            y2 = yt;
      if ( y2 < ybt )
            y2 = ybt;

      int lclip = pb_spec->lclip;
      int rclip = pb_spec->rclip;


      //              Clip the triangle
      if ( cw )
      {
            for ( int iy = y2 ; iy <= y1 ; iy++ )
            {
                  if ( ledge[iy] < lclip )
                  {
                        if ( redge[iy] < lclip )
                              ledge[iy] = -1;
                        else
                              ledge[iy] = lclip;
                  }

                  if ( redge[iy] > rclip )
                  {
                        if ( ledge[iy] > rclip )
                              ledge[iy] = -1;
                        else
                              redge[iy] = rclip;
                  }
            }
      }
      else
      {
            for ( int iy = y2 ; iy <= y1 ; iy++ )
            {
                  if ( redge[iy] < lclip )
                  {
                        if ( ledge[iy] < lclip )
                              ledge[iy] = -1;
                        else
                              redge[iy] = lclip;
                  }

                  if ( ledge[iy] > rclip )
                  {
                        if ( redge[iy] > rclip )
                              ledge[iy] = -1;
                        else
                              ledge[iy] = rclip;
                  }
            }
      }



      //              Fill the triangle

      int ya = y2;
      int yb = y1;

      unsigned char *pix_buff = pb_spec->pix_buff;

      int patt_size_x = 0, patt_size_y = 0, patt_pitch = 0;
      unsigned char *patt_s0 = NULL;
      if ( pPatt_spec )
      {
            patt_size_y = pPatt_spec->height;
            patt_size_x = pPatt_spec->width;
            patt_pitch =  pPatt_spec->pb_pitch;
            patt_s0 =     pPatt_spec->pix_buff;
      }



      if ( pb_spec->depth == 24 )
      {
            for ( int iyp = ya ; iyp < yb ; iyp++ )
            {
                  if ( ( iyp >= ybt ) && ( iyp < yt ) )
                  {
                        int yoff = ( iyp - pb_spec->y ) * pb_spec->pb_pitch;

                        unsigned char *py =  pix_buff + yoff;

                        int ix, ixm;
                        if ( cw )
                        {
                              ix = ledge[iyp];
                              ixm = redge[iyp];
                        }
                        else
                        {
                              ixm = ledge[iyp];
                              ix = redge[iyp];
                        }


                        if ( ledge[iyp] != -1 )
                        {

                              //    This would be considered a failure of the dda algorithm
                              //    Happens on very high zoom, with very large triangles.
                              //    The integers of the dda algorithm don't have enough bits...
                              //    Anyway, just ignore this triangle if it happens
                              if(ix > ixm)
                                    continue;

                              int xoff = ( ix-pb_spec->x ) * 3;

                              unsigned char *px =  py  + xoff;


                              if ( pPatt_spec )       // Pattern
                              {
                                    int y_stagger = ( iyp -pPatt_spec->y )   / patt_size_y;
                                    int x_stagger_off = 0;
                                    if ( ( y_stagger & 1 ) && pPatt_spec->b_stagger )
                                          x_stagger_off = pPatt_spec->width / 2;

                                    int patt_y = abs(( iyp -pPatt_spec->y ))   % patt_size_y;

                                    unsigned char *pp0 = patt_s0 + ( patt_y * patt_pitch );

                                    while ( ix <= ixm )
                                    {

                                          int patt_x = abs(( ( ix - pPatt_spec->x ) + x_stagger_off )   % patt_size_x);

                                          unsigned char *pp = pp0 + patt_x * 3;
                                          //  Todo    This line assumes unused_color is always 0,0,0
                                          if ( *pp && * ( pp+1 ) && * ( pp+2 ) )
                                          {
                                                *px++ = *pp++;
                                                *px++ = *pp++;
                                                *px++ = *pp++;
                                          }
                                          else
                                          {
                                                px+=3;
                                                pp+=3;
                                          }

                                          ix++;
                                    }
                              }


                              else                    // No Pattern
                              {
#if defined( __WXGTK__) && defined(__INTEL__)
#define memset3(dest, value, count) \
__asm__ __volatile__ ( \
"cmp $0,%2\n\t" \
"jg 2f\n\t" \
"je 3f\n\t" \
"jmp 4f\n\t" \
"2:\n\t" \
"movl  %0,(%1)\n\t" \
"add $3,%1\n\t" \
"dec %2\n\t" \
"jnz 2b\n\t" \
"3:\n\t" \
"movb %b0,(%1)\n\t" \
"inc %1\n\t" \
"movb %h0,(%1)\n\t" \
"inc %1\n\t" \
"shr $16,%0\n\t" \
"movb %b0,(%1)\n\t" \
"4:\n\t" \
: : "a"(value), "D"(dest), "r"(count) :  );

                                    int count = ixm-ix;
                                    memset3 ( px, color_int, count )
#else

                                    while ( ix <= ixm )
                                    {
                                          *px++ = b;
                                          *px++ = g;
                                          *px++ = r;

                                          ix++;
                                    }
#endif
                              }
                        }
                  }
            }
      }

      if ( pb_spec->depth == 32 )
      {

            assert ( ya <= yb );

            for ( int iyp = ya ; iyp < yb ; iyp++ )
            {
                  if ( ( iyp >= ybt ) && ( iyp < yt ) )
                  {
                        int yoff = ( iyp - pb_spec->y ) * pb_spec->pb_pitch;

                        unsigned char *py = pix_buff + yoff;


                        int ix, ixm;
                        if ( cw )
                        {
                              ix = ledge[iyp];
                              ixm = redge[iyp];
                        }
                        else
                        {
                              ixm = ledge[iyp];
                              ix = redge[iyp];
                        }

                        if ( ledge[iyp] != -1 )
                        {
                              //    This would be considered a failure of the dda algorithm
                              //    Happens on very high zoom, with very large triangles.
                              //    The integers of the dda algorithm don't have enough bits...
                              //    Anyway, just ignore this triangle if it happens
                              if(ix > ixm)
                                    continue;

                              int xoff = ( ix-pb_spec->x ) * pb_spec->depth / 8;

                              unsigned char *px = py + xoff;


                              if ( pPatt_spec )       // Pattern
                              {
                                    int y_stagger = ( iyp -pPatt_spec->y )   / patt_size_y;

                                    int x_stagger_off = 0;
                                    if ( ( y_stagger & 1 ) && pPatt_spec->b_stagger )
                                          x_stagger_off = pPatt_spec->width / 2;

                                    int patt_y = abs(( iyp -pPatt_spec->y ))   % patt_size_y;

                                    unsigned char *pp0 = patt_s0 + ( patt_y * patt_pitch );

                                    while ( ix <= ixm )
                                    {
                                          int patt_x = abs(( ( ix - pPatt_spec->x ) + x_stagger_off )   % patt_size_x);

                                          unsigned char *pp = pp0 + patt_x * 4;

                                          //  Todo    This line assumes unused_color is always 0,0,0
                                          if ( *pp && * ( pp+1 ) && * ( pp+2 ) )
                                          {
                                                *px++ = *pp++;
                                                *px++ = *pp++;
                                                *px++ = *pp++;
                                                px++;
                                                pp++;
                                          }
                                          else
                                          {
                                                px+=4;
                                                pp+=4;
                                          }

                                          ix++;
                                    }
                              }

                              else                    // No Pattern
                              {
                                    int *pxi = ( int * ) px ;
                                    while ( ix <= ixm )
                                    {
                                          *pxi++ = color_int;
                                          ix++;
                                    }
                              }

                        }
                  }
            }
      }

      return true;
}

//----------------------------------------------------------------------------------
//
//              Render Trapezoid
//
//----------------------------------------------------------------------------------
inline int s52plib::dda_trap ( wxPoint *segs, int lseg, int rseg, int ytop, int ybot, S52color *c, render_canvas_parms *pb_spec, render_canvas_parms *pPatt_spec )
{
      unsigned char r = 0, g = 0, b = 0;

      if ( NULL != c )
      {
#ifdef ocpnUSE_ocpnBitmap
            r = c->R;
            g = c->G;
            b = c->B;
#else
            b = c->R;
            g = c->G;
            r = c->B;
#endif
      }

      //      Color Debug
      /*    int fc = rand();
      b = fc & 0xff;
      g = fc & 0xff;
      r = fc & 0xff;
      */

//      int debug = 0;
      int ret_val = 0;

      int color_int = 0;
      if ( NULL != c )
            color_int = ( ( r ) << 16 ) + ( ( g ) << 8 ) + ( b );

      //      Create edge arrays using fast integer DDA

      int lclip = pb_spec->lclip;
      int rclip = pb_spec->rclip;

      int m, x, dy, count;

      //    Left edge
      int xmax = segs[lseg].x;
      int xmin = segs[lseg+1].x;
      int ymax = segs[lseg].y;
      int ymin = segs[lseg+1].y;

      if ( ymax < ymin )
      {
            int a = ymax;
            ymax = ymin;
            ymin = a;

            a = xmax;
            xmax = xmin;
            xmin = a;
      }

      int y_dda_limit = wxMin ( ybot, ymax );
      y_dda_limit = wxMin ( y_dda_limit, 1499 );            // don't overrun edge array

      //    Some peephole optimization:
      //    if xmax and xmin are both < 0, arrange to simply fill the ledge array with 0
      if ( ( xmax < 0 ) && ( xmin < 0 ) )
      {
            xmax = -2;
            xmin = -2;
      }
      //    if xmax and xmin are both > rclip, arrange to simply fill the ledge array with rclip + 1
      //    This may induce special clip case below, and cause trap not to be rendered
      else if ( ( xmax > rclip ) && ( xmin > rclip ) )
      {
            xmax = rclip + 1;
            xmin = rclip + 1;
      }


      dy = ( ymax - ymin );
      if ( dy )
      {
            m = ( xmax - xmin ) << 16;
            m /= dy;

            x = xmin << 16;

            //TODO implement this logic in dda_tri also
            count = ymin;
            while ( count < 0 )
            {
                  x += m;
                  count++;
            }

            while ( count < y_dda_limit )
            {
                  ledge[count] = x >> 16;
                  x += m;
                  count++;
            }
      }


      if ( ( ytop < ymin ) || ( ybot > ymax ) )
      {
//            printf ( "### ledge out of range\n" );
            ret_val = 1;
//            r=255;
//            g=0;
//            b=0;
      }

      //    Right edge
      xmax = segs[rseg].x;
      xmin = segs[rseg+1].x;
      ymax = segs[rseg].y;
      ymin = segs[rseg+1].y;


//Note this never gets hit???
      if ( ymax < ymin )
      {
            int a = ymax;
            ymax = ymin;
            ymin = a;

            a = xmax;
            xmax = xmin;
            xmin = a;
      }


      //    Some peephole optimization:
      //    if xmax and xmin are both < 0, arrange to simply fill the redge array with -1
      //    This may induce special clip case below, and cause trap not to be rendered
      if ( ( xmax < 0 ) && ( xmin < 0 ) )
      {
            xmax = -1;
            xmin = -1;
      }

      //    if xmax and xmin are both > rclip, arrange to simply fill the redge array with rclip + 1
      //    This may induce special clip case below, and cause trap not to be rendered
      else if ( ( xmax > rclip ) && ( xmin > rclip ) )
      {
            xmax = rclip + 1;
            xmin = rclip + 1;
      }

      y_dda_limit = wxMin ( ybot, ymax );
      y_dda_limit = wxMin ( y_dda_limit, 1499 );            // don't overrun edge array

      dy = ( ymax - ymin );
      if ( dy )
      {
            m = ( xmax - xmin ) << 16;
            m /= dy;

            x = xmin << 16;

            count = ymin;
            while ( count < 0 )
            {
                  x += m;
                  count++;
            }

            while ( count < y_dda_limit )
            {
                  redge[count] = x >> 16;
                  x += m;
                  count++;
            }
      }


      if ( ( ytop < ymin ) || ( ybot > ymax ) )
      {
//            printf ( "### redge out of range\n" );
            ret_val = 1;
//            r=255;
//            g=0;
//            b=0;
      }

      //    Clip trapezoid to height spec
      int y1 = ybot;
      int y2 = ytop;


      int ybt = pb_spec->y;
      int yt = pb_spec->y + pb_spec->height;


      if ( y1 > yt )
            y1 = yt;
      if ( y1 < ybt )
            y1 = ybt;

      if ( y2 > yt )
            y2 = yt;
      if ( y2 < ybt )
            y2 = ybt;




      //   Clip the trapezoid to width
      for ( int iy = y2 ; iy <= y1 ; iy++ )
      {
            if ( ledge[iy] < lclip )
            {
                  if ( redge[iy] < lclip )
                        ledge[iy] = -1;
                  else
                        ledge[iy] = lclip;
            }

            if ( redge[iy] > rclip )
            {
                  if ( ledge[iy] > rclip )
                        ledge[iy] = -1;
                  else
                        redge[iy] = rclip;
            }
      }


      //    Fill the trapezoid

      int ya = y2;
      int yb = y1;

      unsigned char *pix_buff = pb_spec->pix_buff;

      int patt_size_x = 0;
      int patt_size_y = 0;
      int patt_pitch = 0;
      unsigned char *patt_s0 = NULL;
      if ( pPatt_spec )
      {
            patt_size_y = pPatt_spec->height;
            patt_size_x = pPatt_spec->width;
            patt_pitch =  pPatt_spec->pb_pitch;
            patt_s0 =     pPatt_spec->pix_buff;
      }



      if ( pb_spec->depth == 24 )
      {
            for ( int iyp = ya ; iyp < yb ; iyp++ )
            {
                  if ( ( iyp >= ybt ) && ( iyp < yt ) )
                  {
                        int yoff = ( iyp - pb_spec->y ) * pb_spec->pb_pitch;

                        unsigned char *py =  pix_buff + yoff;

                        int ix, ixm;
                        ix = ledge[iyp];
                        ixm = redge[iyp];

//                        if(debug) printf("iyp %d, ix %d, ixm %d\n", iyp, ix, ixm);
//                           int ix = ledge[iyp];
//                            if(ix != -1)                    // special clip case
                        if ( ledge[iyp] != -1 )
                        {
                              int xoff = ( ix-pb_spec->x ) * 3;

                              unsigned char *px =  py  + xoff;

                              if ( pPatt_spec )       // Pattern
                              {
                                   int y_stagger = ( iyp -pPatt_spec->y )   / patt_size_y;
                                   int x_stagger_off = 0;
                                   if ( ( y_stagger & 1 ) && pPatt_spec->b_stagger )
                                          x_stagger_off = pPatt_spec->width / 2;

                                   int patt_y = abs(( iyp -pPatt_spec->y ))   % patt_size_y;
                                   unsigned char *pp0 = patt_s0 + ( patt_y * patt_pitch );

                                    while ( ix <= ixm )
                                    {
                                          int patt_x = abs(( ( ix - pPatt_spec->x ) + x_stagger_off )   % patt_size_x);

                                          unsigned char *pp = pp0 + patt_x * 3;

                                          //  Todo    This line assumes unused_color is always 0,0,0
                                          if ( *pp && * ( pp+1 ) && * ( pp+2 ) )
                                          {
                                                *px++ = *pp++;
                                                *px++ = *pp++;
                                                *px++ = *pp++;
                                          }
                                          else
                                          {
                                                px+=3;
                                                pp+=3;
                                          }

                                          ix++;
                                    }
                              }


                              else                    // No Pattern
                              {
#if defined(__WXGTK__WITH_OPTIMIZE_0) && defined(__INTEL__)
#define memset3d(dest, value, count) \
                                    __asm__ __volatile__ ( \
                                    "cmp $0,%2\n\t" \
                                    "jg ld0\n\t" \
                                    "je ld1\n\t" \
                                    "jmp ld2\n\t" \
                                    "ld0:\n\t" \
                                    "movl  %0,(%1)\n\t" \
                                    "add $3,%1\n\t" \
                                    "dec %2\n\t" \
                                    "jnz ld0\n\t" \
                                    "ld1:\n\t" \
                                    "movb %b0,(%1)\n\t" \
                                    "inc %1\n\t" \
                                    "movb %h0,(%1)\n\t" \
                                    "inc %1\n\t" \
                                    "shr $16,%0\n\t" \
                                    "movb %b0,(%1)\n\t" \
                                    "ld2:\n\t" \
                                          : : "a"(value), "D"(dest), "r"(count) :  );
                                    int count = ixm-ix;
                                    memset3d ( px, color_int, count )
#else

                                    while ( ix <= ixm )
                                    {
                                          *px++ = b;
                                          *px++ = g;
                                          *px++ = r;

                                          ix++;
                                    }
#endif
                              }
                        }
                  }
            }
      }

      if ( pb_spec->depth == 32 )
      {

            assert ( ya <= yb );

            for ( int iyp = ya ; iyp < yb ; iyp++ )
            {
                  if ( ( iyp >= ybt ) && ( iyp < yt ) )
                  {
                        int yoff = ( iyp - pb_spec->y ) * pb_spec->pb_pitch;

                        unsigned char *py = pix_buff + yoff;


                        int ix, ixm;
                        ix = ledge[iyp];
                        ixm = redge[iyp];

                        if ( ledge[iyp] != -1 )
                        {
                              int xoff = ( ix-pb_spec->x ) * pb_spec->depth / 8;

                              unsigned char *px = py + xoff;


                              if ( pPatt_spec )       // Pattern
                              {
                                    int y_stagger = ( iyp -pPatt_spec->y )   / patt_size_y;
                                    int x_stagger_off = 0;
                                    if ( ( y_stagger & 1 ) && pPatt_spec->b_stagger )
                                          x_stagger_off = pPatt_spec->width / 2;

                                    int patt_y = abs(( iyp -pPatt_spec->y ))   % patt_size_y;
                                    unsigned char *pp0 = patt_s0 + ( patt_y * patt_pitch );

                                    while ( ix <= ixm )
                                    {
                                          int patt_x = abs(( ( ix - pPatt_spec->x ) + x_stagger_off )   % patt_size_x);

                                          unsigned char *pp = pp0 + patt_x * 4;

                                          //  TODO    This line assumes unused_color is always 0,0,0
                                          if ( *pp && * ( pp+1 ) && * ( pp+2 ) )
                                          {
                                                *px++ = *pp++;
                                                *px++ = *pp++;
                                                *px++ = *pp++;
                                                px++;
                                                pp++;
                                          }
                                          else
                                          {
                                                px+=4;
                                                pp+=4;
                                          }

                                          ix++;
                                    }
                              }

                              else                    // No Pattern
                              {
                                    int *pxi = ( int * ) px ;
                                    while ( ix <= ixm )
                                    {
                                          *pxi++ = color_int;
                                          ix++;
                                    }
                              }

                        }
                  }
            }
      }

      return ret_val;
}



void s52plib::RenderToBufferFilledPolygon ( ObjRazRules *rzRules, S57Obj *obj, S52color *c, wxBoundingBox &BBView,
        render_canvas_parms *pb_spec, render_canvas_parms *pPatt_spec )
{
      S52color cp;
      if ( NULL != c )
      {
            cp.R = c->R;
            cp.G = c->G;
            cp.B = c->B;
      }

      if ( obj->pPolyTessGeo )
      {
            if(!rzRules->obj->pPolyTessGeo->IsOk())               // perform deferred tesselation
                  rzRules->obj->pPolyTessGeo->BuildTessGL();

            wxPoint *pp3 = ( wxPoint * ) malloc ( 3 * sizeof ( wxPoint ) );
            wxPoint *ptp = ( wxPoint * ) malloc ( ( obj->pPolyTessGeo->GetnVertexMax() + 1 ) * sizeof ( wxPoint ) );

            //  Allow a little slop in calculating whether a triangle
            //  is within the requested Viewport
            double margin = BBView.GetWidth() * .05;

            PolyTriGroup *ppg = obj->pPolyTessGeo->Get_PolyTriGroup_head();

            TriPrim *p_tp = ppg->tri_prim_head;
            while ( p_tp )
            {
                  bool b_greenwich = false;
                  if ( BBView.GetMaxX() > 360. )
                  {
                        wxBoundingBox bbRight ( 0., BBView.GetMinY(), BBView.GetMaxX() - 360., BBView.GetMaxY() );
                        if ( bbRight.Intersect ( * ( p_tp->p_bbox ), margin ) != _OUT )
                              b_greenwich = true;
                  }

                  if ( b_greenwich || (BBView.Intersect ( * ( p_tp->p_bbox ), margin ) != _OUT ))
                  {
                        //      Get and convert the points
                        wxPoint *pr = ptp;

                        double *pvert_list = p_tp->p_vertex;

                        for ( int iv =0 ; iv < p_tp->nVert ; iv++ )
                        {
                              double lon = *pvert_list++;
                              double lat = *pvert_list++;
                              rzRules->chart->GetPointPix ( rzRules, lat, lon, pr );

                              pr++;
                        }


                        switch ( p_tp->type )
                        {
                              case PTG_TRIANGLE_FAN:
                              {
                                    for ( int it = 0 ; it < p_tp->nVert - 2 ; it++ )
                                    {
                                          pp3[0].x = ptp[0].x;
                                          pp3[0].y = ptp[0].y;

                                          pp3[1].x = ptp[it+1].x;
                                          pp3[1].y = ptp[it+1].y;

                                          pp3[2].x = ptp[it+2].x;
                                          pp3[2].y = ptp[it+2].y;

                                          dda_tri ( pp3, &cp, pb_spec, pPatt_spec );
                                    }
                                    break;
                              }
                              case PTG_TRIANGLE_STRIP:
                              {
                                    for ( int it = 0 ; it < p_tp->nVert - 2 ; it++ )
                                    {
                                          pp3[0].x = ptp[it].x;
                                          pp3[0].y = ptp[it].y;

                                          pp3[1].x = ptp[it+1].x;
                                          pp3[1].y = ptp[it+1].y;

                                          pp3[2].x = ptp[it+2].x;
                                          pp3[2].y = ptp[it+2].y;

                                          dda_tri ( pp3, &cp, pb_spec, pPatt_spec );
                                    }
                                    break;
                              }
                              case PTG_TRIANGLES:
                              {

                                    for ( int it = 0 ; it < p_tp->nVert ; it+=3 )
                                    {
                                          pp3[0].x = ptp[it].x;
                                          pp3[0].y = ptp[it].y;

                                          pp3[1].x = ptp[it+1].x;
                                          pp3[1].y = ptp[it+1].y;

                                          pp3[2].x = ptp[it+2].x;
                                          pp3[2].y = ptp[it+2].y;

                                          dda_tri ( pp3, &cp, pb_spec, pPatt_spec );
                                    }
                                    break;

                              }
                        }
                  }   // if bbox
                  p_tp = p_tp->p_next;                // pick up the next in chain
            }       // while
            free ( ptp );
            free ( pp3 );
      }       // if pPolyTessGeo

      else if ( obj->pPolyTrapGeo )
      {
            if(!rzRules->obj->pPolyTrapGeo->IsOk())
                  rzRules->obj->pPolyTrapGeo->BuildTess();

            S52color cs;
            cs.R = 255;
            cs.G = 0;
            cs.B = 0;


            if ( obj->pPolyTrapGeo->IsOk()  /*&& (obj->Index == 7) && ( obj->pPolyTrapGeo->GetnVertexMax() < 1000)*/)
            {
                  PolyTrapGroup *ptg = obj->pPolyTrapGeo->Get_PolyTrapGroup_head();

                  //  Convert the segment array to screen coordinates
                  int nVertex = obj->pPolyTrapGeo->GetnVertexMax();
                  wxPoint *ptp = ( wxPoint * ) malloc ( ( nVertex + 1 ) * sizeof ( wxPoint ) );

                  rzRules->chart->GetPointPix ( rzRules, obj->pPolyTrapGeo->Get_PolyTrapGroup_head()->ptrapgroup_geom, ptp, nVertex );

                  //  Render the trapezoids
                  int ntraps = ptg->ntrap_count;
                  trapz_t *ptraps = ptg->trap_array;

                  for ( int i=0 ; i < ntraps ; i++ )
                  {
                        cs.R = 0;
                        cs.G = 255;
                        cs.B = 0;

                        int lseg = ptraps->ilseg;
                        int rseg = ptraps->irseg;

                        //    Get the screen co-ordinates of top and bottom of trapezoid,
                        //    understanding that ptraps->hiy is the upper line
                        wxPoint pr;
                        rzRules->chart->GetPointPix ( rzRules, ptraps->hiy, 0., &pr );
                        int trap_y_top = pr.y;

                        rzRules->chart->GetPointPix ( rzRules, ptraps->loy, 0., &pr );
                        int trap_y_bot = pr.y;

                        S52color *cd = &cp;
                        if ( ptg->m_trap_error )
                              cd = &cs;


                        int trap_height = trap_y_bot - trap_y_top;

                        //    Clip the trapezoid array to the render_canvas_parms dimensions
                        if ( ( trap_y_top >= pb_spec->y - trap_height ) && ( trap_y_bot <= pb_spec->y + pb_spec->height + trap_height ) )
                        {
/*
                              if(obj->Index == 7)
                              {
                                    int clip_top =  pb_spec->y - trap_height;
                                    int clip_bot = pb_spec->y + pb_spec->height + trap_height;
                                    printf("Trap %d pb_spec-> %d clip_top %d clip_bot %d\n", i, pb_spec->y, clip_top, clip_bot);
                                    printf("Trap %d  lseg: %d   rseg: %d   loy: %d   hiy: %d\n", i, lseg, rseg, trap_y_top, trap_y_bot);
                              }
*/
//                              if((lseg == 66) && (trap_y_top < 0))
  //                                    cs.B = 128;

                              dda_trap ( ptp, lseg, rseg, trap_y_top, trap_y_bot, cd, pb_spec, pPatt_spec );


                        }

                        ptraps++;
                  }
                  free ( ptp );
            }   // if OK
      }       // if pPolyTrapGeo


}

int s52plib::RenderToGLAC ( ObjRazRules *rzRules, Rules *rules, ViewPort *vp )
{
      S52color *c;
      char *str = ( char* ) rules->INSTstr;

      c = ps52plib->S52_getColor ( str );

      glColor3ub( c->R, c->G, c->B );


      wxBoundingBox BBView = vp->GetBBox();
      if ( rzRules->obj->pPolyTessGeo )
      {
            if(!rzRules->obj->pPolyTessGeo->IsOk())               // perform deferred tesselation
                  rzRules->obj->pPolyTessGeo->BuildTessGL();

            wxPoint *ptp = ( wxPoint * ) malloc ( ( rzRules->obj->pPolyTessGeo->GetnVertexMax() + 1 ) * sizeof ( wxPoint ) );

            //  Allow a little slop in calculating whether a triangle
            //  is within the requested Viewport
            double margin = BBView.GetWidth() * .05;

            PolyTriGroup *ppg = rzRules->obj->pPolyTessGeo->Get_PolyTriGroup_head();

            TriPrim *p_tp = ppg->tri_prim_head;
            while ( p_tp )
            {
                  bool b_greenwich = false;
                  if ( BBView.GetMaxX() > 360. )
                  {
                        wxBoundingBox bbRight ( 0., vp->GetBBox().GetMinY(), vp->GetBBox().GetMaxX() - 360., vp->GetBBox().GetMaxY() );
                        if ( bbRight.Intersect ( * ( p_tp->p_bbox ), margin ) != _OUT )
                              b_greenwich = true;
                  }

                  if ( b_greenwich || (BBView.Intersect ( * ( p_tp->p_bbox ), margin ) != _OUT ))
                  {
                        //      Get and convert the points
/*
                        wxPoint *pr = ptp;
                        double *pvert_list = p_tp->p_vertex;

                        for ( int iv =0 ; iv < p_tp->nVert ; iv++ )
                        {
                              double lon = *pvert_list++;
                              double lat = *pvert_list++;
                              rzRules->chart->GetPointPix ( rzRules, lat, lon, pr );

                              pr++;
                        }
*/
                        rzRules->chart->GetPointPix (  rzRules, (wxPoint2DDouble*)p_tp->p_vertex, ptp, p_tp->nVert );


                        switch ( p_tp->type )
                        {
                              case PTG_TRIANGLE_FAN:
                              {
                                    glBegin(GL_TRIANGLE_FAN);
                                    for ( int it = 0 ; it < p_tp->nVert ; it++ )
                                          glVertex2f(ptp[it].x, ptp[it].y);
                                    glEnd();
                                    break;
                              }

                              case PTG_TRIANGLE_STRIP:
                              {
                                    glBegin(GL_TRIANGLE_STRIP);
                                    for ( int it = 0 ; it < p_tp->nVert ; it++ )
                                          glVertex2f(ptp[it].x, ptp[it].y);
                                    glEnd();
                                    break;
                              }
                              case PTG_TRIANGLES:
                              {
                                    for ( int it = 0 ; it < p_tp->nVert ; it+=3 )
                                    {
                                          int xmin = wxMin(ptp[it].x, wxMin(ptp[it+1].x, ptp[it+2].x));
                                          int xmax = wxMax(ptp[it].x, wxMax(ptp[it+1].x, ptp[it+2].x));
                                          int ymin = wxMin(ptp[it].y, wxMin(ptp[it+1].y, ptp[it+2].y));
                                          int ymax = wxMax(ptp[it].y, wxMax(ptp[it+1].y, ptp[it+2].y));

                                          wxRect rect(xmin, ymin, xmax-xmin, ymax-ymin);
                                          if(rect.Intersects(m_render_rect))
                                          {
                                                glBegin(GL_TRIANGLES);
                                                glVertex2f(ptp[it].x, ptp[it].y);
                                                glVertex2f(ptp[it+1].x, ptp[it+1].y);
                                                glVertex2f(ptp[it+2].x, ptp[it+2].y);
                                                glEnd();
                                          }
                                    }
                                    break;
                              }
                        }
                  }   // if bbox
                  p_tp = p_tp->p_next;                // pick up the next in chain
            }       // while
            free ( ptp );
      }       // if pPolyTessGeo



#if 0
      //    At very small scales, the object could be visible on both the left and right sides of the screen.
      //    Identify this case......
      if(vp->chart_scale > 5e7)
      {
            //    Does the object hang out over the left side of the VP?
            if((rzRules->obj->BBObj.GetMaxX() > vp->GetBBox().GetMinX()) && (rzRules->obj->BBObj.GetMinX() < vp->GetBBox().GetMinX()))
            {
                  //    If we add 360 to the objects lons, does it intersect the the right side of the VP?
                  if(((rzRules->obj->BBObj.GetMaxX() + 360.) > vp->GetBBox().GetMaxX()) && ((rzRules->obj->BBObj.GetMinX() + 360.) < vp->GetBBox().GetMaxX()))
                  {
                        //  If so, this area oject should be drawn again, this time for the left side
                        //    Do this by temporarily adjusting the objects rendering offset
                        rzRules->obj->x_origin -= mercator_k0 * WGS84_semimajor_axis_meters * 2.0 * PI;
                        RenderToBufferFilledPolygon ( rzRules, rzRules->obj, c, vp->GetBBox(), pb_spec, NULL );
                        rzRules->obj->x_origin += mercator_k0 * WGS84_semimajor_axis_meters * 2.0 * PI;

                  }
            }
      }
#endif

      return 1;
}


int s52plib::RenderToGLAP ( ObjRazRules *rzRules, Rules *rules, ViewPort *vp )
{

      int obj_xmin =  10000;
      int obj_xmax = -10000;
      int obj_ymin =  10000;
      int obj_ymax = -10000;

      double z_clip_geom = 1.0;
      double z_tex_geom = 0.;

      GLuint clip_list;

      wxBoundingBox BBView = vp->GetBBox();
            //  Allow a little slop in calculating whether a triangle
            //  is within the requested Viewport
      double margin = BBView.GetWidth() * .05;

      wxPoint *ptp;
      if ( rzRules->obj->pPolyTessGeo )
      {
            if(!rzRules->obj->pPolyTessGeo->IsOk())               // perform deferred tesselation
                  rzRules->obj->pPolyTessGeo->BuildTessGL();

            ptp = ( wxPoint * ) malloc ( ( rzRules->obj->pPolyTessGeo->GetnVertexMax() + 1 ) * sizeof ( wxPoint ) );
      }
      else
            return 0;

      if(g_b_useStencil)
      {
            glPushAttrib(GL_STENCIL_BUFFER_BIT);

      //    Use masked bit "1" of the stencil buffer to create a stencil for the area of interest

            glEnable (GL_STENCIL_TEST);
            glStencilMask(0x2);                 // write only into bit 1 of the stencil buffer
            glColorMask(false, false, false, false);  // Disable writing to the color buffer
            glClear(GL_STENCIL_BUFFER_BIT);

            //    We are going to write "2" into the stencil buffer wherever the object is valid
            glStencilFunc (GL_ALWAYS, 2, 2);
            glStencilOp (GL_KEEP, GL_KEEP, GL_REPLACE);

            glColor3ub( 0, 254, 0 );            // any color will do

      }
      else
      {
            glPushAttrib(GL_DEPTH_BUFFER_BIT);

            glEnable(GL_DEPTH_TEST); // to use the depth test
            glDepthFunc(GL_GREATER); // Respect global render mask in depth buffer
            glDepthMask(GL_TRUE);    // to allow writes to the depth buffer
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);   // disable color buffer

            glColor3f(1,1,0);


            //    Overall chart clip buffer was set at z=0.5
            //    Draw this clip geometry at z = .25, so still respecting the previously established clip region
            //    Subsequent drawing to this area at z=.25  will pass only this area if using glDepthFunc(GL_EQUAL);

            //    TODO  If this fails, consider the uncertainty of GL_EQUAL on floating point comparison
            z_clip_geom = .25;
            z_tex_geom =  .25;
      }

      //  Render the geometry
      {
            // Generate a Display list if using Depth Buffer clipping, for use later
            if(!g_b_useStencil)
            {
                  clip_list = glGenLists (1);
                  glNewList(clip_list, GL_COMPILE);
            }


            PolyTriGroup *ppg = rzRules->obj->pPolyTessGeo->Get_PolyTriGroup_head();

            TriPrim *p_tp = ppg->tri_prim_head;
            while ( p_tp )
            {
                  bool b_greenwich = false;
                  if ( BBView.GetMaxX() > 360. )
                  {
                        wxBoundingBox bbRight ( 0., vp->GetBBox().GetMinY(), vp->GetBBox().GetMaxX() - 360., vp->GetBBox().GetMaxY() );
                        if ( bbRight.Intersect ( * ( p_tp->p_bbox ), margin ) != _OUT )
                              b_greenwich = true;
                  }

                  if ( b_greenwich || (BBView.Intersect ( * ( p_tp->p_bbox ), margin ) != _OUT ))
                  {

                        //      Get and convert the points

                        wxPoint *pr = ptp;
                        double *pvert_list = p_tp->p_vertex;

                        for ( int iv =0 ; iv < p_tp->nVert ; iv++ )
                        {
                        double lon = *pvert_list++;
                        double lat = *pvert_list++;
                        rzRules->chart->GetPointPix ( rzRules, lat, lon, pr );

                        obj_xmin = wxMin(obj_xmin, pr->x);
                        obj_xmax = wxMax(obj_xmax, pr->x);
                        obj_ymin = wxMin(obj_ymin, pr->y);
                        obj_ymax = wxMax(obj_ymax, pr->y);

                        pr++;
                  }

//                        rzRules->chart->GetPointPix (  rzRules, (wxPoint2DDouble*)p_tp->p_vertex, ptp, p_tp->nVert );


                        switch ( p_tp->type )
                        {
                              case PTG_TRIANGLE_FAN:
                              {
                                    glBegin(GL_TRIANGLE_FAN);
                                    for ( int it = 0 ; it < p_tp->nVert ; it++ )
                                          glVertex3f(ptp[it].x, ptp[it].y, z_clip_geom);
                                    glEnd();
                                    break;
                              }

                              case PTG_TRIANGLE_STRIP:
                              {
                                    glBegin(GL_TRIANGLE_STRIP);
                                    for ( int it = 0 ; it < p_tp->nVert ; it++ )
                                          glVertex3f(ptp[it].x, ptp[it].y, z_clip_geom);
                                    glEnd();
                                    break;
                              }
                              case PTG_TRIANGLES:
                              {
                                    for ( int it = 0 ; it < p_tp->nVert ; it+=3 )
                                    {
                                          int xmin = wxMin(ptp[it].x, wxMin(ptp[it+1].x, ptp[it+2].x));
                                          int xmax = wxMax(ptp[it].x, wxMax(ptp[it+1].x, ptp[it+2].x));
                                          int ymin = wxMin(ptp[it].y, wxMin(ptp[it+1].y, ptp[it+2].y));
                                          int ymax = wxMax(ptp[it].y, wxMax(ptp[it+1].y, ptp[it+2].y));

                                          wxRect rect(xmin, ymin, xmax-xmin, ymax-ymin);
                                          if(rect.Intersects(m_render_rect))
                                          {
                                                glBegin(GL_TRIANGLES);
                                                glVertex3f(ptp[it].x,   ptp[it].y,   z_clip_geom);
                                                glVertex3f(ptp[it+1].x, ptp[it+1].y, z_clip_geom);
                                                glVertex3f(ptp[it+2].x, ptp[it+2].y, z_clip_geom);
                                                glEnd();
                                          }
                                    }
                                    break;
                              }
                        }
                  }   // if bbox
                  p_tp = p_tp->p_next;                // pick up the next in chain
            }       // while

            if(!g_b_useStencil)
            {
                  glEndList();
                  glCallList(clip_list);
            }

            if(g_b_useStencil)
            {
            //    Now set the stencil ops to subsequently render only where the stencil bit is "2"
                  glStencilFunc (GL_EQUAL, 2, 2);
                  glStencilOp (GL_KEEP, GL_KEEP, GL_KEEP);
                  glColorMask(true, true, true, true);            // Re-enable the color buffer
            }
            else
            {
                  glDepthFunc(GL_EQUAL);                            // Set the test value
                  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);  // re-enable color buffer
                  glDepthMask(GL_FALSE);                            // disable depth buffer
            }

            //    Get the pattern definition
            if ( ( rules->razRule->pixelPtr == NULL ) || ( rules->razRule->parm1 != m_colortable_index ) )
            {
                  render_canvas_parms *patt_spec = CreatePatternBufferSpec(rzRules, rules, vp, 32, true);
                  rules->razRule->pixelPtr = patt_spec;
                  rules->razRule->parm1 = m_colortable_index;
                  rules->razRule->parm0 = ID_GL_PATT_SPEC;
            }


      //  Render the Area using the pattern spec stored in the rules
            render_canvas_parms *ppatt_spec = ( render_canvas_parms * ) rules->razRule->pixelPtr;

            //    Has the pattern been uploaded as a texture?
            if(!ppatt_spec->OGL_tex_name)
            {
                  GLuint tex_name;
                  glGenTextures(1, &tex_name);
                  ppatt_spec->OGL_tex_name = tex_name;

                  glBindTexture(GL_TEXTURE_2D, tex_name);

                  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

                  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ppatt_spec->w_pot, ppatt_spec->h_pot, 0, GL_RGBA, GL_UNSIGNED_BYTE, ppatt_spec->pix_buff);
            }

            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, ppatt_spec->OGL_tex_name);

            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

            int xr = obj_xmin;//0;
            int yr = obj_ymin;//0;
            int h = ppatt_spec->height;
            int w = ppatt_spec->width;

            float ww = (float)ppatt_spec->width/(float)ppatt_spec->w_pot;
            float hh = (float)ppatt_spec->height/(float)ppatt_spec->h_pot;
            while (yr < vp->pix_height)
            {
                  xr = obj_xmin;//0;
                  while(xr < vp->pix_width)
                  {
                        //    Render a quad.

                        if(((xr >= obj_xmin) && (xr <= obj_xmax)) && ((yr >= obj_ymin) && (yr <= obj_ymax)))
                        {
                              glBegin(GL_QUADS);
                              glTexCoord2f( 0,  0);            glVertex3f(xr,     yr, z_tex_geom);
                              glTexCoord2f( ww, 0);            glVertex3f(xr + w, yr, z_tex_geom);
                              glTexCoord2f( ww, hh);           glVertex3f(xr + w, yr + h, z_tex_geom);
                              glTexCoord2f( 0,  hh);           glVertex3f(xr,     yr + h, z_tex_geom);
                              glEnd();
                        }
                        xr += ppatt_spec->width;
                  }
                  yr += ppatt_spec->height;
            }

            glDisable(GL_TEXTURE_2D);
            glDisable(GL_BLEND);


            //    If using DepthBuffer clipping, we need to
            //    undo the sub-clip area for this feature render.
            //    Otherwise, subsequent AP renders with also honor this sub-clip region.

            //    We do this by rendering the geometry again with the depth(Z) value
            //    set to the global clipping value.
            //    For efficiency, we use the display list created above,
            //    translated appropriately in z direction

            //    Note that this is not required for stencil buffer clipping,
            //    since the relevent bit (2) is cleared on any subsequent AP renders.

            if(!g_b_useStencil)
            {

                  glEnable(GL_DEPTH_TEST); // to use the depth test
                  glDepthFunc(GL_LEQUAL); // Respect global render mask in depth buffer
                  glDepthMask(GL_TRUE);    // to allow writes to the depth buffer
                  glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);   // disable color buffer

                  glColor3f(1,1,0);

                  glTranslatef(0, 0, .25);      // Cause depth buffer rending at z = 0.5
                  glCallList(clip_list);        // Re-Render the clip geometry
                  glTranslatef(0, 0, -.25);     // undo translation (may not be required....)


                  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);  // re-enable color buffer
                  glDepthMask(GL_FALSE);                            // disable depth buffer

                  glDeleteLists(clip_list, 1);
            }

            //    Restore the previous state
            glPopAttrib();

      }

      free ( ptp );


#if 0
      //    At very small scales, the object could be visible on both the left and right sides of the screen.
      //    Identify this case......
      if(vp->chart_scale > 5e7)
      {
            //    Does the object hang out over the left side of the VP?
      if((rzRules->obj->BBObj.GetMaxX() > vp->GetBBox().GetMinX()) && (rzRules->obj->BBObj.GetMinX() < vp->GetBBox().GetMinX()))
      {
                  //    If we add 360 to the objects lons, does it intersect the the right side of the VP?
      if(((rzRules->obj->BBObj.GetMaxX() + 360.) > vp->GetBBox().GetMaxX()) && ((rzRules->obj->BBObj.GetMinX() + 360.) < vp->GetBBox().GetMaxX()))
      {
                        //  If so, this area oject should be drawn again, this time for the left side
                        //    Do this by temporarily adjusting the objects rendering offset
      rzRules->obj->x_origin -= mercator_k0 * WGS84_semimajor_axis_meters * 2.0 * PI;
      RenderToBufferFilledPolygon ( rzRules, rzRules->obj, c, vp->GetBBox(), pb_spec, NULL );
      rzRules->obj->x_origin += mercator_k0 * WGS84_semimajor_axis_meters * 2.0 * PI;

}
}
}
#endif

      return 1;
}


int s52plib::RenderAreaToGL(const wxGLContext &glcc, ObjRazRules *rzRules, ViewPort *vp, wxRect &render_rect)
{
//Debug Hooks
//      if(!strncmp(rzRules->LUP->OBCL, "PRCARE", 6))
//            int yyrjt = 4;

      if ( !ObjectRenderCheckPos ( rzRules, vp ) )
            return 0;

      if ( !ObjectRenderCheckCat ( rzRules, vp ) )
      {
            if(!ObjectRenderCheckCS( rzRules, vp ))
                  return 0;
      }

      m_render_rect = render_rect;        // We only render into this (screen coordinate) rectangle

      Rules *rules = rzRules->LUP->ruleList;

//Debug Hooks
// if(!strncmp(rzRules->LUP->OBCL, "FSHFAC", 6))
//              int yyrjt = 4;

 //     if ( rzRules->obj->Index == 2524 )
 //           int ggld = 4;

      while ( rules != NULL )
      {
            switch ( rules->ruleType )
            {
                  case RUL_ARE_CO:       RenderToGLAC ( rzRules,rules, vp );break;      // AC
                  case RUL_ARE_PA:       RenderToGLAP ( rzRules,rules, vp );break;      // AP

                  case RUL_CND_SY:
                  {
                        if ( !rzRules->obj->bCS_Added )
                        {
                              rzRules->obj->CSrules = NULL;
                              GetAndAddCSRules ( rzRules, rules );
                              rzRules->obj->bCS_Added = 1;                // mark the object
                        }
                        Rules *rules_last = rules;
                        rules = rzRules->obj->CSrules;

                        //    The CS procedure may have changed the Display Category of the Object, need to check again for visibility
                        if(ObjectRenderCheckCat ( rzRules, vp ))
                        {
                              while ( NULL != rules )
                              {
      //Hve seen drgare fault here, need to code area query to debug
      //possible that RENDERtoBUFFERAP/AC is blowing obj->CSRules
      //    When it faults here, look at new debug field obj->CSLUP
                                    switch ( rules->ruleType )
                                    {
                                          case RUL_ARE_CO:       RenderToGLAC ( rzRules,rules, vp );break;
                                          case RUL_ARE_PA:       RenderToGLAP ( rzRules,rules, vp );break;
                                          case RUL_NONE:
                                          default:
                                                break; // no rule type (init)
                                    }
                                    rules_last = rules;
                                    rules = rules->next;
                              }
                        }

                        rules = rules_last;
                        break;
                  }



                  case RUL_NONE:
                  default:
                        break; // no rule type (init)
            }                                     // switch

            rules = rules->next;
      }

      return 1;

}


render_canvas_parms* s52plib::CreatePatternBufferSpec(ObjRazRules *rzRules, Rules *rules, ViewPort *vp, int bpp, bool b_pot)
{
      wxImage Image;

      Rule *prule = rules->razRule;

      bool bstagger_pattern = (prule->fillType.PATP == 'S');

            //      Create a wxImage of the pattern drawn on an "unused_color" field
      if ( prule->definition.SYDF == 'R' )
            Image = RuleXBMToImage ( rules->razRule );


      else          // Vector
      {
            float fsf = 100 / canvas_pix_per_mm;

                  // Base bounding box
            wxBoundingBox box ( prule->pos.patt.bnbox_x.PBXC,  prule->pos.patt.bnbox_y.PBXR,
                                prule->pos.patt.bnbox_x.PBXC + prule->pos.patt.bnbox_w.PAHL,
                                prule->pos.patt.bnbox_y.PBXR +prule->pos.patt.bnbox_h.PAVL );

                  // Expand to include pivot
            box.Expand ( prule->pos.patt.pivot_x.PACL,  prule->pos.patt.pivot_y.PARW );

                  //    Pattern bounding boxes may be offset from origin, to preset the spacing
                  //    So, the bitmap must be delta based.
            double dwidth = box.GetMaxX() - box.GetMinX();
            double dheight = box.GetMaxY() - box.GetMinY();

                  //  Add in the pattern spacing parameters
            dwidth  += prule->pos.patt.minDist.PAMI;
            dheight += prule->pos.patt.minDist.PAMI;

                  //  Prescale
            dwidth  /= fsf;
            dheight /= fsf;

            int width = ( int ) dwidth + 1;
            int height = ( int ) dheight + 1;


                  //      Instantiate the vector pattern to a wxBitmap
            wxMemoryDC mdc;
            wxBitmap *pbm = NULL;

            if ( ( 0 != width ) && ( 0 != height ) )
            {
                  pbm = new wxBitmap ( width, height );

                  mdc.SelectObject ( *pbm );
                  mdc.SetBackground ( wxBrush ( m_unused_wxColor ) );
                  mdc.Clear();


                        //    For pattern debugging
//                              mdc.SetPen(*wxGREEN_PEN);
//                              mdc.DrawRectangle(0, 0, width, height);
//                              mdc.SetPen(wxNullPen);

                  int pivot_x = prule->pos.patt.pivot_x.PACL;
                  int pivot_y = prule->pos.patt.pivot_y.PARW ;

                  char *str = prule->vector.LVCT;
                  char *col = prule->colRef.LCRF;
                  wxPoint pivot ( pivot_x, pivot_y );
                  wxPoint r0 ( ( int ) ( (pivot_x  - box.GetMinX())/fsf ) + 1, ( int ) (( pivot_y - box.GetMinY())/fsf ) + 1 );
                  RenderHPGLtoDC ( str, col, &mdc, r0, pivot, 0 );
            }
            else
            {
                  pbm = new wxBitmap ( 2, 2 );                // substitute small, blank pattern
                  mdc.SelectObject ( *pbm );
                  mdc.SetBackground ( wxBrush (m_unused_wxColor ) );
                  mdc.Clear();
            }

                  //    Build a wxImage from the wxBitmap
            Image = pbm->ConvertToImage();

            delete pbm;
            mdc.SelectObject ( wxNullBitmap );
      }

//  Convert the wxImage to a populated render_canvas_parms struct

      int sizey = Image.GetHeight();
      int sizex = Image.GetWidth();

      render_canvas_parms *patt_spec = new render_canvas_parms;
      patt_spec->OGL_tex_name = 0;

      if(b_pot)
      {
            int xp = sizex;
            int a = 0;
            while(xp)
            {
                  xp = xp >> 1;
                  a++;
            }
            patt_spec->w_pot = 1 << a;

            xp = sizey;
            a = 0;
            while(xp)
            {
                  xp = xp >> 1;
                  a++;
            }
            patt_spec->h_pot = 1 << a;

      }
      else
      {
            patt_spec->w_pot = sizex;
            patt_spec->h_pot = sizey;
      }

      patt_spec->depth = bpp;                              // set the depth

      patt_spec->pb_pitch = ( ( patt_spec->w_pot * patt_spec->depth / 8 ) );
      patt_spec->lclip = 0;
      patt_spec->rclip = patt_spec->w_pot - 1;
      patt_spec->pix_buff = ( unsigned char * ) malloc ( patt_spec->h_pot * patt_spec->pb_pitch );

            // Preset background
      memset ( patt_spec->pix_buff, 0,sizey * patt_spec->pb_pitch );
      patt_spec->width = sizex;
      patt_spec->height = sizey;
      patt_spec->x = 0;
      patt_spec->y = 0;
      patt_spec->b_stagger = bstagger_pattern;

      unsigned char *pd0 = patt_spec->pix_buff;
      unsigned char *pd;
      unsigned char *ps0 = Image.GetData();
      unsigned char *ps;

      if ( bpp == 24 )
      {
            for ( int iy = 0 ; iy < sizey ; iy++ )
            {
                  pd = pd0 + ( iy * patt_spec->pb_pitch );
                  ps = ps0 + ( iy * sizex * 3 );
                  for ( int ix = 0 ; ix<sizex ; ix++ )
                  {
#ifdef ocpnUSE_ocpnBitmap
                        unsigned char c1 = *ps++;
                        unsigned char c2 = *ps++;
                        unsigned char c3 = *ps++;

                        *pd++ = c3;
                        *pd++ = c2;
                        *pd++ = c1;
#else
                        *pd++ = *ps++;
                        *pd++ = *ps++;
                        *pd++ = *ps++;
#endif
                  }
            }
      }

      else if ( bpp == 32 )       // was pb_spec->depth
      {
            unsigned char mr =  m_unused_wxColor.Red();
            unsigned char mg =  m_unused_wxColor.Green();
            unsigned char mb =  m_unused_wxColor.Blue();

            for ( int iy = 0 ; iy < sizey ; iy++ )
            {
                  pd = pd0 + ( iy * patt_spec->pb_pitch );
                  ps = ps0 + ( iy * sizex * 3 );
                  for ( int ix = 0 ; ix < sizex ; ix++ )
                  {
                        if(ix < sizex)
                        {
                              unsigned char r = *ps++;
                              unsigned char g = *ps++;
                              unsigned char b = *ps++;

                              *pd++ = r;
                              *pd++ = g;
                              *pd++ = b;
                              *pd++ = ((r==mr)&&(g==mg)&&(b==mb) ? 0 : 255);
                        }
                  }
            }
      }

      return patt_spec;

}


int s52plib::RenderToBufferAP ( ObjRazRules *rzRules, Rules *rules, ViewPort *vp,
                                render_canvas_parms *pb_spec )
{
      wxImage Image;

      if ( ( rules->razRule->pixelPtr == NULL ) || ( rules->razRule->parm1 != m_colortable_index ) )
      {
            render_canvas_parms *patt_spec = CreatePatternBufferSpec(rzRules, rules, vp, BPP);
#if 0
            Rule *prule = rules->razRule;

            bool bstagger_pattern = (prule->fillType.PATP == 'S');

            //      Create a wxImage of the pattern drawn on an "unused_color" field
            if ( prule->definition.SYDF == 'R' )
                  Image = RuleXBMToImage ( rules->razRule );


            else          // Vector
            {
                  float fsf = 100 / canvas_pix_per_mm;

                  // Base bounding box
                  wxBoundingBox box ( prule->pos.patt.bnbox_x.PBXC,  prule->pos.patt.bnbox_y.PBXR,
                                      prule->pos.patt.bnbox_x.PBXC + prule->pos.patt.bnbox_w.PAHL,
                                      prule->pos.patt.bnbox_y.PBXR +prule->pos.patt.bnbox_h.PAVL );

                  // Expand to include pivot
                  box.Expand ( prule->pos.patt.pivot_x.PACL,  prule->pos.patt.pivot_y.PARW );

                  //    Pattern bounding boxes may be offset from origin, to preset the spacing
                  //    So, the bitmap must be delta based.
                  double dwidth = box.GetMaxX() - box.GetMinX();
                  double dheight = box.GetMaxY() - box.GetMinY();

                  //  Add in the pattern spacing parameters
                  dwidth  += prule->pos.patt.minDist.PAMI;
                  dheight += prule->pos.patt.minDist.PAMI;

                  //  Prescale
                  dwidth  /= fsf;
                  dheight /= fsf;

                  int width = ( int ) dwidth + 1;
                  int height = ( int ) dheight + 1;


                  //      Instantiate the vector pattern to a wxBitmap
                  wxMemoryDC mdc;
                  wxBitmap *pbm = NULL;

                  if ( ( 0 != width ) && ( 0 != height ) )
                  {
                        pbm = new wxBitmap ( width, height );

                        mdc.SelectObject ( *pbm );
                        mdc.SetBackground ( wxBrush ( m_unused_wxColor ) );
                        mdc.Clear();

                        //    For pattern debugging
//                              mdc.SetPen(*wxGREEN_PEN);
//                              mdc.DrawRectangle(0, 0, width, height);
//                              mdc.SetPen(wxNullPen);

                        int pivot_x = prule->pos.patt.pivot_x.PACL;
                        int pivot_y = prule->pos.patt.pivot_y.PARW ;

                        char *str = prule->vector.LVCT;
                        char *col = prule->colRef.LCRF;
                        wxPoint pivot ( pivot_x, pivot_y );
                        wxPoint r0 ( ( int ) ( (pivot_x  - box.GetMinX())/fsf ) + 1, ( int ) (( pivot_y - box.GetMinY())/fsf ) + 1 );
                        RenderHPGLtoDC ( str, col, &mdc, r0, pivot, 0 );
                  }
                  else
                  {
                        pbm = new wxBitmap ( 2, 2 );                // substitute small, blank pattern
                        mdc.SelectObject ( *pbm );
                        mdc.SetBackground ( wxBrush (m_unused_wxColor ) );
                        mdc.Clear();
                  }

                  //    Build a wxImage from the wxBitmap
                  Image = pbm->ConvertToImage();

                  delete pbm;
                  mdc.SelectObject ( wxNullBitmap );

            }




//  Convert the initial wxImage in the rule to a useful PixelBuff

            int sizey = Image.GetHeight();
            int sizex = Image.GetWidth();

            render_canvas_parms *patt_spec = new render_canvas_parms;
            patt_spec->depth = BPP;                              // set the depth

            patt_spec->pb_pitch = ( ( sizex * patt_spec->depth / 8 ) );
            patt_spec->lclip = 0;
            patt_spec->rclip = sizex - 1;
            patt_spec->pix_buff = ( unsigned char * ) malloc ( sizey * patt_spec->pb_pitch );

            // Preset background
            memset ( patt_spec->pix_buff, 0,sizey * patt_spec->pb_pitch );
            patt_spec->width = sizex;
            patt_spec->height = sizey;
            patt_spec->x = 0;
            patt_spec->y = 0;
            patt_spec->b_stagger = bstagger_pattern;

            unsigned char *pd0 = patt_spec->pix_buff;
            unsigned char *pd;
            unsigned char *ps0 = Image.GetData();
            unsigned char *ps;

            if ( pb_spec->depth == 24 )
            {
                  for ( int iy = 0 ; iy < sizey ; iy++ )
                  {
                        pd = pd0 + ( iy * patt_spec->pb_pitch );
                        ps = ps0 + ( iy * sizex * 3 );
                        for ( int ix = 0 ; ix<sizex ; ix++ )
                        {
#ifdef ocpnUSE_ocpnBitmap
                              unsigned char c1 = *ps++;
                              unsigned char c2 = *ps++;
                              unsigned char c3 = *ps++;

                              *pd++ = c3;
                              *pd++ = c2;
                              *pd++ = c1;
#else
                              *pd++ = *ps++;
                              *pd++ = *ps++;
                              *pd++ = *ps++;
#endif
                        }
                  }
            }

            else if ( pb_spec->depth == 32 )
            {
                  for ( int iy = 0 ; iy < sizey ; iy++ )
                  {
                        pd = pd0 + ( iy * patt_spec->pb_pitch );
                        ps = ps0 + ( iy * sizex * 3 );
                        for ( int ix = 0 ; ix<sizex ; ix++ )
                        {
                              *pd++ = *ps++;
                              *pd++ = *ps++;
                              *pd++ = *ps++;
                              pd++;
                        }
                  }
            }
#endif
            rules->razRule->pixelPtr = patt_spec;
            rules->razRule->parm1 = m_colortable_index;
            rules->razRule->parm0 = ID_RGB_PATT_SPEC;

      }         // Instantiation done


      //  Render the Area using the pattern spec stored in the rules
      render_canvas_parms *ppatt_spec = ( render_canvas_parms * ) rules->razRule->pixelPtr;

      //  Set the pattern reference point

      wxPoint r;
      rzRules->chart->GetPointPix ( rzRules, rzRules->obj->y, rzRules->obj->x, &r );

      ppatt_spec->x = r.x - 2000000;                  // bias way down to avoid zero-crossing logic in dda
      ppatt_spec->y = r.y - 2000000;

      RenderToBufferFilledPolygon ( rzRules, rzRules->obj, NULL, vp->GetBBox(), pb_spec, ppatt_spec );

      return 1;
}


int s52plib::RenderToBufferAC ( ObjRazRules *rzRules, Rules *rules, ViewPort *vp,
                                render_canvas_parms *pb_spec )
{
      S52color *c;
      char *str = ( char* ) rules->INSTstr;

      c = ps52plib->S52_getColor ( str );

      RenderToBufferFilledPolygon ( rzRules, rzRules->obj, c, vp->GetBBox(), pb_spec, NULL );


      //    At very small scales, the object could be visible on both the left and right sides of the screen.
      //    Identify this case......
      if(vp->chart_scale > 5e7)
      {
            //    Does the object hang out over the left side of the VP?
            if((rzRules->obj->BBObj.GetMaxX() > vp->GetBBox().GetMinX()) && (rzRules->obj->BBObj.GetMinX() < vp->GetBBox().GetMinX()))
            {
                  //    If we add 360 to the objects lons, does it intersect the the right side of the VP?
                  if(((rzRules->obj->BBObj.GetMaxX() + 360.) > vp->GetBBox().GetMaxX()) && ((rzRules->obj->BBObj.GetMinX() + 360.) < vp->GetBBox().GetMaxX()))
                  {
                        //  If so, this area oject should be drawn again, this time for the left side
                        //    Do this by temporarily adjusting the objects rendering offset
                        rzRules->obj->x_origin -= mercator_k0 * WGS84_semimajor_axis_meters * 2.0 * PI;
                        RenderToBufferFilledPolygon ( rzRules, rzRules->obj, c, vp->GetBBox(), pb_spec, NULL );
                        rzRules->obj->x_origin += mercator_k0 * WGS84_semimajor_axis_meters * 2.0 * PI;

                  }
            }
      }

      return 1;
}

int s52plib::RenderAreaToDC ( wxDC *pdcin, ObjRazRules *rzRules, ViewPort *vp,
                          render_canvas_parms *pb_spec )
{
//Debug Hooks
//      if(!strncmp(rzRules->LUP->OBCL, "$AREAS", 6))
//            int yyrjt = 4;

      if ( !ObjectRenderCheckPos ( rzRules, vp ) )
            return 0;

      if ( !ObjectRenderCheckCat ( rzRules, vp ) )
      {
            if(!ObjectRenderCheckCS( rzRules, vp ))
                  return 0;
      }

      m_pdc = pdcin;                    // use this DC
      Rules *rules = rzRules->LUP->ruleList;

//Debug Hooks
// if(!strncmp(rzRules->LUP->OBCL, "FSHFAC", 6))
//              int yyrjt = 4;

 //     if ( rzRules->obj->Index == 2524 )
 //           int ggld = 4;

      while ( rules != NULL )
      {
            switch ( rules->ruleType )
            {
                  case RUL_ARE_CO:       RenderToBufferAC ( rzRules,rules, vp, pb_spec );break;      // AC
                  case RUL_ARE_PA:       RenderToBufferAP ( rzRules,rules, vp, pb_spec );break;      // AP

                  case RUL_CND_SY:
                  {
                        if ( !rzRules->obj->bCS_Added )
                        {
                              rzRules->obj->CSrules = NULL;
                              GetAndAddCSRules ( rzRules, rules );
                              rzRules->obj->bCS_Added = 1;                // mark the object
                        }
                        Rules *rules_last = rules;
                        rules = rzRules->obj->CSrules;

                        //    The CS procedure may have changed the Display Category of the Object, need to check again for visibility
                        if(ObjectRenderCheckCat ( rzRules, vp ))
                        {
                              while ( NULL != rules )
                              {
      //Hve seen drgare fault here, need to code area query to debug
      //possible that RENDERtoBUFFERAP/AC is blowing obj->CSRules
      //    When it faults here, look at new debug field obj->CSLUP
                                    switch ( rules->ruleType )
                                    {
                                          case RUL_ARE_CO:       RenderToBufferAC ( rzRules,rules, vp, pb_spec );break;
                                          case RUL_ARE_PA:       RenderToBufferAP ( rzRules,rules, vp, pb_spec );break;
                                          case RUL_NONE:
                                          default:
                                                break; // no rule type (init)
                                    }
                                    rules_last = rules;
                                    rules = rules->next;
                              }
                        }

                        rules = rules_last;
                        break;
                  }



                  case RUL_NONE:
                  default:
                        break; // no rule type (init)
            }                                     // switch

            rules = rules->next;
      }

      return 1;

}

void s52plib::GetAndAddCSRules ( ObjRazRules *rzRules, Rules *rules )
{

      LUPrec  *NewLUP;
      LUPrec  *LUP;
      LUPrec  *LUPCandidate;

      char *rule_str1 = RenderCS ( rzRules, rules );
      wxString cs_string ( rule_str1, wxConvUTF8 );
      free ( rule_str1 );  //delete rule_str1;


//  Try to find a match for this object/attribute set in dynamic CS LUP Table

//  Do this by checking each LUP in the CS LUPARRAY and checking....
//  a) is Object Name the same? and
//  b) was LUP created earlier by exactly the same INSTruction string?
//  c) does LUP have same Display Category and Priority?

      wxArrayOfLUPrec *la = condSymbolLUPArray;
      int index = 0;
      int index_max = la->GetCount();
      LUP = NULL;

      while ( ( index < index_max ) )
      {
            LUPCandidate = la->Item ( index );
            if ( !strcmp ( rzRules->LUP->OBCL, LUPCandidate->OBCL ) )
            {
                  if ( LUPCandidate->INST->IsSameAs ( cs_string ) )
                  {
                        if( LUPCandidate->DISC == rzRules->LUP->DISC)
                        {
                              LUP = LUPCandidate;
                              break;
                        }
                  }
            }
            index++;
      }






//  If not found, need to create a dynamic LUP and add to CS LUP Table

      if ( NULL == LUP )                              // Not found
      {

            NewLUP = ( LUPrec* ) calloc ( 1, sizeof ( LUPrec ) );
            pAlloc->Add ( NewLUP );

            NewLUP->DISC = rzRules->LUP->DISC;         // as a default

            //sscanf(pBuf+11, "%d", &LUP->RCID);

            strncpy ( NewLUP->OBCL, rzRules->LUP->OBCL, 6 );  // the object class name
//  if(!strncmp(LUP->OBCL, "LNDARE", 6))
//         int qewr = 9;

//                        NewLUP->FTYP = (enum _Object_t)pBuf[25];
//                        NewLUP->DPRI = (enum _DisPrio)pBuf[30];
//                        NewLUP->RPRI = (enum _RadPrio)pBuf[31];
//                        NewLUP->TNAM = (enum _LUPname)pBuf[36];


// Parse the instant object's attribute name string and attribute values to the LUP
// Attribute values are neede to ensure exact match


            /*
                                    wxString *pobj_attList = rzRules->obj->attList;
                                    if ('\037' != pobj_attList[0])                                // could be empty!
                                    {

                                        wxString *LUPATTC = new wxString;

                                        wxArrayString *pAS = new wxArrayString();
                                        char *p = (char *)pobj_attList->mb_str();

                                        wxString *st1 = new wxString;
                                        int attIdx = 0;

                                        while(*p)
                                        {
                                            while(*p != 0x1f)
                                            {
                                              st1->Append(*p);
                                              p++;
                                            }

                                            S57attVal *v;
                                            v = rzRules->obj->attVal->Item(attIdx);
                                            wxString apf = AttValPrintf(v);
                                            st1->Append(apf);

                                            LUPATTC->Append(*st1);
                                            LUPATTC->Append('\037');

                                            pAS->Add(*st1);
                                            st1->Clear();
                                            p++;
                                            attIdx++;
                                        }

                                        delete st1;

                                        NewLUP->ATTCArray = pAS;
                                        NewLUP->ATTC = LUPATTC;
                                    }


            */

//      Add the complete CS string to the LUP

            wxString *pINST = new wxString ( cs_string );
            NewLUP->INST = pINST;

            _LUP2rules ( NewLUP, rzRules->obj );

// Add LUP to array
            wxArrayOfLUPrec *pLUPARRAYtyped = condSymbolLUPArray;

            pLUPARRAYtyped->Add ( NewLUP );


            LUP = NewLUP;

      }       // if (LUP = NULL)


      Rules *top = LUP->ruleList;

      rzRules->obj->CSrules = top;                // patch in a new rule set

}

bool s52plib::ObjectRenderCheck ( ObjRazRules *rzRules, ViewPort *vp )
{
//      if ( !strncmp ( rzRules->LUP->OBCL, "BOYLAT", 6 ) )
//      {
//            if ( ObjectRenderCheckPos ( rzRules, vp ) )
//               int yyp = 5;
//      }

      if ( !ObjectRenderCheckPos ( rzRules, vp ) )
            return false;

      if ( !ObjectRenderCheckCat ( rzRules, vp ) )
            return false;

      return true;
}


bool s52plib::ObjectRenderCheckCS ( ObjRazRules *rzRules, ViewPort *vp )
{
//  We need to do this test since some CS procedures change the display category
//  So we need to tentatively process all objects with CS LUPs
      Rules *rules = rzRules->LUP->ruleList;
      while ( rules != NULL )
      {
            if(RUL_CND_SY == rules->ruleType )
                  return true;

            rules = rules->next;
      }

      return false;
}


bool s52plib::ObjectRenderCheckPos ( ObjRazRules *rzRules, ViewPort *vp )
{
      if ( rzRules->obj==NULL )
            return false;

      // Debug for testing US5FL51.000 slcons
//    if((rzRules->obj->Index == 3868) || (rzRules->obj->Index == 3870))
//        return false;

//    if(rzRules->obj->Index != 3)
//        return false;

      // Of course, the object must be at least partly visible in the viewport
      wxBoundingBox BBView = vp->GetBBox();
      if ( BBView.Intersect ( rzRules->obj->BBObj, 0 ) == _OUT ) // Object is wholly outside window
      {

            //  Do a secondary test if the viewport crosses Greenwich
            //  This will pick up objects east of Greenwich
            if ( vp->GetBBox().GetMaxX() > 360. )
            {
                  wxBoundingBox bbRight ( 0., vp->GetBBox().GetMinY(), vp->GetBBox().GetMaxX() - 360., vp->GetBBox().GetMaxY() );
                  if ( bbRight.Intersect ( rzRules->obj->BBObj, 0 ) == _OUT )
                        return false;
            }

            else
                  return false;
      }

      return true;
/*
      bool b_catfilter = ObjectRenderCheckCat ( rzRules );

//  Soundings override
      if ( !strncmp ( rzRules->LUP->OBCL, "SOUNDG", 6 ) )
            b_catfilter = m_bShowSoundg;


      bool b_visible = false;
      if ( b_catfilter )
      {
            b_visible = true;

//      SCAMIN Filtering
            //      Implementation note:
            //      According to S52 specs, SCAMIN must not apply to GROUP1 objects, Meta Objects
            //      or DisplayCategoryBase objects.
            //      Occasionally, an ENC will encode a spurious SCAMIN value for one of these objects.
            //      see, for example, US5VA18M, in OpenCPN SENC as Feature 350(DEPARE), LNAM = 022608187ED20ACC.
            //      We shall explicitly ignore SCAMIN filtering for these types of objects.

            if ( m_bUseSCAMIN )
            {
                  if ( ( DISPLAYBASE == rzRules->LUP->DISC ) || ( PRIO_GROUP1 == rzRules->LUP->DPRI ) )
                        b_visible = true;
                  else if ( vp->chart_scale > rzRules->obj->Scamin )
                        b_visible = false;

                  //      On the other hand, $TEXTS features need not really be displayed at all scales, always
                  //      To do so makes a very cluttered display
                  if ( ( !strncmp ( rzRules->LUP->OBCL, "$TEXTS", 6 ) ) && ( vp->chart_scale > rzRules->obj->Scamin ) )
                        b_visible = false;
            }

            return b_visible;
      }

      return false;
*/
}


bool s52plib::ObjectRenderCheckCat ( ObjRazRules *rzRules, ViewPort *vp )
{
      if ( rzRules->obj==NULL )
            return false;

      bool b_catfilter = true;

      //  Meta object override
      if ( !strncmp ( rzRules->LUP->OBCL, "M_", 2 ) )
            if ( !m_bShowMeta )
                  return false;


      //      Do Object Type Filtering
      DisCat obj_cat = rzRules->obj->m_DisplayCat;

      if ( m_nDisplayCategory == MARINERS_STANDARD )
      {
            if(-1 == rzRules->obj->iOBJL)
                  UpdateOBJLArray(rzRules->obj);

            if ( ! ( ( OBJLElement * ) ( pOBJLArray->Item ( rzRules->obj->iOBJL ) ) )->nViz )
                  b_catfilter = false;
      }

      else if ( m_nDisplayCategory == OTHER )
      {
            if ( ( DISPLAYBASE != obj_cat )
                   && ( STANDARD != obj_cat )
                   && ( OTHER != obj_cat ) )
            {
                  b_catfilter = false;
            }
      }

      else if ( m_nDisplayCategory == STANDARD )
      {
            if ( ( DISPLAYBASE != obj_cat ) && ( STANDARD != obj_cat ) )
            {
                  b_catfilter = false;
            }
      }
      else if ( m_nDisplayCategory == DISPLAYBASE )
      {
            if ( DISPLAYBASE != obj_cat )
            {
                  b_catfilter = false;
            }
      }

//  Soundings override
      if ( !strncmp ( rzRules->LUP->OBCL, "SOUNDG", 6 ) )
            b_catfilter = m_bShowSoundg;


      bool b_visible = false;
      if ( b_catfilter )
      {
            b_visible = true;

//      SCAMIN Filtering
            //      Implementation note:
            //      According to S52 specs, SCAMIN must not apply to GROUP1 objects, Meta Objects
            //      or DisplayCategoryBase objects.
            //      Occasionally, an ENC will encode a spurious SCAMIN value for one of these objects.
            //      see, for example, US5VA18M, in OpenCPN SENC as Feature 350(DEPARE), LNAM = 022608187ED20ACC.
            //      We shall explicitly ignore SCAMIN filtering for these types of objects.

            if ( m_bUseSCAMIN )
            {
                  if ( ( DISPLAYBASE == rzRules->LUP->DISC ) || ( PRIO_GROUP1 == rzRules->LUP->DPRI ) )
                        b_visible = true;
                  else if ( vp->chart_scale > rzRules->obj->Scamin )
                        b_visible = false;

                  //      On the other hand, $TEXTS features need not really be displayed at all scales, always
                  //      To do so makes a very cluttered display
                  if ( ( !strncmp ( rzRules->LUP->OBCL, "$TEXTS", 6 ) ) && ( vp->chart_scale > rzRules->obj->Scamin ) )
                        b_visible = false;
            }

            return b_visible;
      }

      return b_visible;
}



//    Do all those things necessary to prepare for a new rendering
void s52plib::PrepareForRender()
{

}

void s52plib::ClearTextList ( void )
{
      //      Clear the current text rectangle list
      m_textObjList.Clear();

}

void s52plib::AdjustTextList ( int dx, int dy, int screenw, int screenh )
{
      wxRect rScreen ( 0, 0, screenw, screenh );
      //    Iterate over the text rectangle list
      //        1.  Apply the specified offset to the list elements
      //        2.. Remove any list elements that are off screen after applied offset

      ObjList::Node *node = m_textObjList.GetFirst();
      while(node)
      {
            wxRect *pcurrent = & ( node->GetData()->rText );
            pcurrent->Offset ( dx, dy );

            if ( !pcurrent->Intersects ( rScreen ) )
            {
                  m_textObjList.DeleteNode ( node );

                  node = m_textObjList.GetFirst();
            }
            else
                  node = node->GetNext();
      }
}





/*----------------------------------------------------------------------------------*/
/*    Draw anti-aliased dash or solid line 1 pixel wide                             */
/*         using Wu algorithm                                                       */
/*                                                                                  */
/*    This is a slow, DC pixel based implementation, and is quite slow.             */
/*    Recommend using sparingly..........                                           */
/*                                                                                  */
/*    Thanks to Suchit.Tiwari@Ge.com                                                */
/*----------------------------------------------------------------------------------*/

void DrawWuLine ( wxDC *pDC, int X0, int Y0, int X1, int Y1, wxColour clrLine, int dash, int space )
{
      bool bdraw = true;

      //    calculate the length of the line
      double len = sqrt ( pow ( (double)( X0-X1 ), 2 ) + pow ( (double)( Y0-Y1 ), 2 ) );

      int dot_cnt = dash;
      if ( space == 0 )
            dot_cnt = -1;                 // No spaces, dots run (almost) forever

      /* Make sure the line runs top to bottom */
      if ( Y0 > Y1 )
      {
            int Temp = Y0; Y0 = Y1; Y1 = Temp;
            Temp = X0; X0 = X1; X1 = Temp;
      }

      /* Draw the initial pixel, which is always exactly intersected by
        the line and so needs no weighting */

      pDC->SetPen ( wxPen ( clrLine ) );
      pDC->DrawPoint ( X0, Y0 );

      int XDir;
      int DeltaX = X1 - X0;
      if ( DeltaX >= 0 )
      {
            XDir = 1;
      }
      else
      {
            XDir   = -1;
            DeltaX = 0 - DeltaX; /* make DeltaX positive */
      }

      /* Special-case horizontal, vertical, and diagonal lines, which
        require no weighting because they go right through the center of
        every pixel */
      int DeltaY = Y1 - Y0;
      if ( DeltaY == 0 )
      {
            /* Horizontal line */
            while ( DeltaX-- != 0 )
            {
                  X0 += XDir;
                  if ( bdraw )
                  {
                        pDC->SetPen ( wxPen ( clrLine ) );
                        pDC->DrawPoint ( X0, Y0 );
                  }

                  if ( dot_cnt-- == 0 )
                  {
                        dot_cnt = bdraw?space:dash;
                        bdraw = !bdraw;
                  }
            }
            return;
      }
      if ( DeltaX == 0 )
      {
            /* Vertical line */
            do
            {
                  Y0++;
                  if ( bdraw )
                  {
                        pDC->SetPen ( wxPen ( clrLine ) );
                        pDC->DrawPoint ( X0, Y0 );
                  }

                  if ( dot_cnt-- == 0 )
                  {
                        dot_cnt = bdraw?space:dash;
                        bdraw = !bdraw;
                  }

            }
            while ( --DeltaY != 0 );
            return;
      }

      if ( DeltaX == DeltaY )
      {
            /* Diagonal line */
            do
            {
                  X0 += XDir;
                  Y0++;
                  if ( bdraw )
                  {
                        pDC->SetPen ( wxPen ( clrLine ) );
                        pDC->DrawPoint ( X0, Y0 );
                  }

                  if ( dot_cnt-- == 0 )
                  {
                        dot_cnt = bdraw?space:dash;
                        bdraw = !bdraw;
                  }

            }
            while ( --DeltaY != 0 );
            return;
      }

      /* Line is not horizontal, diagonal, or vertical */

      //  Extract a bitmap from the dc
      wxMemoryDC mdc;

      int width = 1 + abs ( X0 - X1 );
      int height = 1 + abs ( Y0 - Y1 );

      wxBitmap bm ( width, height );
      mdc.SelectObject ( bm );

      mdc.Blit ( 0, 0, width, height, pDC, wxMin ( X0, X1 ), Y0 );

      // convert bitmap to image
      wxImage img = bm.ConvertToImage();

      mdc.SelectObject ( wxNullBitmap );

      //  Adjust coordinates
      int xp0, yp0;

      xp0 = X0 - wxMin ( X0, X1 );
      yp0 = Y0 - wxMin ( Y0, Y1 );

      wxColour clrBackGround;

      unsigned short ErrorAdj;
      unsigned short ErrorAccTemp, Weighting;

      unsigned short ErrorAcc = 0;  /* initialize the line error accumulator to 0 */

      unsigned char rl = clrLine.Red();
      unsigned char gl = clrLine.Green();
      unsigned char bl = clrLine.Blue();
      double grayl = rl * 0.299 + gl * 0.587 + bl * 0.114;

      /* Is this an X-major or Y-major line? */
      if ( DeltaY > DeltaX )
      {
            /* Y-major line; calculate 16-bit fixed-point fractional part of a
                    pixel that X advances each time Y advances 1 pixel, truncating the
                    result so that we won't overrun the endpoint along the X axis */
            ErrorAdj = ( ( unsigned long ) DeltaX << 16 ) / ( unsigned long ) DeltaY;

            space = ( int ) ( space * fabs ( DeltaY / len ) );
            dash = ( int ) ( dash * fabs ( DeltaY / len ) );
            dot_cnt = dash;
            if ( space == 0 )
                  dot_cnt = -1;                 // No spaces, dots run (almost) forever

            /* Draw all pixels other than the first and last */
            while ( --DeltaY )
            {
                  ErrorAccTemp = ErrorAcc;   /* remember currrent accumulated error */
                  ErrorAcc = (ErrorAcc + ErrorAdj) & 0xffff;      /* calculate error for next pixel */
                  if ( ErrorAcc <= ErrorAccTemp )
                  {
                        /* The error accumulator turned over, so advance the X coord */
                        xp0 += XDir;
                  }
                  yp0++; /* Y-major, so always advance Y */
                  /* The IntensityBits most significant bits of ErrorAcc give us the
                  intensity weighting for this pixel, and the complement of the
                  weighting for the paired pixel */
                  Weighting = ErrorAcc >> 8;


                  unsigned char rb =  img.GetRed ( xp0, yp0 );
                  unsigned char gb =  img.GetGreen ( xp0, yp0 );
                  unsigned char bb =  img.GetBlue ( xp0, yp0 );

                  double grayb = rb * 0.299 + gb * 0.587 + bb * 0.114;

                  unsigned char rr = ( rb > rl ? ( ( unsigned char ) ( ( ( double ) ( grayl<grayb?Weighting:
                                                   ( Weighting ^ 255 ) ) ) / 255.0 * ( rb - rl ) + rl ) ) :
                                                   ( ( unsigned char ) ( ( ( double ) ( grayl<grayb?Weighting: ( Weighting ^ 255 ) ) )
                                                                         / 255.0 * ( rl - rb ) + rb ) ) );
                  unsigned char gr = ( gb > gl ? ( ( unsigned char ) ( ( ( double ) ( grayl<grayb?Weighting:
                                                   ( Weighting ^ 255 ) ) ) / 255.0 * ( gb - gl ) + gl ) ) :
                                                   ( ( unsigned char ) ( ( ( double ) ( grayl<grayb?Weighting: ( Weighting ^ 255 ) ) )
                                                                         / 255.0 * ( gl - gb ) + gb ) ) );
                  unsigned char br = ( bb > bl ? ( ( unsigned char ) ( ( ( double ) ( grayl<grayb?Weighting:
                                                   ( Weighting ^ 255 ) ) ) / 255.0 * ( bb - bl ) + bl ) ) :
                                                   ( ( unsigned char ) ( ( ( double ) ( grayl<grayb?Weighting: ( Weighting ^ 255 ) ) )
                                                                         / 255.0 * ( bl - bb ) + bb ) ) );

                  if ( bdraw )
                        img.SetRGB ( xp0, yp0, rr, gr, br );


                  rb =  img.GetRed ( xp0 + XDir, yp0 );
                  gb =  img.GetGreen ( xp0 + XDir, yp0 );
                  bb =  img.GetBlue ( xp0 + XDir, yp0 );

                  grayb = rb * 0.299 + gb * 0.587 + bb * 0.114;

                  rr = ( rb > rl ? ( ( unsigned char ) ( ( ( double ) ( grayl<grayb? ( Weighting ^ 255 ) :
                                                         Weighting ) ) / 255.0 * ( rb - rl ) + rl ) ) :
                                     ( ( unsigned char ) ( ( ( double ) ( grayl<grayb? ( Weighting ^ 255 ) :Weighting ) )
                                                           / 255.0 * ( rl - rb ) + rb ) ) );
                  gr = ( gb > gl ? ( ( unsigned char ) ( ( ( double ) ( grayl<grayb? ( Weighting ^ 255 ) :
                                                         Weighting ) ) / 255.0 * ( gb - gl ) + gl ) ) :
                                     ( ( unsigned char ) ( ( ( double ) ( grayl<grayb? ( Weighting ^ 255 ) :Weighting ) )
                                                           / 255.0 * ( gl - gb ) + gb ) ) );
                  br = ( bb > bl ? ( ( unsigned char ) ( ( ( double ) ( grayl<grayb? ( Weighting ^ 255 ) :
                                                         Weighting ) ) / 255.0 * ( bb - bl ) + bl ) ) :
                                     ( ( unsigned char ) ( ( ( double ) ( grayl<grayb? ( Weighting ^ 255 ) :Weighting ) )
                                                           / 255.0 * ( bl - bb ) + bb ) ) );

                  if ( bdraw )
                        img.SetRGB ( xp0 + XDir, yp0, rr, gr, br );

                  if ( dot_cnt-- == 0 )
      {
                        dot_cnt = bdraw?space:dash;
                        bdraw = !bdraw;
                  }
            }

//                convert from image to bitmap, then blit it back into pDC
            wxBitmap fbm ( img, -1 );
            mdc.SelectObject ( fbm );

            pDC->Blit ( wxMin ( X0, X1 ), Y0, width, height, &mdc, 0, 0 );

            mdc.SelectObject ( wxNullBitmap );

            return;
      }


      /* It's an X-major line; calculate 16-bit fixed-point fractional part of a
        pixel that Y advances each time X advances 1 pixel, truncating the
        result to avoid overrunning the endpoint along the X axis */
      ErrorAdj = ( ( unsigned long ) DeltaY << 16 ) / ( unsigned long ) DeltaX;

      space = ( int ) ( space * fabs ( DeltaX / len ) );
      dash = ( int ) ( dash * fabs ( DeltaX / len ) );
      dot_cnt = dash;
      if ( space == 0 )
            dot_cnt = -1;                 // No spaces, dots run (almost) forever

      /* Draw all pixels other than the first and last */
      while ( --DeltaX )
      {
            ErrorAccTemp = ErrorAcc;   /* remember currrent accumulated error */
            ErrorAcc += ErrorAdj;      /* calculate error for next pixel */
            if ( ErrorAcc <= ErrorAccTemp )
            {
                  /* The error accumulator turned over, so advance the Y coord */
                  yp0++;
            }
            xp0 += XDir; /* X-major, so always advance X */
            /* The IntensityBits most significant bits of ErrorAcc give us the
            intensity weighting for this pixel, and the complement of the
            weighting for the paired pixel */
            Weighting = ErrorAcc >> 8;

            unsigned char rb =  img.GetRed ( xp0, yp0 );
            unsigned char gb =  img.GetGreen ( xp0, yp0 );
            unsigned char bb =  img.GetBlue ( xp0, yp0 );

            double grayb = rb * 0.299 + gb * 0.587 + bb * 0.114;

            unsigned char rr = ( rb > rl ? ( ( unsigned char ) ( ( ( double ) ( grayl<grayb?Weighting:
                                             ( Weighting ^ 255 ) ) ) / 255.0 * ( rb - rl ) + rl ) ) :
                                             ( ( unsigned char ) ( ( ( double ) ( grayl<grayb?Weighting: ( Weighting ^ 255 ) ) )
                                                                   / 255.0 * ( rl - rb ) + rb ) ) );
            unsigned char gr = ( gb > gl ? ( ( unsigned char ) ( ( ( double ) ( grayl<grayb?Weighting:
                                             ( Weighting ^ 255 ) ) ) / 255.0 * ( gb - gl ) + gl ) ) :
                                             ( ( unsigned char ) ( ( ( double ) ( grayl<grayb?Weighting: ( Weighting ^ 255 ) ) )
                                                                   / 255.0 * ( gl - gb ) + gb ) ) );
            unsigned char br = ( bb > bl ? ( ( unsigned char ) ( ( ( double ) ( grayl<grayb?Weighting:
                                             ( Weighting ^ 255 ) ) ) / 255.0 * ( bb - bl ) + bl ) ) :
                                             ( ( unsigned char ) ( ( ( double ) ( grayl<grayb?Weighting: ( Weighting ^ 255 ) ) )
                                                                   / 255.0 * ( bl - bb ) + bb ) ) );

            if ( bdraw )
                  img.SetRGB ( xp0, yp0, rr, gr, br );

            rb =  img.GetRed ( xp0, yp0 + 1 );
            gb =  img.GetGreen ( xp0, yp0 + 1 );
            bb =  img.GetBlue ( xp0, yp0 + 1 );

            grayb = rb * 0.299 + gb * 0.587 + bb * 0.114;

            rr = ( rb > rl ? ( ( unsigned char ) ( ( ( double ) ( grayl<grayb? ( Weighting ^ 255 ) :
                                                   Weighting ) ) / 255.0 * ( rb - rl ) + rl ) ) :
                               ( ( unsigned char ) ( ( ( double ) ( grayl<grayb? ( Weighting ^ 255 ) :Weighting ) )
                                                     / 255.0 * ( rl - rb ) + rb ) ) );
            gr = ( gb > gl ? ( ( unsigned char ) ( ( ( double ) ( grayl<grayb? ( Weighting ^ 255 ) :
                                                   Weighting ) ) / 255.0 * ( gb - gl ) + gl ) ) :
                               ( ( unsigned char ) ( ( ( double ) ( grayl<grayb? ( Weighting ^ 255 ) :Weighting ) )
                                                     / 255.0 * ( gl - gb ) + gb ) ) );
            br = ( bb > bl ? ( ( unsigned char ) ( ( ( double ) ( grayl<grayb? ( Weighting ^ 255 ) :
                                                   Weighting ) ) / 255.0 * ( bb - bl ) + bl ) ) :
                               ( ( unsigned char ) ( ( ( double ) ( grayl<grayb? ( Weighting ^ 255 ) :Weighting ) )
                                                     / 255.0 * ( bl - bb ) + bb ) ) );

            if ( bdraw )
                  img.SetRGB ( xp0, yp0 + 1, rr, gr, br );


            if ( dot_cnt-- == 0 )
{
                  dot_cnt = bdraw?space:dash;
                  bdraw = !bdraw;
            }

      }


//                convert from image to bitmap, then blit it back into pDC
      wxBitmap fbm ( img, -1 );
      mdc.SelectObject ( fbm );

      pDC->Blit ( wxMin ( X0, X1 ), Y0, width, height, &mdc, 0, 0 );

      mdc.SelectObject ( wxNullBitmap );

      /* Draw the final pixel, which is always exactly intersected by the line
        and so needs no weighting */
//        pDC->SetPen ( wxPen ( clrLine ) );
//        pDC->DrawPoint ( X1, Y1 );
}

/* OpenGL Text Rendering Support   */

/* Copyright (c) Mark J. Kilgard, 1997. */

/* This program is freely distributable without licensing fees  and is
   provided without guarantee or warrantee expressed or  implied. This
   program is -not- in the public domain. */

/*  Heavily edited for OpenCPN by David S. Register    */

//#include <assert.h>
//#include <ctype.h>
// #include <stdlib.h>
// #include <stdio.h>
// #include <string.h>
// #include <GL/glu.h>
// #include "TexFont.h"


//#define USE_DISPLAY_LISTS
/* Intensity texture format not in OpenGL 1.0; added by the EXT_texture
         extension and now part of OpenGL 1.1. */
         int useLuminanceAlpha = 1;

         /* byte swap a 32-bit value */
#define SWAPL(x, n) { \
         n = ((char *) (x))[0];\
         ((char *) (x))[0] = ((char *) (x))[3];\
         ((char *) (x))[3] = n;\
         n = ((char *) (x))[1];\
         ((char *) (x))[1] = ((char *) (x))[2];\
         ((char *) (x))[2] = n; }

         /* byte swap a short */
#define SWAPS(x, n) { \
         n = ((char *) (x))[0];\
         ((char *) (x))[0] = ((char *) (x))[1];\
         ((char *) (x))[1] = n; }

static TexGlyphVertexInfo *
                     getTCVI(TexFont * txf, int c)
{
      TexGlyphVertexInfo *tgvi;

  /* Automatically substitute uppercase letters with lowercase if not
      uppercase available (and vice versa). */
      if ((c >= txf->min_glyph) && (c < txf->min_glyph + txf->range)) {
            tgvi = txf->lut[c - txf->min_glyph];
            if (tgvi) {
                  return tgvi;
            }
            if (islower(c)) {
                  c = toupper(c);
                  if ((c >= txf->min_glyph) && (c < txf->min_glyph + txf->range)) {
                        return txf->lut[c - txf->min_glyph];
                  }
            }
            if (isupper(c)) {
                  c = tolower(c);
                  if ((c >= txf->min_glyph) && (c < txf->min_glyph + txf->range)) {
                        return txf->lut[c - txf->min_glyph];
                  }
            }
      }
      fprintf(stderr, "texfont: tried to access unavailable font character \"%c\" (%d)\n",
              isprint(c) ? c : ' ', c);
//      abort();
      return NULL;
      /* NOTREACHED */
}

static char *lastError;

char *txfErrorString(void)
{
      return lastError;
}

TexFont *txfLoadFont(char *filename)
{
      TexFont *txf;
      FILE *file;
      GLfloat w, h, xstep, ystep;
      char fileid[4], tmp;
      unsigned char *texbitmap;
      int min_glyph, max_glyph;
      int endianness, swap, format, stride, width, height;
      int i, j, got;

      txf = NULL;
      file = fopen(filename, "rb");
      if (file == NULL) {
            lastError = (char *)"file open failed.";
            goto error;
      }
      txf = (TexFont *) malloc(sizeof(TexFont));
      if (txf == NULL) {
            lastError = (char *)"out of memory.";
            goto error;
      }
      /* For easy cleanup in error case. */
      txf->tgi = NULL;
      txf->tgvi = NULL;
      txf->lut = NULL;
      txf->teximage = NULL;

      got = fread(fileid, 1, 4, file);
      if (got != 4 || strncmp(fileid, "\377txf", 4)) {
            lastError = (char *)"not a texture font file.";
            goto error;
      }
      assert(sizeof(int) == 4);  /* Ensure external file format size. */
      got = fread(&endianness, sizeof(int), 1, file);
      if (got == 1 && endianness == 0x12345678) {
            swap = 0;
      } else if (got == 1 && endianness == 0x78563412) {
            swap = 1;
      } else {
            lastError = (char *)"not a texture font file.";
            goto error;
      }
#define EXPECT(n) if (got != n) { lastError = (char *)"premature end of file."; goto error; }
      got = fread(&format, sizeof(int), 1, file);
      EXPECT(1);
      got = fread(&txf->tex_width, sizeof(int), 1, file);
      EXPECT(1);
      got = fread(&txf->tex_height, sizeof(int), 1, file);
      EXPECT(1);
      got = fread(&txf->max_ascent, sizeof(int), 1, file);
      EXPECT(1);
      got = fread(&txf->max_descent, sizeof(int), 1, file);
      EXPECT(1);
      got = fread(&txf->num_glyphs, sizeof(int), 1, file);
      EXPECT(1);

      if (swap) {
            SWAPL(&format, tmp);
            SWAPL(&txf->tex_width, tmp);
            SWAPL(&txf->tex_height, tmp);
            SWAPL(&txf->max_ascent, tmp);
            SWAPL(&txf->max_descent, tmp);
            SWAPL(&txf->num_glyphs, tmp);
      }
      txf->tgi = (TexGlyphInfo *) malloc(txf->num_glyphs * sizeof(TexGlyphInfo));
      if (txf->tgi == NULL) {
            lastError = (char *)"out of memory.";
            goto error;
      }
      assert(sizeof(TexGlyphInfo) == 12);  /* Ensure external file format size. */
      got = fread(txf->tgi, sizeof(TexGlyphInfo), txf->num_glyphs, file);
      EXPECT(txf->num_glyphs);

      if (swap) {
            for (i = 0; i < txf->num_glyphs; i++) {
                  SWAPS(&txf->tgi[i].c, tmp);
                  SWAPS(&txf->tgi[i].x, tmp);
                  SWAPS(&txf->tgi[i].y, tmp);
            }
      }
      txf->tgvi = (TexGlyphVertexInfo *)
                  malloc(txf->num_glyphs * sizeof(TexGlyphVertexInfo));
      if (txf->tgvi == NULL) {
            lastError = (char *)"out of memory.";
            goto error;
      }
      w = txf->tex_width;
      h = txf->tex_height;
      xstep = 0.5 / w;
      ystep = 0.5 / h;
      for (i = 0; i < txf->num_glyphs; i++) {
            TexGlyphInfo *tgi;

            tgi = &txf->tgi[i];
            txf->tgvi[i].t0[0] = tgi->x / w + xstep;
            txf->tgvi[i].t0[1] = tgi->y / h + ystep;
            txf->tgvi[i].v0[0] = tgi->xoffset;
            txf->tgvi[i].v0[1] = tgi->yoffset;
            txf->tgvi[i].t1[0] = (tgi->x + tgi->width) / w + xstep;
            txf->tgvi[i].t1[1] = tgi->y / h + ystep;
            txf->tgvi[i].v1[0] = tgi->xoffset + tgi->width;
            txf->tgvi[i].v1[1] = tgi->yoffset;
            txf->tgvi[i].t2[0] = (tgi->x + tgi->width) / w + xstep;
            txf->tgvi[i].t2[1] = (tgi->y + tgi->height) / h + ystep;
            txf->tgvi[i].v2[0] = tgi->xoffset + tgi->width;
            txf->tgvi[i].v2[1] = tgi->yoffset + tgi->height;
            txf->tgvi[i].t3[0] = tgi->x / w + xstep;
            txf->tgvi[i].t3[1] = (tgi->y + tgi->height) / h + ystep;
            txf->tgvi[i].v3[0] = tgi->xoffset;
            txf->tgvi[i].v3[1] = tgi->yoffset + tgi->height;
            txf->tgvi[i].advance = tgi->advance;
      }

      min_glyph = txf->tgi[0].c;
      max_glyph = txf->tgi[0].c;
      for (i = 1; i < txf->num_glyphs; i++) {
            if (txf->tgi[i].c < min_glyph) {
                  min_glyph = txf->tgi[i].c;
            }
            if (txf->tgi[i].c > max_glyph) {
                  max_glyph = txf->tgi[i].c;
            }
      }
      txf->min_glyph = min_glyph;
      txf->range = max_glyph - min_glyph + 1;

      txf->lut = (TexGlyphVertexInfo **)
                  calloc(txf->range, sizeof(TexGlyphVertexInfo *));
      if (txf->lut == NULL) {
            lastError = (char *)"out of memory.";
            goto error;
      }
      for (i = 0; i < txf->num_glyphs; i++) {
            txf->lut[txf->tgi[i].c - txf->min_glyph] = &txf->tgvi[i];
      }

      switch (format) {
            case TXF_FORMAT_BYTE:
                  if (useLuminanceAlpha) {
                        unsigned char *orig;

                        orig = (unsigned char *) malloc(txf->tex_width * txf->tex_height);
                        if (orig == NULL) {
                              lastError = (char *)"out of memory.";
                              goto error;
                        }
                        got = fread(orig, 1, txf->tex_width * txf->tex_height, file);
                        EXPECT(txf->tex_width * txf->tex_height);
                        txf->teximage = (unsigned char *)
                                    malloc(2 * txf->tex_width * txf->tex_height);
                        if (txf->teximage == NULL) {
                              lastError = (char *)"out of memory.";
                              goto error;
                        }
                        for (i = 0; i < txf->tex_width * txf->tex_height; i++) {
                              txf->teximage[i * 2] = orig[i];
                              txf->teximage[i * 2 + 1] = orig[i];
                        }
                        free(orig);
                  } else {
                        txf->teximage = (unsigned char *)
                                    malloc(txf->tex_width * txf->tex_height);
                        if (txf->teximage == NULL) {
                              lastError = (char *)"out of memory.";
                              goto error;
                        }
                        got = fread(txf->teximage, 1, txf->tex_width * txf->tex_height, file);
                        EXPECT(txf->tex_width * txf->tex_height);
                  }
                  break;
            case TXF_FORMAT_BITMAP:
                  width = txf->tex_width;
                  height = txf->tex_height;
                  stride = (width + 7) >> 3;
                  texbitmap = (unsigned char *) malloc(stride * height);
                  if (texbitmap == NULL) {
                        lastError = (char *)"out of memory.";
                        goto error;
                  }
                  got = fread(texbitmap, 1, stride * height, file);
                  EXPECT(stride * height);
                  if (useLuminanceAlpha) {
                        txf->teximage = (unsigned char *) calloc(width * height * 2, 1);
                        if (txf->teximage == NULL) {
                              lastError = (char *)"out of memory.";
                              goto error;
                        }
                        for (i = 0; i < height; i++) {
                              for (j = 0; j < width; j++) {
                                    if (texbitmap[i * stride + (j >> 3)] & (1 << (j & 7))) {
                                          txf->teximage[(i * width + j) * 2] = 255;
                                          txf->teximage[(i * width + j) * 2 + 1] = 255;
                                    }
                              }
                        }
                  } else {
                        txf->teximage = (unsigned char *) calloc(width * height, 1);
                        if (txf->teximage == NULL) {
                              lastError = (char *)"out of memory.";
                              goto error;
                        }
                        for (i = 0; i < height; i++) {
                              for (j = 0; j < width; j++) {
                                    if (texbitmap[i * stride + (j >> 3)] & (1 << (j & 7))) {
                                          txf->teximage[i * width + j] = 255;
                                    }
                              }
                        }
                  }
                  free(texbitmap);
                  break;
      }

      fclose(file);
      return txf;

error:

            if (txf) {
      if (txf->tgi)
            free(txf->tgi);
      if (txf->tgvi)
            free(txf->tgvi);
      if (txf->lut)
            free(txf->lut);
      if (txf->teximage)
            free(txf->teximage);
      free(txf);
            }
            if (file)
                  fclose(file);
            return NULL;
}

GLuint txfEstablishTexture(TexFont * txf, GLuint texobj, GLboolean setupMipmaps)
{
      glEnable(GL_TEXTURE_2D);
	  if (txf->texobj == 0) {
            if (texobj == 0) {
#if !defined(USE_DISPLAY_LISTS)
                  glGenTextures(1, &txf->texobj);
#else
                  txf->texobj = glGenLists(1);
#endif
            } else {
                  txf->texobj = texobj;
            }
      }
#if !defined(USE_DISPLAY_LISTS)
      glBindTexture(GL_TEXTURE_2D, txf->texobj);
#else
      glNewList(txf->texobj, GL_COMPILE);
#endif

#if 0
  /* XXX Indigo2 IMPACT in IRIX 5.3 and 6.2 does not support the GL_INTENSITY
      internal texture format. Sigh. Win32 non-GLX users should disable this
      code. */
      if (useLuminanceAlpha == 0) {
            char *vendor, *renderer, *version;

            renderer = (char *) glGetString(GL_RENDERER);
            vendor = (char *) glGetString(GL_VENDOR);
            if (!strcmp(vendor, "SGI") && !strncmp(renderer, "IMPACT", 6)) {
                  version = (char *) glGetString(GL_VERSION);
                  if (!strcmp(version, "1.0 Irix 6.2") ||
                       !strcmp(version, "1.0 Irix 5.3")) {
                        unsigned char *latex;
                        int width = txf->tex_width;
                        int height = txf->tex_height;
                        int i;

                        useLuminanceAlpha = 1;
                        latex = (unsigned char *) calloc(width * height * 2, 1);
                        /* XXX unprotected alloc. */
                        for (i = 0; i < height * width; i++) {
                              latex[i * 2] = txf->teximage[i];
                              latex[i * 2 + 1] = txf->teximage[i];
                        }
                        free(txf->teximage);
                        txf->teximage = latex;
                       }
            }
      }
#endif

      if (useLuminanceAlpha) {
            if (setupMipmaps) {
                  gluBuild2DMipmaps(GL_TEXTURE_2D, GL_LUMINANCE_ALPHA,
                                    txf->tex_width, txf->tex_height,
                                    GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, txf->teximage);
            } else {
                  glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA,
                               txf->tex_width, txf->tex_height, 0,
                               GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, txf->teximage);
            }
      } else {
#if defined(GL_VERSION_1_1) || defined(GL_EXT_texture)
    /* Use GL_INTENSITY4 as internal texture format since we want to use as
            little texture memory as possible. */
            if (setupMipmaps) {
                  gluBuild2DMipmaps(GL_TEXTURE_2D, GL_INTENSITY4,
                                    txf->tex_width, txf->tex_height,
                                    GL_LUMINANCE, GL_UNSIGNED_BYTE, txf->teximage);
            } else {
                  glTexImage2D(GL_TEXTURE_2D, 0, GL_INTENSITY4,
                               txf->tex_width, txf->tex_height, 0,
                               GL_LUMINANCE, GL_UNSIGNED_BYTE, txf->teximage);
            }
#else
            abort();            /* Should not get here without EXT_texture or OpenGL
            1.1. */
#endif
      }

#if defined(USE_DISPLAY_LISTS)
      glEndList();
      glCallList(txf->texobj);
#endif
      return txf->texobj;
}

void
            txfBindFontTexture(
                               TexFont * txf)
{
#if !defined(USE_DISPLAY_LISTS)
      glBindTexture(GL_TEXTURE_2D, txf->texobj);
#else
      glCallList(txf->texobj);
#endif
}

void
txfUnloadFont(
              TexFont * txf)
{
      if (txf->teximage) {
            free(txf->teximage);
      }
      free(txf->tgi);
      free(txf->tgvi);
      free(txf->lut);
      free(txf);
}

void  txfGetStringMetrics(
                                TexFont * txf,
                                char *string,
                                int len,
                                int *width,
                                int *max_ascent,
                                int *max_descent)
{
      TexGlyphVertexInfo *tgvi;
      int w, i;

      w = 0;
      for (i = 0; i < len; i++) {
            if (string[i] == 27) {
                  switch (string[i + 1]) {
                        case 'M':
                              i += 4;
                              break;
                        case 'T':
                              i += 7;
                              break;
                        case 'L':
                              i += 7;
                              break;
                        case 'F':
                              i += 13;
                              break;
                  }
            } else {
                  tgvi = getTCVI(txf, string[i]);
                  if(tgvi)
                        w += tgvi->advance;
            }
      }
      *width = w;
      *max_ascent = txf->max_ascent;
      *max_descent = txf->max_descent;
}

void txfRenderGlyph(TexFont * txf, int c)
{
      if(c > 0)
      {
            TexGlyphVertexInfo *tgvi;

            tgvi = getTCVI(txf, c);
            glBegin(GL_QUADS);

            glTexCoord2fv(tgvi->t0);
            glVertex2sv(tgvi->v0);
            glTexCoord2fv(tgvi->t1);
            glVertex2sv(tgvi->v1);
            glTexCoord2fv(tgvi->t2);
            glVertex2sv(tgvi->v2);
            glTexCoord2fv(tgvi->t3);
            glVertex2sv(tgvi->v3);

            glEnd();
            glTranslatef(tgvi->advance, 0.0, 0.0);
      }
}

void
txfRenderString(TexFont * txf,
                            char *string,
                            int len)
{
      int i;

      for (i = 0; i < len; i++) {
            txfRenderGlyph(txf, string[i]);
      }
}

enum {
      MONO, TOP_BOTTOM, LEFT_RIGHT, FOUR
};

void
txfRenderFancyString(
                                TexFont * txf,
                                 char *string,
                                 int len)
{
      TexGlyphVertexInfo *tgvi;
      GLubyte c[4][3];
      int mode = MONO;
      int i;

      for (i = 0; i < len; i++) {
            if (string[i] == 27) {
                  switch (string[i + 1]) {
                        case 'M':
                              mode = MONO;
                              glColor3ubv((GLubyte *) & string[i + 2]);
                              i += 4;
                              break;
                        case 'T':
                              mode = TOP_BOTTOM;
                              memcpy(c, &string[i + 2], 6);
                              i += 7;
                              break;
                        case 'L':
                              mode = LEFT_RIGHT;
                              memcpy(c, &string[i + 2], 6);
                              i += 7;
                              break;
                        case 'F':
                              mode = FOUR;
                              memcpy(c, &string[i + 2], 12);
                              i += 13;
                              break;
                  }
            } else {
                  switch (mode) {
                        case MONO:
                              txfRenderGlyph(txf, string[i]);
                              break;
                        case TOP_BOTTOM:
                              tgvi = getTCVI(txf, string[i]);
                              glBegin(GL_QUADS);
                              glColor3ubv(c[0]);
                              glTexCoord2fv(tgvi->t0);
                              glVertex2sv(tgvi->v0);
                              glTexCoord2fv(tgvi->t1);
                              glVertex2sv(tgvi->v1);
                              glColor3ubv(c[1]);
                              glTexCoord2fv(tgvi->t2);
                              glVertex2sv(tgvi->v2);
                              glTexCoord2fv(tgvi->t3);
                              glVertex2sv(tgvi->v3);
                              glEnd();
                              glTranslatef(tgvi->advance, 0.0, 0.0);
                              break;
                        case LEFT_RIGHT:
                              tgvi = getTCVI(txf, string[i]);
                              glBegin(GL_QUADS);
                              glColor3ubv(c[0]);
                              glTexCoord2fv(tgvi->t0);
                              glVertex2sv(tgvi->v0);
                              glColor3ubv(c[1]);
                              glTexCoord2fv(tgvi->t1);
                              glVertex2sv(tgvi->v1);
                              glColor3ubv(c[1]);
                              glTexCoord2fv(tgvi->t2);
                              glVertex2sv(tgvi->v2);
                              glColor3ubv(c[0]);
                              glTexCoord2fv(tgvi->t3);
                              glVertex2sv(tgvi->v3);
                              glEnd();
                              glTranslatef(tgvi->advance, 0.0, 0.0);
                              break;
                        case FOUR:
                              tgvi = getTCVI(txf, string[i]);
                              glBegin(GL_QUADS);
                              glColor3ubv(c[0]);
                              glTexCoord2fv(tgvi->t0);
                              glVertex2sv(tgvi->v0);
                              glColor3ubv(c[1]);
                              glTexCoord2fv(tgvi->t1);
                              glVertex2sv(tgvi->v1);
                              glColor3ubv(c[2]);
                              glTexCoord2fv(tgvi->t2);
                              glVertex2sv(tgvi->v2);
                              glColor3ubv(c[3]);
                              glTexCoord2fv(tgvi->t3);
                              glVertex2sv(tgvi->v3);
                              glEnd();
                              glTranslatef(tgvi->advance, 0.0, 0.0);
                              break;
                  }
            }
      }
}

int txfInFont(TexFont * txf, int c)
{
//       TexGlyphVertexInfo *tgvi;

      /* NOTE: No uppercase/lowercase substituion. */
      if ((c >= txf->min_glyph) && (c < txf->min_glyph + txf->range)) {
            if (txf->lut[c - txf->min_glyph]) {
                  return 1;
            }
      }
      return 0;
}
