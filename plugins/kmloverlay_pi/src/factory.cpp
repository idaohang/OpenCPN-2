/***************************************************************************
 * $Id: factory.cpp, v0.1 2012-02-08 SethDart Exp $
 *
 * Project:  OpenCPN
 * Purpose:  KML overlay plugin
 * Author:   Jean-Eudes Onfray
 *
 ***************************************************************************
 *   Copyright (C) 2012 by Jean-Eudes Onfray                               *
 *   je@onfray.fr                                                          *
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
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************
 */

#include <wx/wxprec.h>

#ifndef  WX_PRECOMP
  #include <wx/wx.h>
#endif //precompiled headers

#include <iostream>
#include <kml/base/file.h>
#include "factory.h"

#if !wxCHECK_VERSION(2, 9, 0)
WX_DECLARE_LIST(wxPoint, wxPointList);
#include <wx/listimpl.cpp>
WX_DEFINE_LIST(wxPointList);
#endif

const wxColor KMLOverlayColor = wxColor( 144, 144, 144 );

KMLOverlayFactory::KMLOverlayFactory()
{
}

KMLOverlayFactory::~KMLOverlayFactory()
{
      for ( size_t i = m_Objects.GetCount(); i > 0; i-- )
      {
            Container *cont = m_Objects.Item( i-1 );
            m_Objects.Remove( cont );
            delete cont;
      }
}

bool KMLOverlayFactory::RenderOverlay( wxDC &dc, PlugIn_ViewPort *vp )
{
      for ( size_t i = 0; i < m_Objects.GetCount(); i++ )
      {
            m_Objects.Item( i )->Render( dc, vp );
      }
      return true;
}

bool KMLOverlayFactory::Add( wxString filename, bool visible )
{
      Container *cont = new Container( filename, visible );
      if ( cont->Parse() )
      {
            m_Objects.Add( cont );
            RequestRefresh( GetOCPNCanvasWindow() );
            return true;
      }
      else
      {
            delete cont;
            return false;
      }
}

void KMLOverlayFactory::SetVisibility( int idx, bool visible )
{
      m_Objects.Item( idx )->SetVisibility( visible );
      RequestRefresh( GetOCPNCanvasWindow() );
}

void KMLOverlayFactory::Delete( int idx )
{
      Container *cont = m_Objects.Item( idx );
      m_Objects.Remove( cont );
      delete cont;
      RequestRefresh( GetOCPNCanvasWindow() );
}

wxString KMLOverlayFactory::GetFilename( int idx )
{
      return m_Objects.Item( idx )->GetFilename();
}

bool KMLOverlayFactory::GetVisibility( int idx )
{
      return m_Objects.Item( idx )->GetVisibility();
}

int KMLOverlayFactory::GetCount()
{
      return m_Objects.GetCount();
}

KMLOverlayFactory::Container::Container( wxString filename, bool visible )
     : m_ready( false ), m_filename( filename ), m_visible( visible )
{
}

bool KMLOverlayFactory::Container::Parse()
{
      std::string _filename( m_filename.mb_str() );
      std::string file_data;
      if ( !kmlbase::File::ReadFileToString( _filename, &file_data ) ) {
            // TODO: display error message
            return false;
      }

      std::string errors;
      kmlengine::KmlFilePtr kml_file = kmlengine::KmlFile::CreateFromParse( file_data, &errors );
      if ( !kml_file ) {
            // TODO: display error message
            return false;
      }

/*
      kmldom::ElementPtr element = kml_file->get_root();
      const kmldom::KmlPtr kml = kmldom::AsKml( element );
      m_root = kml->get_feature();
*/
      m_root = kmlengine::GetRootFeature( kml_file->get_root() );

      m_ready = true;
      return true;
}

void KMLOverlayFactory::Container::RenderPoint( wxDC &dc, PlugIn_ViewPort *vp, const kmldom::PointPtr& point )
{
      if ( point->has_coordinates() ) {
            int radius = 5;
            wxPoint pt;

            kmldom::CoordinatesPtr coord = point->get_coordinates();
            if ( coord->get_coordinates_array_size() != 1 )
            {
                  // TODO: log error: a point should have only one coord
                  return;
            }
            kmlbase::Vec3 vec = coord->get_coordinates_array_at( 0 );
            GetCanvasPixLL( vp,  &pt, vec.get_latitude(), vec.get_longitude() );

            wxPen pen( KMLOverlayColor, 1 );
            dc.SetPen( pen );
            dc.SetBrush( KMLOverlayColor );
            dc.DrawCircle( pt, radius );
      }
}

void KMLOverlayFactory::Container::RenderLineString( wxDC &dc, PlugIn_ViewPort *vp, const kmldom::LineStringPtr& linestring )
{
      if ( linestring->has_coordinates() ) {
            wxPointList ptlist;
            kmldom::CoordinatesPtr coord = linestring->get_coordinates();
            for ( size_t i = 0; i < coord->get_coordinates_array_size(); ++i ) {
                  kmlbase::Vec3 vec = coord->get_coordinates_array_at( i );
                  wxPoint* pt = new wxPoint();
                  GetCanvasPixLL( vp,  pt, vec.get_latitude(), vec.get_longitude() );
                  ptlist.Append ( pt );
            }

            wxPen pen( KMLOverlayColor, 2 );
            dc.SetPen( pen );
            dc.DrawLines( (wxList *)&ptlist );
            ptlist.DeleteContents(true);
            ptlist.Clear();
      }
}

void KMLOverlayFactory::Container::RenderLinearRing( wxDC &dc, PlugIn_ViewPort *vp, const kmldom::LinearRingPtr& linearring )
{
      if ( linearring->has_coordinates() ) {
            wxPointList ptlist;
            kmldom::CoordinatesPtr coord = linearring->get_coordinates();
            for ( size_t i = 0; i < coord->get_coordinates_array_size(); ++i ) {
                  kmlbase::Vec3 vec = coord->get_coordinates_array_at( i );
                  wxPoint* pt = new wxPoint();
                  GetCanvasPixLL( vp,  pt, vec.get_latitude(), vec.get_longitude() );
                  ptlist.Append ( pt );
            }

            wxPen pen( KMLOverlayColor, 2 );
            dc.SetPen( pen );
            dc.SetBrush( *wxTRANSPARENT_BRUSH );
            dc.DrawPolygon( (wxList *)&ptlist );
            ptlist.DeleteContents(true);
            ptlist.Clear();
      }
}

void KMLOverlayFactory::Container::RenderPolygon( wxDC &dc, PlugIn_ViewPort *vp, const kmldom::PolygonPtr& polygon )
{
      if ( polygon->has_outerboundaryis() ) {
            kmldom::OuterBoundaryIsPtr bound = polygon->get_outerboundaryis();
            if ( bound->has_linearring() ) {
                  RenderLinearRing( dc, vp, bound->get_linearring() );
            }
// TODO: handle inner boundary (array)
      }
}

void KMLOverlayFactory::Container::RenderGroundOverlay( wxDC &dc, PlugIn_ViewPort *vp, const kmldom::GroundOverlayPtr& groundoverlay )
{
// See bool GRIBOverlayFactory::RenderGribSigWh(GribRecord *pGR, wxDC &dc, PlugIn_ViewPort *vp)
/* TODO
      if groundoverlay->has_latlonbox()
            groundoverlay->get_latlonbox()
 should we handle <gx:LatLonQuad> (Used for nonrectangular quadrilateral ground overlays.)
   has_gx_latlonquad get_gx_latlonquad
 */
}

void KMLOverlayFactory::Container::RenderGeometry( wxDC &dc, PlugIn_ViewPort *vp, const kmldom::GeometryPtr& geometry )
{
      if ( !geometry ) {
            return;
      }

      switch ( geometry->Type() ) {
      case kmldom::Type_Point:
      {
            if ( const kmldom::PointPtr point = kmldom::AsPoint( geometry ) ) {
                  RenderPoint( dc, vp, point );
            }
      }
      break;
      case kmldom::Type_LineString:
      {
            if ( const kmldom::LineStringPtr linestring = kmldom::AsLineString( geometry ) ) {
                  RenderLineString( dc, vp, linestring );
            }
      }
      break;
      case kmldom::Type_LinearRing:
      {
            if ( const kmldom::LinearRingPtr linearring = kmldom::AsLinearRing( geometry ) ) {
                  RenderLinearRing( dc, vp, linearring );
            }
      }
      break;
      case kmldom::Type_Polygon:
      {
            if ( const kmldom::PolygonPtr polygon = kmldom::AsPolygon( geometry ) ) {
                  RenderPolygon( dc, vp, polygon );
            }
      }
      break;
      case kmldom::Type_MultiGeometry:
      {
            if ( const kmldom::MultiGeometryPtr multigeometry = kmldom::AsMultiGeometry( geometry ) )
            {
                  for ( size_t i = 0; i < multigeometry->get_geometry_array_size(); ++i ) {
                        RenderGeometry( dc, vp, multigeometry->get_geometry_array_at( i ) );
                  }
            }
      }
      break;
      case kmldom::Type_Model:
      break;
      default:  // KML has 6 types of Geometry.
      break;
      }
}

void KMLOverlayFactory::Container::RenderFeature( wxDC &dc, PlugIn_ViewPort *vp, const kmldom::FeaturePtr& feature)
{
      if ( !feature )
      {
            return;
      }
      if ( !feature->get_visibility() )
      {
            return;
      }

/* Can we do something with name?
      if ( feature->has_name() ) {
            std::cout << "Render " << feature->get_name() << std::endl;
      }
 */

      switch ( feature->Type() ) {
      case kmldom::Type_Document:
            // Document is a container, handled below.
      break;
      case kmldom::Type_Folder:
            // Folder is a container, handled below.
      break;
      case kmldom::Type_GroundOverlay:
      {
            if ( const kmldom::GroundOverlayPtr groundoverlay = kmldom::AsGroundOverlay( feature ) ) {
                  RenderGroundOverlay( dc, vp, groundoverlay );
            }
      }
      break;
      case kmldom::Type_Placemark:
      {
            if ( const kmldom::PlacemarkPtr placemark = kmldom::AsPlacemark( feature ) ) {
                  RenderGeometry( dc, vp, placemark->get_geometry() );
            }
      }
      break;
      default:
      break;
      }

      if ( const kmldom::ContainerPtr container = kmldom::AsContainer( feature ) ) {
            for ( size_t i = 0; i < container->get_feature_array_size(); ++i ) {
                  RenderFeature( dc, vp, container->get_feature_array_at( i ) );
            }
      }
}

bool KMLOverlayFactory::Container::Render( wxDC &dc, PlugIn_ViewPort *vp )
{
      if ( !m_ready )
            return false;

      if ( !m_visible )
            return true;

      RenderFeature( dc, vp, m_root );
      return true;
}

void KMLOverlayFactory::Container::SetVisibility( bool visible )
{
      m_visible = visible;
}

wxString KMLOverlayFactory::Container::GetFilename()
{
      return m_filename;
}

bool KMLOverlayFactory::Container::GetVisibility()
{
      return m_visible;
}
