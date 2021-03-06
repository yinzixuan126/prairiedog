/*  Copyright Michael Otte, University of Colorado, 9-9-2009
 *
 *  This file is part of goal_server_cu.
 *
 *  goal_server_cu is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  goal_server_cu is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with goal_server_cu. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 *  If you require a different license, contact Michael Otte at
 *  michael.otte@colorado.edu
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/types.h>
#include <time.h>
#include <math.h>
#include <list>
#include <sstream>

#include <ros/ros.h>

#include "geometry_msgs/PoseArray.h"
#include "geometry_msgs/Pose2D.h"
#include "geometry_msgs/PoseStamped.h"
#include "geometry_msgs/PolygonStamped.h"
#include "geometry_msgs/PoseWithCovarianceStamped.h"

#include "nav_msgs/Path.h"
#include "nav_msgs/GetMap.h"
#include "nav_msgs/GridCells.h"

#include "localization_cu/GetPose.h"

#ifndef PI
  #define PI 3.1415926535897
#endif
           
struct POSE;
typedef struct POSE POSE;
 
// globals that can be reset using parameter server, see main();
float goal_pose_x_init = 0; 
float goal_pose_y_init = 0;
float goal_pose_z_init = 0; // note z coord is basically unused 
float goal_pose_theta_init = 0;

// publisher handles
ros::Publisher goal_pub;

// subscriber handles
ros::Subscriber reset_goal_sub;

// global ROS provide service server handles
ros::ServiceServer get_goal_srv;

// globals
POSE* goal = NULL; // the current goal

/* ----------------------- POSE -----------------------------------------*/
struct POSE
{
    float x;
    float y;
    float z;
    
    float qw;
    float qx;
    float qy;
    float qz; 
    
    float alpha; // rotation in 2D plane
    
    float cos_alpha;
    float sin_alpha;
};


// this creates and returns a pointer to a POSE struct
POSE* make_pose(float x, float y, float z, float alpha)
{
  POSE* pose = (POSE*)calloc(1, sizeof(POSE));
  pose->x = x;
  pose->y = y;
  pose->z = z;
  pose->alpha = alpha;

  float r = sqrt(2-(2*cos(pose->alpha)));

  if(r == 0)
    pose->qw = 1;
  else
    pose->qw = sin(pose->alpha)/r; 

  pose->qx = 0;
  pose->qy = 0;
  pose->qz = r/2; 

  pose->cos_alpha = cos(pose->alpha);
  pose->sin_alpha = sin(pose->alpha);
  
  return pose;
}


// this deallocates all required memory for a POSE
void destroy_pose(POSE* pose)
{
  if(pose != NULL)
    free(pose);
}

// prints pose on command line
void print_pose(POSE* pose)
{
  if(pose != NULL)
  {
    printf("\n");  
    printf("x: %f, y: %f, z: %f \n",pose->x, pose->y, pose->z);  
    printf("qw: %f, qx: %f, qy: %f, qz: %f \n", pose->qw, pose->qx, pose->qy, pose->qz);  
    printf("\n");  
  } 
}


/*------------------------ ROS Callbacks --------------------------------*/
void reset_goal_callback(const geometry_msgs::PoseStamped::ConstPtr& msg)
{    
  if(strcmp(msg->header.frame_id.c_str(),"/map_cu") != 0)
  {
    printf("goal_server recieved a new goal pose with the wrong frame: %s\n", msg->header.frame_id.c_str()); 
    return;
  }
   
  if(goal == NULL)
    goal = make_pose(0, 0, 0, 0);
  
  goal->x = msg->pose.position.x;
  goal->y = msg->pose.position.y;
  goal->z = msg->pose.position.z;
  goal->qw = msg->pose.orientation.w;
  goal->qx = msg->pose.orientation.x;
  goal->qy = msg->pose.orientation.y;
  goal->qz = msg->pose.orientation.z; 
  
}


/*-------------------------- ROS Publisher functions --------------------*/
void publish_goal()
{ 
  if(goal == NULL)
  {
    printf("goal_server: goal is null \n");
    return;
  }
  
  //printf("goal_server: publishing goal \n");
      
  geometry_msgs::PoseStamped msg;
  msg.header.frame_id = "/map_cu";
  msg.pose.position.x = goal->x;
  msg.pose.position.y = goal->y;
  msg.pose.position.z = goal->z;  
  msg.pose.orientation.w = goal->qw; 
  msg.pose.orientation.x = goal->qx;
  msg.pose.orientation.y = goal->qy;
  msg.pose.orientation.z = goal->qz; 
  goal_pub.publish(msg);
  
  //printf(" published pose %d\n",num_its++);
  //print_pose(posterior_pose);
}

// service that provides goal
bool get_goal_callback(localization_cu::GetPose::Request &req, localization_cu::GetPose::Response &resp)
{  
  if(goal == NULL)
    return false;
  
  geometry_msgs::PoseStamped msg;
  resp.pose.header.frame_id = "/map_cu";
  resp.pose.pose.position.x = goal->x;
  resp.pose.pose.position.y = goal->y;
  resp.pose.pose.position.z = goal->z;  
  resp.pose.pose.orientation.w = goal->qw; 
  resp.pose.pose.orientation.x = goal->qx;
  resp.pose.pose.orientation.y = goal->qy;
  resp.pose.pose.orientation.z = goal->qz; 
  
  //printf(" published goal via service\n");
  //print_pose(goal);
  
  return true;
}

int main(int argc, char** argv) 
{
    ros::init(argc, argv, "goal_server_cu");
    ros::NodeHandle nh;
    ros::Rate loop_rate(100);
    
     // load globals from parameter server
    double param_input;
    bool given_goal = false;
    if(ros::param::get("goal_server_cu/goal_pose_x_init", param_input)) 
    {
      goal_pose_x_init = (float)param_input;
      given_goal = true;
    }
    if(ros::param::get("goal_server_cu/goal_pose_y_init", param_input)) 
    {
      goal_pose_y_init = (float)param_input;
      given_goal = true;
    }
    if(ros::param::get("goal_server_cu/goal_pose_z_init", param_input)) 
    {
      goal_pose_z_init = (float)param_input;                                 // z is basically unused
      given_goal = true;
    }
    if(ros::param::get("goal_server_cu/goal_pose_theta_init", param_input)) 
    {
      goal_pose_theta_init = (float)param_input;
      given_goal = true;
    }
    
    // if the goal was sent in, then init goal pose
    if(given_goal)
    {
      printf("goal recieved at startup: [%f %f %f] \n", goal_pose_x_init, goal_pose_y_init, goal_pose_theta_init);
      goal = make_pose(goal_pose_x_init, goal_pose_y_init, goal_pose_z_init, goal_pose_theta_init);
    }
    
    // set up publisher
    goal_pub = nh.advertise<geometry_msgs::PoseStamped>("/cu/goal_cu", 1);
    
    // set up subscribers
    reset_goal_sub = nh.subscribe("/cu/reset_goal_cu", 1, reset_goal_callback);
    
    // set up service servers
    get_goal_srv = nh.advertiseService("/cu/get_goal_cu", get_goal_callback);
    
    while (ros::ok()) 
    {
      //printf(" This is the goal server \n");
      //print_pose(goal);
        
      publish_goal();
        
      ros::spinOnce();
      loop_rate.sleep();
    }
    
    
    // destroy publisher
    goal_pub.shutdown();
    reset_goal_sub.shutdown();
    get_goal_srv.shutdown();
        
    destroy_pose(goal);
}
