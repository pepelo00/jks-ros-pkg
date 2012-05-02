/*
 * Copyright (c) 2012, Willow Garage, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Willow Garage, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <QApplication>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMenuBar>

#include <assisted_teleop/assisted_teleop.h>
#include <moveit_visualization_ros/primitive_object_addition_dialog.h>
#include <moveit_visualization_ros/mesh_object_addition_dialog.h>

static const std::string VIS_TOPIC_NAME = "planning_components_visualization";

namespace assisted_teleop {
  using namespace moveit_visualization_ros;

AssistedTeleop::AssistedTeleop() :
  first_update_(false),
  allow_trajectory_execution_(false)
{
  ros::NodeHandle nh;
  ros::NodeHandle loc_nh("~");

  bool monitor_robot_state = false;
  loc_nh.param("monitor_robot_state", monitor_robot_state, false);

  interactive_marker_server_.reset(new interactive_markers::InteractiveMarkerServer("interactive_kinematics_visualization", "", false));

  if(!monitor_robot_state) {
    ROS_INFO_STREAM("Starting publisher thread");
    joint_state_publisher_.reset(new KinematicStateJointStatePublisher());
    planning_scene_monitor_.reset(new planning_scene_monitor::PlanningSceneMonitor("robot_description"));
    boost::thread publisher_thread(boost::bind(&AssistedTeleop::publisherFunction, this, true));
  } else {
    transformer_.reset(new tf::TransformListener());
    planning_scene_monitor_.reset(new planning_scene_monitor::PlanningSceneMonitor("robot_description", transformer_.get()));
    joint_state_publisher_.reset(new KinematicStateJointStatePublisher());
    bool publish_root_transform = false;
    loc_nh.param("publish_root_transform", publish_root_transform, false);
    if(publish_root_transform) {
      boost::thread publisher_thread(boost::bind(&AssistedTeleop::publisherFunction, this, false));
    }
    planning_scene_monitor_->startStateMonitor();
  }

   if(monitor_robot_state) {
    loc_nh.param("allow_trajectory_execution", allow_trajectory_execution_, false);
    if(allow_trajectory_execution_) {
      bool manage_controllers= false;
      loc_nh.param("manage_controllers", manage_controllers, true);
      trajectory_execution_monitor_.reset(new trajectory_execution_ros::TrajectoryExecutionMonitorRos(planning_scene_monitor_->getPlanningScene()->getKinematicModel(), manage_controllers));
    }
  }

  vis_marker_publisher_ = nh.advertise<visualization_msgs::Marker> (VIS_TOPIC_NAME, 128);
  vis_marker_array_publisher_ = nh.advertise<visualization_msgs::MarkerArray> (VIS_TOPIC_NAME + "_array", 128);

  std_msgs::ColorRGBA col;
  col.r = col.g = col.b = .5;
  col.a = 1.0;

  std_msgs::ColorRGBA good_color;
  good_color.a = 1.0;
  good_color.g = 1.0;

  std_msgs::ColorRGBA bad_color;
  bad_color.a = 1.0;
  bad_color.r = 1.0;

  kinematics_plugin_loader_.reset(new kinematics_plugin_loader::KinematicsPluginLoader());

  tv_.reset(new TeleopVisualizationQtWrapper(planning_scene_monitor_->getPlanningScene(),
                                               planning_scene_monitor_->getGroupJointLimitsMap(),
                                               interactive_marker_server_,
                                               kinematics_plugin_loader_,
                                               vis_marker_array_publisher_));
/* ael 
*/
  iov_.reset(new InteractiveObjectVisualizationQtWrapper(planning_scene_monitor_->getPlanningScene(),
                                                         interactive_marker_server_,
                                                         col));

  iov_->setUpdateCallback(boost::bind(&AssistedTeleop::updatePlanningScene, this, _1));


  if(monitor_robot_state) {
    tv_->addMenuEntry("Reset start state", boost::bind(&AssistedTeleop::updateToCurrentState, this));
    if(allow_trajectory_execution_) {
      tv_->setAllStartChainModes(false);
      tv_->addMenuEntry("Execute last trajectory", boost::bind(&AssistedTeleop::executeLastTrajectory, this));

      /* ael */
      //tv_->addMenuEntry("Start Following",  boost::bind(&AssistedTeleop::startFollowing, this));
      //tv_->addMenuEntry("Stop Following",   boost::bind(&AssistedTeleop::stopFollowing, this));
      //tv_.setExecutionFunction( boost::bind(&AssistedTeleop::executeTeleopUpdate, this, _1, _2) );
      tv_->setTrajectoryExecutionFunction( boost::bind(&AssistedTeleop::executeLastTrajectory, this) );
    }
  }
  tv_->hideAllGroups();


  rviz_frame_ = new rviz::VisualizationPanel;

  QList<int> sizes;
  sizes.push_back(0);
  sizes.push_back(1000);

  rviz_frame_->setSizes(sizes);

  //kind of hacky way to do this - this just turns on interactive move
  //given the way that the vis manager is creating tools
  rviz_frame_->getManager()->setCurrentTool(rviz_frame_->getManager()->getTool(1));

  std::string display_config_name = ros::package::getPath("assisted_teleop")+"/config/pr2_assisted_teleop_display.config";

  rviz_frame_->loadDisplayConfig(display_config_name);

  main_window_ = new QWidget;
  main_window_->resize(1500,1000);
  //InteractiveObjectVisualizationWidget* iov_widget = new InteractiveObjectVisualizationWidget(main_window_);

/* ael
*/
  PrimitiveObjectAdditionDialog* primitive_object_dialog = new PrimitiveObjectAdditionDialog(main_window_);
  MeshObjectAdditionDialog* mesh_object_dialog = new MeshObjectAdditionDialog(main_window_);


  QHBoxLayout* main_layout = new QHBoxLayout;
  QMenuBar* menu_bar = new QMenuBar(main_window_);
  /* ael
  */
  PlanningSceneFileMenu* planning_scene_file_menu = new PlanningSceneFileMenu(menu_bar);
  QObject::connect(iov_.get(),
                   SIGNAL(updatePlanningSceneSignal(planning_scene::PlanningSceneConstPtr)),
                   planning_scene_file_menu,
                   SLOT(updatePlanningSceneSignalled(planning_scene::PlanningSceneConstPtr)));
  QObject::connect(planning_scene_file_menu->getDatabaseDialog(),
                   SIGNAL(planningSceneLoaded(moveit_msgs::PlanningScenePtr)),
                   iov_.get(),
                   SLOT(loadPlanningSceneSignalled(moveit_msgs::PlanningScenePtr)));
  menu_bar->addMenu(planning_scene_file_menu);


  planning_group_selection_menu_ = new PlanningGroupSelectionMenu(menu_bar);
  QObject::connect(planning_group_selection_menu_,
                   SIGNAL(groupSelected(const QString&)),
                   tv_.get(),
                   SLOT(newGroupSelected(const QString&)));
  planning_group_selection_menu_->init(planning_scene_monitor_->getPlanningScene()->getSrdfModel());
  menu_bar->addMenu(planning_group_selection_menu_);

  coll_object_menu_ = menu_bar->addMenu("Collision Objects");


/* ael
*/
  QAction* show_primitive_objects_dialog = coll_object_menu_->addAction("Add Primitive Collision Object");
  QObject::connect(show_primitive_objects_dialog, SIGNAL(triggered()), primitive_object_dialog, SLOT(show()));
  QAction* show_mesh_objects_dialog = coll_object_menu_->addAction("Add Mesh Collision Object");
  QObject::connect(show_mesh_objects_dialog, SIGNAL(triggered()), mesh_object_dialog, SLOT(show()));


  main_layout->setMenuBar(menu_bar);

  //main_layout->addWidget(iov_widget);
  main_layout->addWidget(rviz_frame_);

  main_window_->setLayout(main_layout);

  //QObject::connect(iov_widget, SIGNAL(addCubeRequested()), iov_.get(), SLOT(addCubeSignalled()));

  /* ael
  */
  QObject::connect(primitive_object_dialog,
                   SIGNAL(addCollisionObjectRequested(const moveit_msgs::CollisionObject&, const QColor&)),
                   iov_.get(),
                   SLOT(addCollisionObjectSignalled(const moveit_msgs::CollisionObject&, const QColor&)));

  QObject::connect(mesh_object_dialog,
                   SIGNAL(addCollisionObjectRequested(const moveit_msgs::CollisionObject&, const QColor&)),
                   iov_.get(),
                   SLOT(addCollisionObjectSignalled(const moveit_msgs::CollisionObject&, const QColor&)));


  main_window_->show();

  planning_scene_monitor_->setUpdateCallback(boost::bind(&AssistedTeleop::updateSceneCallback, this));
}

AssistedTeleop::~AssistedTeleop() {
  iov_.reset();
  tv_.reset();
  if(trajectory_execution_monitor_) {
    trajectory_execution_monitor_->restoreOriginalControllers();
    trajectory_execution_monitor_.reset();
  }
  planning_scene_monitor_.reset();
  delete rviz_frame_;
}

void AssistedTeleop::publisherFunction(bool joint_states) {
  ros::WallRate r(10.0);

  while(ros::ok())
  {
    joint_state_publisher_->broadcastRootTransform(planning_scene_monitor_->getPlanningScene()->getCurrentState());
    if(joint_states) {
      joint_state_publisher_->publishKinematicState(planning_scene_monitor_->getPlanningScene()->getCurrentState());
    }
    r.sleep();
  }
}

void AssistedTeleop::updatePlanningScene(planning_scene::PlanningSceneConstPtr planning_scene) {
  current_diff_ = planning_scene;
  tv_->updatePlanningScene(planning_scene);
}

void AssistedTeleop::updateSceneCallback() {
  if(first_update_) {
    ROS_INFO_STREAM("Got update scene callback");
    tv_->resetAllStartAndGoalStates();
    first_update_ = false;
  }
}

bool AssistedTeleop::doneWithExecution() {
  ROS_INFO_STREAM("Done with trajectory execution.");
  //ROS_INFO_STREAM("^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^");
  return true;
}
void AssistedTeleop::executeLastTrajectory() {
  std::string group_name;
  trajectory_msgs::JointTrajectory traj;
  if(tv_->getLastTrajectory(group_name, traj)) {
    trajectory_execution::TrajectoryExecutionRequest ter;
    ter.group_name_ = group_name;

    ter.trajectory_ = traj;
    ROS_DEBUG_STREAM("Attempting to execute trajectory for group name " << group_name);

    std::vector<trajectory_execution::TrajectoryExecutionRequest> ter_reqs;
    ter_reqs.push_back(ter);

    trajectory_execution_monitor_->executeTrajectories(ter_reqs);
                                                       //boost::bind(&AssistedTeleop::doneWithExecution, this));
  }
}

void AssistedTeleop::updateToCurrentState() {
  iov_->updateCurrentState(planning_scene_monitor_->getPlanningScene()->getCurrentState());
  tv_->resetAllStartStates();
}

//void AssistedTeleop::executeTeleopUpdate(const std::string& group, const trajectory_msgs::JointTrajectory& traj) {
//    trajectory_execution::TrajectoryExecutionRequest ter;
//    ter.group_name_ = group;
//    ter.trajectory_ = traj;
//
//    ROS_DEBUG_STREAM("Attempting to execute trajectory for group name " << group_name);
//
//    std::vector<trajectory_execution::TrajectoryExecutionRequest> ter_reqs;
//    ter_reqs.push_back(ter);
//
//    trajectory_execution_monitor_->executeTrajectories(ter_reqs); //,
//                                                       //boost::bind(&AssistedTeleop::doneWithExecution, this));
//  }
//}

//void AssistedTeleop::startFollowing() {
  //tv_->startFollowing();
//}


//void AssistedTeleop::stopFollowing() {
  //tv_->stopFollowing();
//}

}
