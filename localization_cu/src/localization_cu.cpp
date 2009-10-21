/*  Copyright Michael Otte, University of Colorado, 9-9-2009
 *
 *  This file is part of Localization_CU.
 *
 *  Localization_CU is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Localization_CU is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Localization_CU. If not, see <http://www.gnu.org/licenses/>.
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

#ifndef PI
  #define PI 3.1415926535897
#endif
  
#define MOVEMULT 2 // if pose jumps globally more than this mult by the local movement, global pose is dropped (used to prune error gps data)
   
bool USING_GPS = true; // true if there is gps data available (causes node to wait until GPS is received to broadcast position, gps data includes user defined pose i.e. from visualization node)
        
struct POSE;
typedef struct POSE POSE;
        
// publisher handles
ros::Publisher pose_pub;

// subscriber handles
ros::Subscriber odometer_pose_sub;
ros::Subscriber user_pose_sub;
ros::Subscriber stargazer_pose_sub;

// globals
POSE* posterior_pose = NULL; // our best idea of where we are in the global coordinate system
POSE* odometer_pose = NULL;  // pose as determined by on-board odometry

float local_offset[] = {0, 0, 0}; // x,y,theta
float global_offset[] = {0, 0, 0}; // x,y,theta
float accum_movement = -1;
bool received_gps = false;

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
POSE* make_pose(float x, float y, float z)
{
  POSE* pose = (POSE*)calloc(1, sizeof(POSE));
  pose->x = 0;
  pose->y = 0;
  pose->z = 0;
  pose->alpha = 0;

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

void odometer_pose_callback(const geometry_msgs::Pose2D::ConstPtr& msg)
{       
  if(USING_GPS && !received_gps)
    return;
  
  // remember old posterior x and y
  float old_x = posterior_pose->x;
  float old_y = posterior_pose->y;
  
  // note this assumes robot rotation is only in the 2D plane (and also constant velocity = average velocity between updates)
  odometer_pose->x = msg->x;
  odometer_pose->y = msg->y;
  odometer_pose->z = 0;
  odometer_pose->alpha = msg->theta;
    
  // update global pose based on this local pose
  
  // transform #1, to local coordinate frame where robot was last time we had both global and local pose info
  float xl = odometer_pose->x - local_offset[0];
  float yl = odometer_pose->y - local_offset[1];
  
  // transform #2, to global coordinate frame where robot was last time we had both global and local pose info
  float angle = global_offset[2] - local_offset[2];
  float xg = xl*cos(angle) - yl*sin(angle);
  float yg = xl*sin(angle) + yl*cos(angle);
  
  // transform #3, to global coordinate frame
  posterior_pose->x = xg + global_offset[0];
  posterior_pose->y = yg + global_offset[1];
  posterior_pose->z = 0;
  
  posterior_pose->alpha = odometer_pose->alpha + angle;
  posterior_pose->cos_alpha = cos(posterior_pose->alpha);
  posterior_pose->sin_alpha = sin(posterior_pose->alpha);
  
  float r = sqrt(2-(2*cos(posterior_pose->alpha)));

  if(r == 0)
    posterior_pose->qw = 1;
  else
    posterior_pose->qw = sin(posterior_pose->alpha)/r; 
  
  posterior_pose->qx = 0;
  posterior_pose->qy = 0;
  posterior_pose->qz = r/2; 
  
  // remember accumulated movement
  if(accum_movement == -1)
      accum_movement = 0;
  accum_movement += sqrt((old_x - posterior_pose->x)*(old_x - posterior_pose->x) + (old_y - posterior_pose->y)*(old_y - posterior_pose->y));
}

void user_pose_callback(const geometry_msgs::PoseStamped::ConstPtr& msg)
{    
  if(msg->header.frame_id != "/2Dmap")
    ROS_INFO("received a message that is not for the 2Dmap frame");
      
  posterior_pose->x = msg->pose.position.x;
  posterior_pose->y = msg->pose.position.y;
  posterior_pose->z = msg->pose.position.z;
  posterior_pose->qw = msg->pose.orientation.w;
  posterior_pose->qx = msg->pose.orientation.x;
  posterior_pose->qy = msg->pose.orientation.y;
  posterior_pose->qz = msg->pose.orientation.z; 
  
  float qw = posterior_pose->qw;
  float qx = posterior_pose->qx;
  float qy = posterior_pose->qy;
  float qz = posterior_pose->qz; 
  
  posterior_pose->cos_alpha = qw*qw + qx*qx - qy*qy - qz*qz;
  posterior_pose->sin_alpha = 2*qw*qz + 2*qx*qy; 
  posterior_pose->alpha = atan2(posterior_pose->sin_alpha, posterior_pose->cos_alpha);
  
  // save most recent data from both local and global pose to use in transformation matrices
  
  local_offset[0] = odometer_pose->x;
  local_offset[1] = odometer_pose->y;
  local_offset[2] = odometer_pose->alpha;
  
  global_offset[0] = posterior_pose->x;
  global_offset[1] = posterior_pose->y;
  global_offset[2] = posterior_pose->alpha;
  
  accum_movement = -1;
  received_gps = true;
}

void stargazer_pose_callback(const geometry_msgs::Pose2D::ConstPtr& msg)
{   
  // check to make sure that this could be correct given accumulated local pose based movement since last global update
  float pose_diff = sqrt((msg->x - posterior_pose->x)*(msg->x - posterior_pose->x) + (msg->y - posterior_pose->y)*(msg->y - posterior_pose->y));
  if(accum_movement*MOVEMULT < pose_diff && accum_movement != -1) // ignore because the global pose jump is too far 
    return;
     
  // currently, we use the raw psudolite data as ground truth
  posterior_pose->x = msg->x;
  posterior_pose->y = msg->y;
  posterior_pose->z = 0;
  posterior_pose->alpha = -msg->theta; // note that stargazer uses left-handed convention

  float r = sqrt(2-(2*cos(posterior_pose->alpha)));

  if(r == 0)
    posterior_pose->qw = 1;
  else
    posterior_pose->qw = sin(posterior_pose->alpha)/r; 
  
  posterior_pose->qx = 0;
  posterior_pose->qy = 0;
  posterior_pose->qz = r/2; 
  
  posterior_pose->cos_alpha = cos(posterior_pose->alpha);
  posterior_pose->sin_alpha = sin(posterior_pose->alpha);
  
  // save most recent data from both local and global pose to use in transformation matrices
  local_offset[0] = odometer_pose->x;
  local_offset[1] = odometer_pose->y;
  local_offset[2] = odometer_pose->alpha;
  
  global_offset[0] = posterior_pose->x;
  global_offset[1] = posterior_pose->y;
  global_offset[2] = posterior_pose->alpha;
  
  // reset accumulated movement
  accum_movement = -1;
  received_gps = true;
}

/*-------------------------- ROS Publisher functions --------------------*/
//int num_its = 0;
void publish_pose()
{ 
  if(USING_GPS && !received_gps)
    return;
  
  geometry_msgs::PoseStamped msg;
  msg.header.frame_id = "/2Dmap";
  msg.pose.position.x = posterior_pose->x;
  msg.pose.position.y = posterior_pose->y;
  msg.pose.position.z = posterior_pose->z;  
  msg.pose.orientation.w = posterior_pose->qw; 
  msg.pose.orientation.x = posterior_pose->qx;
  msg.pose.orientation.y = posterior_pose->qy;
  msg.pose.orientation.z = posterior_pose->qz; 
  pose_pub.publish(msg);
  
  //printf(" published pose %d\n",num_its++);
  //print_pose(posterior_pose);
}


int main(int argc, char** argv) 
{
    ros::init(argc, argv, "localization_cu");
    ros::NodeHandle nh;
    ros::Rate loop_rate(100);
    
    odometer_pose = make_pose(0,0,0);
    posterior_pose = make_pose(0,0,0);
    
    // set up publisher
    pose_pub = nh.advertise<geometry_msgs::PoseStamped>("/cu/pose_cu", 1);
    
    // set up subscribers
    odometer_pose_sub = nh.subscribe("/cu/odometer_pose_cu", 1, odometer_pose_callback);
    user_pose_sub = nh.subscribe("/cu/user_pose_cu", 1, user_pose_callback);
    stargazer_pose_sub = nh.subscribe("/cu/stargazer_pose_cu", 1, stargazer_pose_callback);

    while (ros::ok()) 
    {
      //printf(" This is the localization system \n");
      //print_pose(posterior_pose);
        
      publish_pose();
        
      ros::spinOnce();
      loop_rate.sleep();
    }
    
    
    // destroy publisher
    pose_pub.shutdown();
    
    odometer_pose_sub.shutdown();
    user_pose_sub.shutdown();
    
    destroy_pose(odometer_pose);
    destroy_pose(posterior_pose);
}