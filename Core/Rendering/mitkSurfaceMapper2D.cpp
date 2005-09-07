/*=========================================================================

Program:   Medical Imaging & Interaction Toolkit
Module:    $RCSfile$
Language:  C++
Date:      $Date$
Version:   $Revision$

Copyright (c) German Cancer Research Center, Division of Medical and
Biological Informatics. All rights reserved.
See MITKCopyright.txt or http://www.mitk.org/copyright.html for details.

This software is distributed WITHOUT ANY WARRANTY; without even
the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
PURPOSE.  See the above copyright notices for more information.

=========================================================================*/


#include <mitkGL.h>
#include "mitkSurfaceMapper2D.h"
#include "mitkBaseRenderer.h"
#include "mitkPlaneGeometry.h"
#include "mitkSurface.h"
#include "mitkColorProperty.h"
#include "mitkProperties.h"
#include "mitkAbstractTransformGeometry.h"

#include <vtkPolyData.h>
#include <vtkPolyDataSource.h>
#include <vtkPlane.h>
#include <vtkCutter.h>
#include <vtkPoints.h>
#include <vtkCellArray.h>
#include <vtkLookupTable.h>
#include <vtkPointData.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkPolyData.h>
#include <vtkLinearTransform.h>

//##ModelId=3EF180540006
mitk::SurfaceMapper2D::SurfaceMapper2D()
{
  m_Plane  = vtkPlane::New();
  m_Cutter = vtkCutter::New();

  m_Cutter->SetCutFunction(m_Plane);
  m_Cutter->GenerateValues(1,0,1);

  m_LUT = vtkLookupTable::New();
  m_LUT->SetTableRange(0,255);
  m_LUT->SetNumberOfColors(255);
  m_LUT->SetRampToLinear ();
  m_LUT->Build();
}

//##ModelId=3EF180540019
mitk::SurfaceMapper2D::~SurfaceMapper2D()
{
}

//##ModelId=3EF18053036B
const mitk::Surface *mitk::SurfaceMapper2D::GetInput(void)
{
  if(m_Surface.IsNotNull())
    return m_Surface;

  return static_cast<const mitk::Surface * > ( GetData() );
}

//##ModelId=3EF18053039D
void mitk::SurfaceMapper2D::Paint(mitk::BaseRenderer * renderer)
{
  if(IsVisible(renderer)==false) return;

  mitk::Surface::Pointer input  = const_cast<mitk::Surface*>(this->GetInput());

  if(input.IsNull())
    return;

  //
  // get the TimeSlicedGeometry of the input object
  //
  const TimeSlicedGeometry* inputTimeGeometry = input->GetTimeSlicedGeometry();
  if(( inputTimeGeometry == NULL ) || ( inputTimeGeometry->GetTimeSteps() == 0 ) )
    return;

  bool useCellData;
  if (dynamic_cast<mitk::BoolProperty *>(this->GetDataTreeNode()->GetProperty("useCellDataForColouring").GetPointer()) == NULL)
    useCellData = false;
  else
    useCellData = dynamic_cast<mitk::BoolProperty *>(this->GetDataTreeNode()->GetProperty("useCellDataForColouring").GetPointer())->GetValue();

  if (dynamic_cast<mitk::IntProperty *>(this->GetDataTreeNode()->GetProperty("lineWidth").GetPointer()) == NULL)
    m_LineWidth = 1;
  else
    m_LineWidth = dynamic_cast<mitk::IntProperty *>(this->GetDataTreeNode()->GetProperty("lineWidth").GetPointer())->GetValue();

  //
  // get the world time
  //
  Geometry2D::ConstPointer worldGeometry = renderer->GetCurrentWorldGeometry2D();
  assert( worldGeometry.IsNotNull() );
  ScalarType time = worldGeometry->GetTimeBounds()[ 0 ];

  //
  // convert the world time to time steps of the input object
  //
  int timestep=0;
  if( time > -ScalarTypeNumericTraits::max() )
    timestep = inputTimeGeometry->MSToTimeStep( time );
  if( inputTimeGeometry->IsValidTime( timestep ) == false )
    return;

  vtkPolyData * vtkpolydata = input->GetVtkPolyData( timestep );
  if((vtkpolydata==NULL) || (vtkpolydata->GetNumberOfPoints() < 1 ))
    return;

  PlaneGeometry::ConstPointer worldPlaneGeometry = dynamic_cast<const PlaneGeometry*>(worldGeometry.GetPointer());

  if(vtkpolydata!=NULL)
  {
    Point3D point;
    Vector3D normal;

    vtkLinearTransform * vtktransform = GetDataTreeNode()->GetVtkTransform();
    if(worldPlaneGeometry.IsNotNull())
    {
      // set up vtkPlane according to worldGeometry
      point=worldPlaneGeometry->GetOrigin();
      normal=worldPlaneGeometry->GetNormal(); normal.Normalize();
      m_Plane->SetTransform((vtkAbstractTransform*)NULL);
    }
    else
    {
      AbstractTransformGeometry::ConstPointer worldAbstractGeometry = dynamic_cast<const AbstractTransformGeometry*>(renderer->GetCurrentWorldGeometry2D());
      if(worldAbstractGeometry.IsNotNull())
      {
        AbstractTransformGeometry::ConstPointer surfaceAbstractGeometry = dynamic_cast<const AbstractTransformGeometry*>(input->GetTimeSlicedGeometry()->GetGeometry3D(0));
        if(surfaceAbstractGeometry.IsNotNull()) //@todo substitude by operator== after implementation, see bug id 28
        {
          PaintCells(vtkpolydata, worldGeometry, renderer->GetDisplayGeometry(), vtktransform, (useCellData ? m_LUT : NULL));
          return;
        }
        else
        {
          //@FIXME: does not work correctly. Does m_Plane->SetTransform really transforms a "plane plane" into a "curved plane"?
          return;
          // set up vtkPlane according to worldGeometry
          point=const_cast<mitk::BoundingBox*>(worldAbstractGeometry->GetParametricBoundingBox())->GetMinimum();
          FillVector3D(normal, 0, 0, 1);
          m_Plane->SetTransform(worldAbstractGeometry->GetVtkAbstractTransform()->GetInverse());
        }
      }
      else
        return;
    }

#if ((VTK_MAJOR_VERSION > 4) || ((VTK_MAJOR_VERSION==4) && (VTK_MINOR_VERSION>=4) ))
    double vp[3], vnormal[3];
#else
    float vp[3], vnormal[3];
#endif

    vnl2vtk(point.Get_vnl_vector(), vp);
    vnl2vtk(normal.Get_vnl_vector(), vnormal);

    //normally, we would need to transform the surface and cut the transformed surface with the cutter.
    //This might be quite slow. Thus, the idea is, to perform an inverse transform of the plane instead.
    //@todo It probably does not work for scaling operations yet:scaling operations have to be 
    //dealed with after the cut is performed by scaling the contour.
    vtkLinearTransform * inversetransform = vtktransform->GetLinearInverse();
    inversetransform->TransformPoint(vp, vp);
    inversetransform->TransformNormalAtPoint(vp, vnormal, vnormal);

    m_Plane->SetOrigin(vp);
    m_Plane->SetNormal(vnormal);

    // set data into cutter
    m_Cutter->SetInput(vtkpolydata);
    //    m_Cutter->GenerateCutScalarsOff();
    //    m_Cutter->SetSortByToSortByCell();

    // calculate the cut
    m_Cutter->Update();

    //apply color and opacity read from the PropertyList
    ApplyProperties(renderer);

    // travers the cut contour
    PaintCells(m_Cutter->GetOutput(), worldGeometry, renderer->GetDisplayGeometry(), vtktransform, (useCellData ? m_LUT : NULL));
  }
}

void mitk::SurfaceMapper2D::PaintCells(vtkPolyData* contour, const mitk::Geometry2D* worldGeometry, const mitk::DisplayGeometry* displayGeometry, vtkLinearTransform * vtktransform, vtkLookupTable *lut)
{
  vtkPoints    *vpoints = contour->GetPoints();
  vtkCellArray *vpolys  = contour->GetLines();

  vtkCellData * vcelldata    = contour->GetCellData();
  vtkDataArray* vcellscalars = vcelldata->GetScalars();

  int i,numberOfCells=vpolys->GetNumberOfCells();

  Point3D p; Point2D p2d, last, first;

  vpolys->InitTraversal();
  for(i=0;i<numberOfCells;++i)
  {
    int *cell, cellSize;
#if ((VTK_MAJOR_VERSION > 4) || ((VTK_MAJOR_VERSION==4) && (VTK_MINOR_VERSION>=4) ))
    double vp[3];
#else
    float vp[3];
#endif

    vpolys->GetNextCell(cellSize, cell);

    vpoints->GetPoint(cell[0], vp);
    //take transformation via vtktransform into account 	 
    vtktransform->TransformPoint(vp, vp);
    vtk2itk(vp, p);

    //convert 3D point (in mm) to 2D point on slice (also in mm)
    worldGeometry->Map(p, p2d);

    //convert point (until now mm and in worldcoordinates) to display coordinates (units )
    displayGeometry->WorldToDisplay(p2d, p2d);
    first=last=p2d;

    int j;
    for(j=1;j<cellSize;++j)
    {
      vpoints->GetPoint(cell[j], vp);
      //take transformation via vtktransform into account 	 
      vtktransform->TransformPoint(vp, vp);
      vtk2itk(vp, p);

      //convert 3D point (in mm) to 2D point on slice (also in mm)
      worldGeometry->Map(p, p2d);

      //convert point (until now mm and in worldcoordinates) to display coordinates (units )
      displayGeometry->WorldToDisplay(p2d, p2d);

      glLineWidth( m_LineWidth );

      if (lut != NULL) 
      {  // color each cell according to cell data

#if ((VTK_MAJOR_VERSION > 4) || ((VTK_MAJOR_VERSION==4) && (VTK_MINOR_VERSION>=4) ))
        double *color;
#else
        float *color;
#endif

        if (vcellscalars != NULL )
        {
          vcellscalars->GetComponent(i,0);
          color = lut->GetColor( vcellscalars->GetComponent(i,0) );
          glColor3f(color[0],color[1],color[2]);
        }
      }
      //draw the line
      glBegin (GL_LINE_LOOP);

      glVertex2f(last[0], last[1]);
      glVertex2f(p2d[0], p2d[1]);
      glVertex2f(last[0], last[1]);
      glEnd ();
      glLineWidth(1.0);
      last=p2d;
    }
  }
}
