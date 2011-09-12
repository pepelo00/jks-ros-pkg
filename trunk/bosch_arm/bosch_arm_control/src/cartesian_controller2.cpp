#include <sys/time.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>

#include "ros/ros.h"
#include "bosch_arm.h"
#include <kdl/frames.hpp>
#include "cartesian_controller2.h"
#include <bosch_arm_control/ArmState.h>
#include <bosch_arm_control/PointCmd.h>
#include <bosch_arm_control/Diagnostic.h>
#include "cc2.h"
#include <Eigen/Core>
#include<Eigen/Array>
#include <Eigen/LU>
#include <Eigen/QR>
#include <action_cart.h>

using namespace std;
void CartesianController::singlePtCmdCallBack(const bosch_arm_control::PointCmd& msg)
{
  MoveAbsAction* a=new MoveAbsAction(msg.coordinates,this,msg.duration);
  act_que.push_back(a);
}


CartesianController::CartesianController(BoschArm *ptr)
{
  rob=ptr;
  diagnostic_pub =  n.advertise<bosch_arm_control::Diagnostic> ("/diagnostics",100);
  sigle_point_cmd_sub = n.subscribe("/joint_point_cmd",3, &CartesianController::singlePtCmdCallBack, this);
  joint_state_pub =  n.advertise<bosch_arm_control::ArmState> ("/joint_states",100);
  js.position.resize(3);
  js.velocity.resize(3);
  js.effort.resize(3);
  Kp[0]=10;
  Kp[1]=10;
  Kp[2]=10;
  
  Kc[0]=0.3;
  Kc[1]=0.3;
  Kc[2]=0.3;
  Kc[3]=0.3;
//    for(int i=0;i<4;i++)
//      Kp[i]=Kp[i]*0.02;
  Kv[0]=0.1;
  Kv[1]=0.1;
  Kv[2]=0.1;
//   for(int i=0;i<4;i++)
//     Kv[i]=0;
  joint_limit_low[0]=-1.7;
  joint_limit_high[0]=2.1;
  joint_limit_low[1]=-0.6;
  joint_limit_high[1]=0.55;
  joint_limit_low[2]=-2.5;
  joint_limit_high[2]=2.3;
  joint_limit_low[3]=-2.3;
  joint_limit_high[3]=2.4;
  cur_act=NULL;
  start_semi=false;
  end_semi=false;

}

//calculate the frames given the joint values.
void CartesianController::updateKinematics()
{
  frm_cur[0]=frm_home[0];
  frm0[0]=frm_home[0];
  for (int i=1;i<=4;i++)
  {
    frm_cur[i].p=frm_home[i].p;
    frm_cur[i].M=frm_home[i].M*Rotation::RotZ(q[i-1]);
    frm0[i]=frm0[i-1]*frm_cur[i];
  }
  frm_cur[5]=frm_home[5];
  frm0[5]=frm0[4]*frm_cur[5];
  frmn[4]=frm_cur[5];
  Vector z(0,0,1);

  //jacobian[3](joint 4) = z4*p54
  for (int i=3;i>=0;i--)
  {
    frmn[i]=frm_cur[i+1]*frmn[i+1];
    jacobian[i]=z*frmn[i+1].p;
    jacobian[i]=frm0[i+1].M*jacobian[i];
  }
  Eigen::MatrixXf jcb(3,4);
  for (int i=0;i<3;i++)
    for (int j=0;j<4;j++)
      jcb(i,j)=jacobian[j][i];

  Eigen::Matrix3f mm=jcb*jcb.transpose();
  //cout<<mm<<endl;
  Eigen::Matrix3f inv=mm.inverse();
  //cout<<inv<<endl;
  nullspace=Eigen::Matrix4f::Identity()-jcb.transpose()*inv*jcb;
  //cout<<nullspace<<endl;
//
//   for(int i=0;i<3;i++)
//     for(int j=0;j<3;j++)
//     {
//       mm(i,j)=0;
//       for(int k=0;k<4;k++)
//         mm(i,j)=mm(i,j)+jacobian[k][i]*jacobian[k][j];
//     }
//   mm=mm.inverse();

}

void CartesianController::start()
{
  rob->initialize();
  //L0,L3 are not available until rob is initialized.
  for(int i=0;i<4;i++)
    link[i]=Vector(constants::link[i*2],constants::link[i*2+1],0);
  frm_home[0]=Frame::Identity();
  Rotation r(0,-1,0,
             0,0,-1,
             1,0,0);
  frm_home[1].M=r;
  frm_home[1].p=Vector(0,0,rob->L0);
  frm_home[2].M=r;
  frm_home[3].M=r;
  frm_home[4].M=r;
  frm_home[4].p=Vector(-rob->L5,0,rob->L3);
  frm_home[5].p=Vector(rob->L4,0,0);

  rob->motor2JointPosition(rob->q,q);
  updateKinematics();
  for (int i=0;i<3;i++)
  {
    x[i]=frm0[5].p[i];
    v[i]=0;
    dx[i]=0;
    f[i]=0;
    xl[i]=x[i];
    xd[i]=x[i];
    vd[i]=v[i];
  }
  //state=CALIBRATION;
  state=SEMIAUTO;
  //state=LOADTRACE;
  ctr_mode=GRAVITY;
  
  
  
  
  for (int i=0;i<4;i++)
      gc[i]=Vector(constants::gravc[i*2],constants::gravc[i*2+1],0);
  
  if (state==CALIBRATION)
  {
    genCalibTraj2();
    cur_act=act_que.front();
    cur_act->initialize();
    act_que.pop_front();
    cout<<"start in calibration"<<endl;
  }
  else if(state==SEMIAUTO)
  {
    vector<double> des(3,0);
  cali1_act=new MoveAbsAction(des,this);
  calib1=new Calibration("calibsemi");
  cali1_act->calib=calib1;
  act_que.push_back(cali1_act);
  cur_act=act_que.front();
  cur_act->initialize();
    act_que.pop_front();
    cout<<"start in semi auto"<<endl;
  }
  else if (state==LOADTRACE)
  {
//     for (int i=0;i<4;i++)
//       gc[i]=Vector(constants::gravc[i*2],constants::gravc[i*2+1],0);
//     vector<double> des(3,0);
//     des[0]=0.5;
//     des[1]=0;
//     des[2]=0.3;
//     act_que.push_back(new MoveAbsAction(des,this));
//     des[0]=0;
//     des[1]=0;
//     des[2]=1.2;
//     act_que.push_back(new MoveAbsAction(des,this));
//     genCalibTraj2();
//     cur_act=act_que.front();
//     cur_act->initialize();
//     act_que.pop_front();

    //state=SERVO;

  }
}

// void CartesianController::genCalibTraj2()
// {
//   
//   vector<double> des(3,0);
// //  int t=8000;
//   //move the the zero position.
//   des[0]=0.30;
//   des[1]=-0.5;
//   des[2]=0.10;
//   act_que.push_back(new MoveAbsAction(des,this));
//   des[0]=0.19;
//   des[1]=-0.18;
//   des[2]=-0.05;
//   act_que.push_back(new MoveAbsAction(des,this));
//   des[0]=0.54;
//   des[1]=-0.18;
//   des[2]=-0.05;
//   cali1_act=new MoveAbsAction(des,this);
//   calib1=new Calibration("calib1");
//   cali1_act->calib=calib1;
//   act_que.push_back(cali1_act);
//   des[0]=0.54;
//   des[1]=0.07;
//   des[2]=-0.05;
//   cali2_act=new MoveAbsAction(des,this);
//   calib2=new Calibration("calib2");
//   cali2_act->calib=calib2;
//   act_que.push_back(cali2_act);
//   
// 
// }

void CartesianController::genCalibTraj2()
{
  
  vector<double> des(3,0);
//  int t=8000;
  //move the the zero position.
  des[0]=0.55;
  des[1]=-0.55;
  des[2]=-0.05;
  act_que.push_back(new MoveAbsAction(des,this));
  des[0]=0.55;
  des[1]=-0.22;
  des[2]=0;
  cali1_act=new MoveAbsAction(des,this);
  calib1=new Calibration("calib1");
  cali1_act->calib=calib1;
  act_que.push_back(cali1_act);
  des[0]=0.14;
  des[1]=-0.20;
  des[2]=-0.05;
  cali2_act=new MoveAbsAction(des,this);
  calib2=new Calibration("calib2");
  cali2_act->calib=calib2;
  act_que.push_back(cali2_act);
  des[0]=0.55;
  des[1]=-0.55;
  des[2]=-0.08;
  cali3_act=new MoveAbsAction(des,this);
  calib3=new Calibration("calib3");
  cali3_act->calib=calib3;
  act_que.push_back(cali3_act);

}


void CartesianController::calcLinkLength()
{

  int bin=calib1->bin;
  double* A[8];
  for(int i=0;i<8;i++)
    A[i]=new double[bin];
  double* b;
  b=new double[bin];
  
  for(int tt=0;tt<calib1->bin;tt++)
  {
    for(int i=0;i<8;i++)
      A[i][tt]=calib1->A[i][tt];
    b[tt]=calib1->b[tt];
  }
  
  
  Eigen::MatrixXf AA(8,8);
  for(int i=0;i<8;i++)
  {
    for(int j=0;j<8;j++)
    {
      double s=0;
      for(int k=0;k<bin;k++)   
        s+=A[i][k]*A[j][k];
      AA(i,j)=s;
    }
  }
  cout<<AA<<endl;
  Eigen::VectorXf Ab(8);
  for(int i=0;i<8;i++)
  {
    double s=0;
    for(int j=0;j<bin;j++)
      s+=A[i][j]*b[j];
    Ab(i)=s;
  }
  cout<<Ab<<endl;
  Eigen::MatrixXf AV=AA.inverse();
  cout<<AV<<endl;
  Eigen::MatrixXf M=AV*Ab;
  //cout<<M<<endl;
  for(int i=0;i<4;i++)
  {
    link[i]=Vector(M(i*2,0),M(i*2+1,0),0);
  }
  for(int i=0;i<4;i++)
  {
    for(int j=0;j<3;j++)
      cout<<link[i][j]<<',';
    cout<<endl;
  }
}

void CartesianController::update()
{
  rob->update();
  clock_gettime(CLOCK_REALTIME, &ts);
  //save the last joint position
//   for (int i=0;i<4;i++)
//     ql[i]=q[i];
  rob->motor2JointPosition(rob->q,q);
  rob->motor2JointVelocity(rob->v,qv);
  updateKinematics();
  for (int i=0;i<3;i++)
  {
    x[i]=frm0[5].p[i];
    v[i]=0;
    for (int j=0;j<4;j++)
      v[i]+=jacobian[j][i]*qv[j];
  }
  if (state==CALIBRATION)
  {
    //update desired position
    if (cur_act->finished)
    {
      if (cur_act->calib==NULL)
        delete cur_act;

      if (act_que.size()==0)
      {
        //calcLinkLength();
        state=SERVO;
        return;
      }
      cur_act=act_que.front();
      //cout<<cur_act->rel[3]<<endl;
      cur_act->initialize();
      act_que.pop_front();
    }
    cur_act->update();
    for (int i=0;i<3;i++)
    {
      xd[i]=cur_act->cmd.coordinates[i];
      vd[i]=cur_act->cmd.velocity[i];

    }
    //update torque command
    PDWithGC();

    logging();
  }
  else if (state==SEMIAUTO)
  {
    if(start_semi==true)
      cur_act->update2();
    if(end_semi==true)
    {
      state=SERVO;
      cur_act->calib->finish();
      calcLinkLength();
    }
    gravity_compensation();
    logging();
  }
  else if (state==LOADTRACE)
  {
    if (cur_act==NULL&&act_que.size()==0)
    {
      state=SERVO;
      return;
    }
    if (cur_act->finished)
    {
      if (cur_act->calib==NULL)
        delete cur_act;

      if (act_que.size()==0)
      {
        //calcGravityCompensation();
        state=SERVO;
        return;
      }

      cur_act=act_que.front();
      //cout<<cur_act->rel[3]<<endl;
      cur_act->initialize();
      act_que.pop_front();

    }
    cur_act->update();
    for (int i=0;i<3;i++)
    {
      xd[i]=cur_act->cmd.coordinates[i];
      vd[i]=cur_act->cmd.velocity[i];

    }
    //update torque command
    PDWithGC();
    //PDControl();
    logging();
  }
  else if (state==SERVO)
  {
    //PDControl();
    if (ctr_mode==PDWITHGC)
      PDWithGC();
    else if (ctr_mode==PDCONTROL)
      PDControl();
    else if (ctr_mode==GRAVITY)
      gravity_compensation();
    else if (ctr_mode==NOCONTROL)
    {
      for (int i=0;i<4;i++)
        rob->torque[i]=0;
    }
    //floating();
    //gravity_compensation();
    logging();
  }
}

void CartesianController::gravity_compensation()
{

  updateKinematics();
  //T=sum(gcj) dot (g*Zi
  Vector g(0,0,-1);
  //double tq[4];
  for (int i=0;i<4;i++)
  {
    Vector Zi=frm0[i+1].M.UnitZ();
    tq[i]=0;
    for (int j=i;j<4;j++)
    {
      Rotation R0j=frm0[j+1].M.Inverse();
      Vector tmp=R0j*(g*Zi);
      tq[i]-=dot(gc[j],tmp);
    }
  }

  for (int i=0;i<4;i++)
  {
    tm[i]=0;
    for (int j=0;j<4;j++)
    {
      tm[i]+=rob->m2j[4*j+i]*tq[j];

    }

  }
  for (int i=0;i<4;i++)
    rob->torque[i]=tm[i];
  //logging();

}

void CartesianController::PDWithGC()
{
  updateKinematics();
  for (int i=0;i<3;i++)
  {

    dx[i]=xd[i]-x[i];
    f[i]=Kp[i]*dx[i]-Kv[i]*(v[i]-vd[i]);
  }
  for (int i=0;i<4;i++)
  {
    tq[i]=0;
    for (int j=0;j<3;j++)
    {
      tq[i]+=jacobian[i][j]*f[j];
      //cout<<jacobian[4*j+i]<<',';
    }
    // cout<<endl;
  }
  //T=sum(gcj) dot (g*Zi
  Vector g(0,0,-1);
  //double tq[4];
  for (int i=0;i<4;i++)
  {
    Vector Zi=frm0[i+1].M.UnitZ();
    for (int j=i;j<4;j++)
    {
      Rotation R0j=frm0[j+1].M.Inverse();
      Vector tmp=R0j*(g*Zi);
      tq[i]-=dot(gc[j],tmp);
    }
  }

  //away from joint limits.
  Eigen::Vector4f c;
  for (int i=0;i<4;i++)
    c(i)=-q[i];
  Eigen::Vector4f cf=nullspace*c;
  for (int i=0;i<4;i++)
    tq[i]+=Kc[i]*cf(i);

  for (int i=0;i<4;i++)
  {
    tm[i]=0;
    for (int j=0;j<4;j++)
    {
      tm[i]+=rob->m2j[4*j+i]*tq[j];

    }

  }
  for (int i=0;i<4;i++)
    rob->torque[i]=tm[i];
  //logging();
}


void CartesianController::PDControl()
{
  for (int i=0;i<3;i++)
  {

    dx[i]=xd[i]-x[i];
    f[i]=Kp[i]*dx[i]-Kv[i]*(v[i]-vd[i]);
  }
  for (int i=0;i<4;i++)
  {
    tq[i]=0;
    for (int j=0;j<3;j++)
    {
      tq[i]+=jacobian[i][j]*f[j];
      //cout<<jacobian[4*j+i]<<',';
    }
    // cout<<endl;
  }
  for (int i=0;i<4;i++)
  {
    tm[i]=0;
    for (int j=0;j<4;j++)
    {
      tm[i]+=rob->m2j[4*j+i]*tq[j];

    }

  }
  for (int i=0;i<4;i++)
    rob->torque[i]=tm[i];
  //logging();
}


void CartesianController::logging()
{
  std::string log;
  std::stringstream out;
  for (int i=0;i<3;i++)
    out<<x[i]<<",";
  for (int i=0;i<3;i++)
    out<<xd[i]<<",";
  for (int i=0;i<3;i++)
    out<<v[i]<<",";
  for (int i=0;i<3;i++)
    out<<f[i]<<",";
  for (int i=0;i<3;i++)
    out<<(x[i]-xd[i])<<",";
  for (int i=0;i<4;i++)
    out<<q[i]<<',';
  for (int i=0;i<4;i++)
    out<<tq[i]<<',';
  for (int i=0;i<4;i++)
    out<<rob->q[i]<<',';
  for (int i=0;i<4;i++)
    out<<rob->v[i]<<',';
  for (int i=0;i<4;i++)
    out<<rob->torque[i]<<',';
  out << ts.tv_sec << ','<< ts.tv_nsec;
  log = out.str();
  diag.data=log;
  diag.header.stamp.sec=ts.tv_sec;
  diag.header.stamp.nsec=ts.tv_nsec;
  diagnostic_pub.publish(diag);
  //vector<double> cur_pos_act(q,q+4);
  js.header.stamp.sec=ts.tv_sec;
  js.header.stamp.nsec=ts.tv_nsec;
  //js.position=cur_pos_act;

  for (int i=0;i<3;i++)
  {
    js.position[i]=x[i];
    js.velocity[i]=v[i];
    js.effort[i]=f[i];
  }
  joint_state_pub.publish(js);
}
void CartesianController::stop()
{
  rob->close();
}