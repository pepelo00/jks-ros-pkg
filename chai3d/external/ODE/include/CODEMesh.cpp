//===========================================================================
/*
    This file is part of the CHAI 3D visualization and haptics libraries.
    Copyright (C) 2003-2008 by CHAI 3D. All rights reserved.

    This library is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License("GPL") version 2
    as published by the Free Software Foundation.

    For using the CHAI 3D libraries with software that can not be combined
    with the GNU GPL, and for taking advantage of the additional benefits
    of our support services, please contact CHAI 3D about acquiring a
    Professional Edition License.

    \author:    <http://www.chai3d.org>
    \author:    Chris Sewell
*/
//===========================================================================

//---------------------------------------------------------------------------

#include "stdafx.h"
#ifdef _MSVC
#pragma warning (disable : 4786)
#endif

#include "CODEMesh.h"
#include "chai3d.h"
#include <algorithm>
#include <conio.h>

//---------------------------------------------------------------------------

#define SQR(x) ((x)*(x))
#define CUBE(x) ((x)*(x)*(x))
#define X 0
#define Y 1
#define Z 2
//---------------------------------------------------------------------------

//===========================================================================
/*!
    Constructor of cODEMesh

    \fn       cODEMesh::cODEMesh(cWorld* a_parent)
    \param    a_parent  Pointer to parent world.
*/
//===========================================================================
cODEMesh::cODEMesh(cWorld* a_parent, dWorldID a_odeWorld, dSpaceID a_odeSpace) : cMesh(a_parent)
{
    m_odeVertex = NULL;
    m_odeIndices = NULL;
    m_odeGeom = NULL;
    m_odeTriMeshData = NULL;
    m_odeBody   = NULL;
    m_odeWorld = a_odeWorld;
    m_odeSpace = a_odeSpace;
}


//===========================================================================
/*!
    Destructor of cODEMesh.

    \fn     cODEMesh::~cODEMesh()
*/
//===========================================================================
cODEMesh::~cODEMesh()
{
    if (m_odeVertex  != NULL)           delete []m_odeVertex;
    if (m_odeIndices != NULL)           delete []m_odeIndices;
    if (m_odeGeom != NULL)          dGeomDestroy(m_odeGeom);
    if (m_odeTriMeshData != NULL)       dGeomTriMeshDataDestroy(m_odeTriMeshData);
    if (m_odeBody != NULL)          dBodyDestroy(m_odeBody);
    
    m_Joint.clear();
}


//===========================================================================
/*!
    Initialize the dynamic for the cODEMesh.

    \fn     cODEMesh::initDynamic()
*/
//===========================================================================
void cODEMesh::initDynamic(objectType a_objType, float a_x, 
                           float a_y, float a_z, float a_density) 
{
    
    unsigned int nVertex,nIndices;
    float l[3];
       
    computeBoundaryBox(true);
    
    cgx = m_boundaryBoxMin.x + (m_boundaryBoxMax.x - m_boundaryBoxMin.x) / 2.0;
    cgy = m_boundaryBoxMin.y + (m_boundaryBoxMax.y - m_boundaryBoxMin.y) / 2.0;
    cgz = m_boundaryBoxMin.z + (m_boundaryBoxMax.z - m_boundaryBoxMin.z) / 2.0;
    
    translateMesh(cgx, cgy, cgz);
    
    cODEMeshToODE(nVertex,nIndices);
    
    l[0] = m_boundaryBoxMax.x - m_boundaryBoxMin.x;
    l[1] = m_boundaryBoxMax.y - m_boundaryBoxMin.y;
    l[2] = m_boundaryBoxMax.z - m_boundaryBoxMin.z;
    
    float r;
    if ((l[0]>l[1]) && (l[0]>l[2]))
        r = l[0]/2.0;
    else if ((l[1]>l[0]) && (l[1]>l[2]))
        r = l[1]/2.0;
    else
        r = l[2]/2.0;
       
    // Create a box that wraps the mesh.
    m_odeGeom = dCreateBox(m_odeSpace,l[0] ,l[1], l[2]);
        
    dGeomSetPosition(m_odeGeom, a_x, a_y, a_z);
    setPos(a_x,a_y,a_z);

   // computeGlobalPositions(1);
    
    m_objType = a_objType;
    if (a_objType == DYNAMIC_OBJECT) 
    {      
        //Create the body,...the physical entity...in the world.
        m_odeBody          = dBodyCreate(m_odeWorld);
        dBodySetPosition(m_odeBody, a_x, a_y, a_z);
        
        //Create the mass entity, calculate the inertia matrix and link all at the body.   
        dMassSetBox(&m_odeMass,a_density,l[0],l[1],l[2]);
        dBodySetMass(m_odeBody,&m_odeMass);
        dGeomSetBody(m_odeGeom,m_odeBody);
        
        dBodySetPosition(m_odeBody, a_x, a_y, a_z);
    } 
}


//===========================================================================
/*!
    Create a copy of the mesh compatible with ODE

    \fn     cODEMesh::cODEMeshToODE(unsigned int n,unsigned int &vertexcount,unsigned int &a_indexcount)
*/
//===========================================================================
void cODEMesh::cODEMeshToODE(unsigned int &a_vertexcount,unsigned int &a_indexcount)
{
    
    a_vertexcount   = getNumVertices(true);
    
    a_indexcount = getNumTriangles(true);
    
    //if (odeVertex != NULL)    delete []odeVertex;
    m_odeVertex   = new float[a_vertexcount*3];
    m_odeIndices =  new odeVector3[a_indexcount];
    
    // for each submesh in a mesh we need to:
    //      - load such submesh
    //      - loop on all the triangles of the mesh and on all the indices
    
    int total_index_vertex = 0;
    int total_index_index = 0;
    for (int submesh_num = 0; submesh_num<this->getNumChildren(); submesh_num++)
    {
        
        cMesh * submesh = (cMesh*)this->getChild(submesh_num);
        
        // submesh total vertices
        int submesh_tvertices = submesh->getNumVertices();
        
        for (unsigned int nVertex = 0;nVertex<submesh_tvertices; nVertex++) 
        {
            unsigned int offset = total_index_vertex*3;
            cVertex *v = submesh->getVertex(nVertex);
            m_odeVertex[offset    ]  = v->getPos().x;
            m_odeVertex[offset + 1]  = v->getPos().y;
            m_odeVertex[offset + 2]  = v->getPos().z;
            total_index_vertex++;
        }
        
        // submesh total indices
        int submesh_tindices = submesh->getNumTriangles();
        
        for (unsigned int nIndex  = 0;nIndex<submesh_tindices;   nIndex++ ) 
        {
            cTriangle *t = submesh->getTriangle(nIndex);
            m_odeIndices[total_index_index][0]  = t->getIndexVertex0();
            m_odeIndices[total_index_index][1]  = t->getIndexVertex1();
            m_odeIndices[total_index_index][2]  = t->getIndexVertex2();
            total_index_index++;
        }        
    }
}


//===========================================================================
/*!
    Update the position and rotation of the cODEMesh to the body value.

    \fn     cODEMesh::updateDynamicPosition()
*/
//===========================================================================
void cODEMesh::updateDynamicPosition()
{
    
    const float *odePosition;
    const float *odeRotation;
    cMatrix3d   chaiRotation;
    
    if (m_objType == DYNAMIC_OBJECT)
    {
        odePosition =  dBodyGetPosition(m_odeBody);
        odeRotation =  dBodyGetRotation(m_odeBody);
    }
    else {
        
        odePosition =  dGeomGetPosition(m_odeGeom);
        odeRotation =  dGeomGetRotation(m_odeGeom);
    }
    
    chaiRotation.set(odeRotation[0],odeRotation[1],odeRotation[2],
        odeRotation[4],odeRotation[5],odeRotation[6],
        odeRotation[8],odeRotation[9],odeRotation[10]);
    
    setRot(chaiRotation);
    setPos(odePosition[0],odePosition[1],odePosition[2]);
    
  //  computeGlobalPositions(1);
}


//===========================================================================
/*!
Set the dynamic position of the cODEMesh to the given value.

  \fn     cODEMesh::setDynamicPosition(cVector3d &pos)
*/
//===========================================================================
void cODEMesh::setDynamicPosition(cVector3d &a_pos)
{
    dGeomSetPosition(m_odeGeom, a_pos.x, a_pos.y, a_pos.z);
    setPos(a_pos.x,a_pos.y,a_pos.z);
 //   computeGlobalPositions(1);

    if (m_objType == DYNAMIC_OBJECT)
    {
        dBodySetPosition(m_odeBody, a_pos.x, a_pos.y, a_pos.z);
    }
}


//===========================================================================
/*!
Calculate the inertia tensor and and the center of mass. The origin of inertia tensor is
in the center of mass.

  \fn     cODEMesh::calculateInertiaTensor(float density, float &cgx, float &cgy, float &cgz,
  float &I11, float &I22, float &I33,
  float &I12, float &I13, float &I23)
*/
//===========================================================================
void cODEMesh::calculateInertiaTensor(float density, float &mass,
                                      float &cgx, float &cgy, float &cgz,
                                      float &I11, float &I22, float &I33,
                                      float &I12, float &I13, float &I23)
{
    
    unsigned int vertexcount =  getNumVertices(true);
    unsigned int triangles   =  getNumTriangles(true);
    
    //face's offset from the origin..no need to compute all..but just once at time
    double w;
    //face's normal...no need to compute all normal...but  just once at time
    double norm[3];
    
    int A;   // alpha
    int B;   // beta
    int C;   // gamma
    
    // projection integrals
    double P1, Pa, Pb, Paa, Pab, Pbb, Paaa, Paab, Pabb, Pbbb;
    
    // face integrals
    double Fa, Fb, Fc, Faa, Fbb, Fcc, Faaa, Fbbb, Fccc, Faab, Fbbc, Fcca;
    
    // volume integrals
    double T0, T1[3], T2[3], TP[3];
    
    // center of mass
    double r[3];
    
    // inertia tensor
    double J[3][3];
    
    //======================= compute volume integrals =============================//
    double nx, ny, nz;
    double dx1, dy1, dz1, dx2, dy2, dz2,len;
    double k1, k2, k3, k4;
    int i;
    float vertex0[3],vertex1[3],vertex2[3];
    unsigned int index0,index1,index2;
    
    T0 = T1[X] = T1[Y] = T1[Z]
        = T2[X] = T2[Y] = T2[Z]
        = TP[X] = TP[Y] = TP[Z] = 0;
    
    for (i = 0; i < triangles; i++) {
        
        unsigned int offset = i;
        index0 = m_odeIndices[i][0]*3;
        index1 = m_odeIndices[i][1]*3;
        index2 = m_odeIndices[i][2]*3;
        //face i..calculate the normal and w
        vertex0[X] = m_odeVertex[index0 + X];
        vertex0[Y] = m_odeVertex[index0 + Y];
        vertex0[Z] = m_odeVertex[index0 + Z];
        
        vertex1[X] = m_odeVertex[index1 + X];
        vertex1[Y] = m_odeVertex[index1 + Y];
        vertex1[Z] = m_odeVertex[index1 + Z];
        
        vertex2[X] = m_odeVertex[index2 + X];
        vertex2[Y] = m_odeVertex[index2 + Y];
        vertex2[Z] = m_odeVertex[index2 + Z];
        
        dx1 = vertex1[X] - vertex0[X];
        dy1 = vertex1[Y] - vertex0[Y];
        dz1 = vertex1[Z] - vertex0[Z];
        dx2 = vertex2[X] - vertex1[X];
        dy2 = vertex2[Y] - vertex1[Y];
        dz2 = vertex2[Z] - vertex1[Z];
        
        nx = dy1 * dz2 - dy2 * dz1;
        ny = dz1 * dx2 - dz2 * dx1;
        nz = dx1 * dy2 - dx2 * dy1;
        
        len = sqrt(nx * nx + ny * ny + nz * nz);
        
        norm[X] = nx / len;
        norm[Y] = ny / len;
        norm[Z] = nz / len;
        
        w = - norm[X] * vertex0[X]
            - norm[Y] * vertex0[Y]
            - norm[Z] * vertex0[Z];
        
        nx = fabs(norm[X]);
        ny = fabs(norm[Y]);
        nz = fabs(norm[Z]);
        if (nx > ny && nx > nz) C = X;
        else C = (ny > nz) ? Y : Z;
        A = (C + 1) % 3;
        B = (A + 1) % 3;
        
        //======================= compute face integrals =============================//
        //======================= compute projection integrals =======================//
        
        double a0, a1, da;
        double b0, b1, db;
        double a0_2, a0_3, a0_4, b0_2, b0_3, b0_4;
        double a1_2, a1_3, b1_2, b1_3;
        double C1, Ca, Caa, Caaa, Cb, Cbb, Cbbb;
        double Cab, Kab, Caab, Kaab, Cabb, Kabb;
        int j;
        
        P1 = Pa = Pb = Paa = Pab = Pbb = Paaa = Paab = Pabb = Pbbb = 0.0;
        
        for (j = 0; j < 3; j++) {
            unsigned int tindex0 = m_odeIndices[offset][j]*3;
            unsigned int tindex1 = m_odeIndices[offset][(j+1)%3]*3;
            a0 = m_odeVertex[tindex0 + A];
            b0 = m_odeVertex[tindex0 + B];
            a1 = m_odeVertex[tindex1 + A];
            b1 = m_odeVertex[tindex1 + B];
            da = a1 - a0;
            db = b1 - b0;
            a0_2 = a0 * a0; a0_3 = a0_2 * a0; a0_4 = a0_3 * a0;
            b0_2 = b0 * b0; b0_3 = b0_2 * b0; b0_4 = b0_3 * b0;
            a1_2 = a1 * a1; a1_3 = a1_2 * a1;
            b1_2 = b1 * b1; b1_3 = b1_2 * b1;
            
            C1 = a1 + a0;
            Ca = a1*C1 + a0_2; Caa = a1*Ca + a0_3; Caaa = a1*Caa + a0_4;
            Cb = b1*(b1 + b0) + b0_2; Cbb = b1*Cb + b0_3; Cbbb = b1*Cbb + b0_4;
            Cab = 3*a1_2 + 2*a1*a0 + a0_2; Kab = a1_2 + 2*a1*a0 + 3*a0_2;
            Caab = a0*Cab + 4*a1_3; Kaab = a1*Kab + 4*a0_3;
            Cabb = 4*b1_3 + 3*b1_2*b0 + 2*b1*b0_2 + b0_3;
            Kabb = b1_3 + 2*b1_2*b0 + 3*b1*b0_2 + 4*b0_3;
            
            P1 += db*C1;
            Pa += db*Ca;
            Paa += db*Caa;
            Paaa += db*Caaa;
            Pb += da*Cb;
            Pbb += da*Cbb;
            Pbbb += da*Cbbb;
            Pab += db*(b1*Cab + b0*Kab);
            Paab += db*(b1*Caab + b0*Kaab);
            Pabb += da*(a1*Cabb + a0*Kabb);
        }
        
        P1 /= 2.0;
        Pa /= 6.0;
        Paa /= 12.0;
        Paaa /= 20.0;
        Pb /= -6.0;
        Pbb /= -12.0;
        Pbbb /= -20.0;
        Pab /= 24.0;
        Paab /= 60.0;
        Pabb /= -60.0;
        
        //============================================================================//
        
        k1 = 1 / norm[C]; k2 = k1 * k1; k3 = k2 * k1; k4 = k3 * k1;
        
        Fa = k1 * Pa;
        Fb = k1 * Pb;
        Fc = -k2 * (norm[A]*Pa + norm[B]*Pb + w*P1);
        
        Faa = k1 * Paa;
        Fbb = k1 * Pbb;
        Fcc = k3 * (SQR(norm[A])*Paa + 2*norm[A]*norm[B]*Pab + SQR(norm[B])*Pbb
            + w*(2*(norm[A]*Pa + norm[B]*Pb) + w*P1));
        
        Faaa = k1 * Paaa;
        Fbbb = k1 * Pbbb;
        Fccc = -k4 * (CUBE(norm[A])*Paaa + 3*SQR(norm[A])*norm[B]*Paab
            + 3*norm[A]*SQR(norm[B])*Pabb + CUBE(norm[B])*Pbbb
            + 3*w*(SQR(norm[A])*Paa + 2*norm[A]*norm[B]*Pab + SQR(norm[B])*Pbb)
            + w*w*(3*(norm[A]*Pa + norm[B]*Pb) + w*P1));
        
        Faab = k1 * Paab;
        Fbbc = -k2 * (norm[A]*Pabb + norm[B]*Pbbb + w*Pbb);
        Fcca = k3 * (SQR(norm[A])*Paaa + 2*norm[A]*norm[B]*Paab + SQR(norm[B])*Pabb
            + w*(2*(norm[A]*Paa + norm[B]*Pab) + w*Pa));
        
        //============================================================================//
        
        T0 += norm[X] * ((A == X) ? Fa : ((B == X) ? Fb : Fc));
        
        T1[A] += norm[A] * Faa;
        T1[B] += norm[B] * Fbb;
        T1[C] += norm[C] * Fcc;
        T2[A] += norm[A] * Faaa;
        T2[B] += norm[B] * Fbbb;
        T2[C] += norm[C] * Fccc;
        TP[A] += norm[A] * Faab;
        TP[B] += norm[B] * Fbbc;
        TP[C] += norm[C] * Fcca;
    }
    
    T1[X] /= 2; T1[Y] /= 2; T1[Z] /= 2;
    T2[X] /= 3; T2[Y] /= 3; T2[Z] /= 3;
    TP[X] /= 2; TP[Y] /= 2; TP[Z] /= 2;
    
    mass = density * T0;
    
    // compute center of mass
    r[X] = T1[X] / T0;
    r[Y] = T1[Y] / T0;
    r[Z] = T1[Z] / T0;
    
    // compute inertia tensor
    J[X][X] = density * (T2[Y] + T2[Z]);
    J[Y][Y] = density * (T2[Z] + T2[X]);
    J[Z][Z] = density * (T2[X] + T2[Y]);
    J[X][Y] = J[Y][X] = - density * TP[X];
    J[Y][Z] = J[Z][Y] = - density * TP[Y];
    J[Z][X] = J[X][Z] = - density * TP[Z];
    
    // translate inertia tensor to center of mass
    J[X][X] -= mass * (r[Y]*r[Y] + r[Z]*r[Z]);
    J[Y][Y] -= mass * (r[Z]*r[Z] + r[X]*r[X]);
    J[Z][Z] -= mass * (r[X]*r[X] + r[Y]*r[Y]);
    J[X][Y] = J[Y][X] += mass * r[X] * r[Y];
    J[Y][Z] = J[Z][Y] += mass * r[Y] * r[Z];
    J[Z][X] = J[X][Z] += mass * r[Z] * r[X];
    //==============================================================================//
    
    cgx = r[X];
    cgy = r[Y];
    cgz = r[Z];
    
    I11 = J[X][X]; I12 = J[X][Y]; I13 = J[X][Z];
    
    I22 = J[Y][Y]; I23 = J[Y][Z];
    
    I33 = J[Z][Z];
    
}


//===========================================================================
/*!
    Set a custom mass for the body.

    \fn     cODEMesh::setMass(dReal mass)
*/
//===========================================================================
void cODEMesh::setMass(float a_mass)
{
  dMassAdjust(&m_odeMass, a_mass);
  dBodySetMass(m_odeBody,&m_odeMass);
}

  
//===========================================================================
/*!
    Create a ball linkage for the body.

    \fn     cODEMesh::ballLink(string id,cODEMesh *meshLinked, cVector3d &anchor)
*/
//===========================================================================
void cODEMesh::ballLink     (string id,cODEMesh *meshLinked, cVector3d &anchor)
{
    m_Joint[id] = dJointCreateBall(m_odeWorld,0);
    dJointAttach(m_Joint[id],m_odeBody,meshLinked->m_odeBody);
    dJointSetBallAnchor(m_Joint[id],anchor.x, anchor.y, anchor.z);
}


//===========================================================================
/*!
    Create a hinged linkage for the body.

    \fn     cODEMesh::hingeLink(string id,cODEMesh *meshLinked, cVector3d &anchor, 
    cVector3d &axis)
*/
//===========================================================================
void cODEMesh::hingeLink(string id,cODEMesh *meshLinked, cVector3d &anchor, 
  cVector3d &axis)
{
    m_Joint[id] = dJointCreateHinge(m_odeWorld,0);
    dJointAttach(m_Joint[id],m_odeBody,meshLinked->m_odeBody);
    dJointSetHingeAnchor(m_Joint[id],anchor.x, anchor.y, anchor.z);
    dJointSetHingeAxis(m_Joint[id],axis.x, axis.y, axis.z);
}


//===========================================================================
/*!
    Create a hinged linkage for the body.

    \fn     cODEMesh::hinge2Link   (string id,cODEMesh *meshLinked, cVector3d &anchor, 
    cVector3d &axis1, cVector3d &axis2)
*/
//===========================================================================
void cODEMesh::hinge2Link   (string id,cODEMesh *meshLinked, cVector3d &anchor, 
  cVector3d &axis1, cVector3d &axis2)
{
    m_Joint[id] = dJointCreateHinge2(m_odeWorld,0);
    dJointAttach(m_Joint[id],m_odeBody,meshLinked->m_odeBody);
    dJointSetHinge2Anchor(m_Joint[id],anchor.x, anchor.y, anchor.z);
    dJointSetHinge2Axis1(m_Joint[id],axis1.x, axis1.y, axis1.z);
    dJointSetHinge2Axis2(m_Joint[id],axis2.x, axis2.y, axis2.z);
}


//===========================================================================
/*!
    Create a slider linkage for the body.

    \fn     cODEMesh::sliderLink(string id,cODEMesh *meshLinked, 
    cVector3d &anchor, cVector3d &axis)
*/
//===========================================================================
void cODEMesh::sliderLink(string id,cODEMesh *meshLinked, cVector3d &anchor, 
  cVector3d &axis)
{
    m_Joint[id] = dJointCreateSlider(m_odeWorld,0);
    dJointAttach(m_Joint[id],m_odeBody,meshLinked->m_odeBody);
    dJointSetSliderAxis(m_Joint[id],axis.x, axis.y, axis.z);
}


//===========================================================================
/*!
    Create a universal linkage for the body.

    \fn     cODEMesh::universalLink(string id,cODEMesh *meshLinked, 
    cVector3d &anchor, cVector3d &axis1, cVector3d &axis2)
*/
//===========================================================================
void cODEMesh::universalLink(string id,cODEMesh *meshLinked, cVector3d &anchor, 
  cVector3d &axis1, cVector3d &axis2)
{
    m_Joint[id] = dJointCreateUniversal(m_odeWorld,0);
    dJointAttach(m_Joint[id],m_odeBody,meshLinked->m_odeBody);
    dJointSetUniversalAnchor(m_Joint[id],anchor.x, anchor.y, anchor.z);
    dJointSetUniversalAxis1(m_Joint[id],axis1.x, axis1.y, axis1.z);
    dJointSetUniversalAxis2(m_Joint[id],axis2.x, axis2.y, axis2.z);
}


//===========================================================================
/*!
    Create a fixed linkage for the body.

    \fn     ccODEMesh::fixedLink    (string id,cODEMesh *meshLinked)
*/
//===========================================================================
void cODEMesh::fixedLink    (string id,cODEMesh *meshLinked)
{
    m_Joint[id] = dJointCreateFixed(m_odeWorld,0);
    dJointAttach(m_Joint[id],m_odeBody,meshLinked->m_odeBody);
    dJointSetFixed(m_Joint[id]);
}


//===========================================================================
/*!
    Destroy a joint in the body.

    \fn     cODEMesh::destroyJoint(string id)
*/
//===========================================================================
bool cODEMesh::destroyJoint(string id)
{  
    std::map<string,dJointID>::iterator cur = m_Joint.find(id);

    if (cur == m_Joint.end()) 
      return false;
    else {
      dJointDestroy(m_Joint[(*cur).first]);
      m_Joint.erase(cur);
      return true;
    }
}


//===========================================================================
/*!
    Get one of the body's joints.

    \fn     cODEMesh::getJoint(string id, dJointID* &pJoint)
*/
//===========================================================================
bool cODEMesh::getJoint(string id, dJointID* &pJoint)
{  
    std::map<string,dJointID>::iterator cur = m_Joint.find(id);
    pJoint = NULL;

    if (cur == m_Joint.end())
    {
      return false;
    }
    else
    {
      pJoint = &m_Joint[(*cur).first];
      return true;
    }
}


//===========================================================================
/*!
    Translate each mesh making up our mesh.

    \fn     cODEMesh::translateMesh(float x, float y, float z)
*/
//===========================================================================
void cODEMesh::translateMesh(float x, float y, float z) 
{
  
    for (int submesh_num = 0; submesh_num<this->getNumChildren(); submesh_num++)
    {
        cMesh * submesh = (cMesh*)this->getChild(submesh_num);
        unsigned int vertexcount = submesh->getNumVertices(true);

        if ( (x != 0) && (y != 0) && (z != 0) ) 
        { 

            for (unsigned int i = 0;i<vertexcount; i++) 
            {
                cVertex *vertex = submesh->getVertex(i);
                cVector3d pos = vertex->getPos();
                pos.sub(cVector3d(x, y, z));
                vertex->setPos(pos);
            }
        }
    }
}

