/****************************************************************************
 *   Copyright (c) 2017 Michael Shomin. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name ATLFlight nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED BY THIS LICENSE.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * In addition Supplemental Terms apply.  See the SUPPLEMENTAL file.
 ****************************************************************************/
#include "snav_interface/snav_interface.hpp"

SnavInterface::SnavInterface(ros::NodeHandle nh, ros::NodeHandle pnh) : nh_(nh), pnh_(pnh)
{
  if(sn_get_flight_data_ptr(sizeof(SnavCachedData),&cached_data_)!=0){
    ROS_ERROR("Error getting cached data.\n");
  };
  sn_update_data();
  last_sn_update_ = ros::Time::now();
  last_gen_command_time_ = ros::Time(0);
  last_traj_command_time_ = ros::Time(0);

  // Setup the publishers
  pose_est_publisher_ = nh_.advertise<geometry_msgs::PoseStamped>("pose", 10);
  pose_des_publisher_ = nh_.advertise<geometry_msgs::PoseStamped>("pose_des", 10);
  vel_est_publisher_ = nh_.advertise<geometry_msgs::Twist>("vel", 10);
  battery_voltage_publisher_ = nh_.advertise<std_msgs::Float32>("battery_voltage", 10);
  on_ground_publisher_ = nh_.advertise<std_msgs::Bool>("on_ground", 10);
  props_state_publisher_ = nh_.advertise<std_msgs::Bool>("props_state", 10);

  cmd_type_subscriber_ = nh_.subscribe("cmd_type", 10, &SnavInterface::CmdTypeCallback, this);
  mapping_type_subscriber_ = nh_.subscribe("mapping_type", 10, &SnavInterface::MappingTypeCallback, this);
  gen_cmd_subscriber_ = nh_.subscribe("gen_cmd", 10, &SnavInterface::GenCmdCallback, this);
  traj_cmd_subscriber_ = nh_.subscribe("traj_cmd", 10, &SnavInterface::TrajCmdCallback, this);
  start_props_subscriber_ = nh_.subscribe("start_props", 10, &SnavInterface::StartPropsCallback, this);
  stop_props_subscriber_ = nh_.subscribe("stop_props", 10, &SnavInterface::StopPropsCallback, this);

  pnh_.param("gps_enu_frame", gps_enu_frame_, std::string("/gps/enu"));
  pnh_.param("estimation_frame", estimation_frame_, std::string("/odom"));
  pnh_.param("base_link_frame", base_link_frame_, std::string("/base_link"));
  pnh_.param("base_link_stab_frame", base_link_stab_frame_, std::string("/base_link_stab"));
  pnh_.param("base_link_no_rot_frame", base_link_no_rot_frame_, std::string("/base_link_no_rot"));
  pnh_.param("desired_frame",desired_frame_,std::string("/desired"));
  pnh_.param("sim_gt_frame", sim_gt_frame_, std::string("/sim/ground_truth"));

  pnh_.param("simulation", simulation_, false);

  std::string rc_cmd_type_string;
  std::string rc_cmd_mapping_string;

  pnh_.param("sn_rc_cmd_type", rc_cmd_type_string, std::string("SN_RC_POS_HOLD_CMD"));
  pnh_.param("sn_rc_mapping_type", rc_cmd_mapping_string, std::string("RC_OPT_LINEAR_MAPPING"));

  SetRcCommandType(rc_cmd_type_string);
  SetRcMappingType(rc_cmd_mapping_string);

  if(!simulation_)
    GetDSPTimeOffset();
  else
  {
    dsp_offset_in_ns_  = 0;
    clock_publisher_ = nh_.advertise<rosgraph_msgs::Clock>("clock", 1);
  }

  valid_rotation_est_ = false;
  valid_rotation_sim_gt_ = false;
}

void SnavInterface::SetRcMappingType(std::string rc_cmd_mapping_string)
{
  if(rc_cmd_mapping_string == "RC_OPT_LINEAR_MAPPING")
  {
    rc_cmd_mapping_ = RC_OPT_LINEAR_MAPPING;
    ROS_INFO("SNAV mapping : RC_OPT_LINEAR_MAPPING");
  }
  else if(rc_cmd_mapping_string == "RC_OPT_ENABLE_DEADBAND")
  {
    rc_cmd_mapping_ = RC_OPT_ENABLE_DEADBAND;
    ROS_INFO("SNAV mapping : RC_OPT_ENABLE_DEADBAND");
  }
  else  if(rc_cmd_mapping_string == "RC_OPT_COMPLIANT_TRACKING")
  {
    rc_cmd_mapping_ = RC_OPT_COMPLIANT_TRACKING;
    ROS_INFO("SNAV mapping : RC_OPT_COMPLIANT_TRACKING");
  }
  else  if(rc_cmd_mapping_string == "RC_OPT_DEFAULT_RC")
  {
    rc_cmd_mapping_ = RC_OPT_DEFAULT_RC;
    ROS_INFO("SNAV mapping : RC_OPT_DEFAULT_RC");
  }
  else  if(rc_cmd_mapping_string == "RC_OPT_TRIGGER_LANDING")
  {
    rc_cmd_mapping_ = RC_OPT_TRIGGER_LANDING;
    ROS_INFO("SNAV mapping : RC_OPT_TRIGGER_LANDING");
  }
  else
  {
    rc_cmd_mapping_ = RC_OPT_LINEAR_MAPPING;
    ROS_INFO("unrecognized sn_rc_mapping_type, using default SNAV mapping : RC_OPT_LINEAR_MAPPING");
  }
}


void SnavInterface::SetRcCommandType(std::string rc_cmd_type_string)
{
  if(rc_cmd_type_string == "SN_RC_RATES_CMD")
  {
    rc_cmd_type_ = SN_RC_RATES_CMD;
    ROS_INFO("SNAV cmd type: SN_RC_RATES_CMD");
  }
  else if(rc_cmd_type_string == "SN_RC_THRUST_ANGLE_CMD")
  {
    rc_cmd_type_ = SN_RC_THRUST_ANGLE_CMD;
    ROS_INFO("SNAV cmd type: SN_RC_THRUST_ANGLE_CMD");
  }
  else if(rc_cmd_type_string == "SN_RC_ALT_HOLD_CMD")
  {
    rc_cmd_type_ = SN_RC_ALT_HOLD_CMD;
    ROS_INFO("SNAV cmd type: SN_RC_ALT_HOLD_CMD");
  }
  else if(rc_cmd_type_string == "SN_RC_THRUST_ANGLE_GPS_HOVER_CMD")
  {
    rc_cmd_type_ = SN_RC_THRUST_ANGLE_GPS_HOVER_CMD;
    ROS_INFO("SNAV cmd type: SN_RC_THRUST_ANGLE_GPS_HOVER_CMD");
  }
  else if(rc_cmd_type_string == "SN_RC_GPS_POS_HOLD_CMD")
  {
    rc_cmd_type_ = SN_RC_GPS_POS_HOLD_CMD;
    ROS_INFO("SNAV cmd type: SN_RC_GPS_POS_HOLD_CMD");
  }
  else if(rc_cmd_type_string == "SN_RC_OPTIC_FLOW_POS_HOLD_CMD")
  {
    rc_cmd_type_ = SN_RC_OPTIC_FLOW_POS_HOLD_CMD;
    ROS_INFO("SNAV cmd type: SN_RC_OPTIC_FLOW_POS_HOLD_CMD");
  }
  else if(rc_cmd_type_string == "SN_RC_VIO_POS_HOLD_CMD")
  {
    rc_cmd_type_ = SN_RC_VIO_POS_HOLD_CMD;
    ROS_INFO("SNAV cmd type: SN_RC_VIO_POS_HOLD_CMD");
  }
  else if(rc_cmd_type_string == "SN_RC_ALT_HOLD_LOW_ANGLE_CMD")
  {
    rc_cmd_type_ = SN_RC_ALT_HOLD_LOW_ANGLE_CMD;
    ROS_INFO("SNAV cmd type: SN_RC_ALT_HOLD_LOW_ANGLE_CMD");
  }
  else if(rc_cmd_type_string == "SN_RC_POS_HOLD_CMD")
  {
    rc_cmd_type_ = SN_RC_POS_HOLD_CMD;
    ROS_INFO("SNAV cmd type: SN_RC_POS_HOLD_CMD");
  }
  else
  {
    // Default is position hold mode command
    ROS_INFO("Unrecognized sn_rc_cmd_type, using default SNAV cmd type: SN_RC_POS_HOLD_CMD");
    rc_cmd_type_ = SN_RC_POS_HOLD_CMD;
  }
}

void SnavInterface::GetDSPTimeOffset()
{
  // get the adsp offset.
  int64_t dsptime;
#ifdef QC_SOC_TARGET_APQ8096
  static const char qdspTimerTickPath[] = "/sys/kernel/boot_slpi/qdsp_qtimer";
#endif
#ifdef QC_SOC_TARGET_APQ8074
  static const char qdspTimerTickPath[] = "/sys/kernel/boot_adsp/qdsp_qtimer";
#endif
  char qdspTicksStr[20] = "";

  static const double clockFreq = 1 / 19.2;
  FILE * qdspClockfp = fopen( qdspTimerTickPath, "r" );
  fread( qdspTicksStr, 16, 1, qdspClockfp );
  uint64_t qdspTicks = strtoull( qdspTicksStr, 0, 16 );
  fclose( qdspClockfp );

  dsptime = (int64_t)( qdspTicks*clockFreq*1e3 );

  //get the apps proc timestamp;
  int64_t appstimeInNs;
  struct timespec t;
  clock_gettime( CLOCK_REALTIME, &t );

  uint64_t timeNanoSecMonotonic = (uint64_t)(t.tv_sec) * 1000000000ULL + t.tv_nsec;
  appstimeInNs = (int64_t)timeNanoSecMonotonic;

  // now compute the offset.
  dsp_offset_in_ns_  = appstimeInNs - dsptime;

  ROS_INFO_STREAM("DSP offset: " <<   dsp_offset_in_ns_ << " ns");
}


void SnavInterface::PublishLowFrequencyData(const ros::TimerEvent& event)
{
  if( (ros::Time::now()-last_sn_update_) < ros::Duration(1.0) )
  {
    PublishBatteryVoltage();
    PublishOnGroundFlag();
    PublishPropsStateFlag();
  }
  else
  {
    ROS_ERROR("Tried to publish low frequency data, but sn_update_data() has not been called in at least 1 second");
  }
}

void SnavInterface::CmdTypeCallback(const std_msgs::String::ConstPtr& msg)
{
  SetRcCommandType(msg->data);
}

void SnavInterface::MappingTypeCallback(const std_msgs::String::ConstPtr& msg)
{
  SetRcMappingType(msg->data);
}

void SnavInterface::GenCmdCallback(const geometry_msgs::Twist::ConstPtr& msg)
{
  generic_command_ = *msg;
  last_gen_command_time_ = ros::Time::now();
  SendGenCommand();
}

void SnavInterface::TrajCmdCallback(const std_msgs::Float32MultiArray::ConstPtr& msg)
{
  last_traj_command_time_ = ros::Time::now();
  sn_send_trajectory_tracking_command(SN_POSITION_CONTROL_VIO, SN_TRAJ_DEFAULT, msg->data[0], msg->data[1], msg->data[2], msg->data[3], msg->data[4], msg->data[5], msg->data[6], msg->data[7], msg->data[8], msg->data[9], msg->data[10]);
}

void SnavInterface::StartPropsCallback(const std_msgs::Empty::ConstPtr& msg)
{
  sn_spin_props();
}

void SnavInterface::StopPropsCallback(const std_msgs::Empty::ConstPtr& msg)
{
  sn_stop_props();
}

void SnavInterface::SendGenCommand()
{
  float snav_rc_cmd[4];

  sn_apply_cmd_mapping(rc_cmd_type_, rc_cmd_mapping_,
      generic_command_.linear.x,
      generic_command_.linear.y,
      generic_command_.linear.z,
      generic_command_.angular.z,
      &snav_rc_cmd[0],
      &snav_rc_cmd[1],
      &snav_rc_cmd[2],
      &snav_rc_cmd[3]);

  sn_send_rc_command(rc_cmd_type_, rc_cmd_mapping_,
      snav_rc_cmd[0],
      snav_rc_cmd[1],
      snav_rc_cmd[2],
      snav_rc_cmd[3]);
}



void SnavInterface::UpdatePoseMessages()
{
  tf2::Quaternion q;
  GetRotationQuaternion(q);
  UpdatePosVelMessages(q);
}

void SnavInterface::GetRotationQuaternion(tf2::Quaternion &q)
{
  // Get Rotation Matrix from sn_cached_data_, convert to tf2 Matrix
  tf2::Matrix3x3 RR( cached_data_->attitude_estimate.rotation_matrix[0],
      cached_data_->attitude_estimate.rotation_matrix[1],
      cached_data_->attitude_estimate.rotation_matrix[2],
      cached_data_->attitude_estimate.rotation_matrix[3],
      cached_data_->attitude_estimate.rotation_matrix[4],
      cached_data_->attitude_estimate.rotation_matrix[5],
      cached_data_->attitude_estimate.rotation_matrix[6],
      cached_data_->attitude_estimate.rotation_matrix[7],
      cached_data_->attitude_estimate.rotation_matrix[8]);

  // Convert Rotation Matrix to quaternion
  RR.getRotation(q);

  // Check for NAN in quaternion
  if(q.getX()!=q.getX() || q.getY()!=q.getY() ||
      q.getZ()!=q.getZ() || q.getW()!=q.getW())
  {
    ROS_WARN("Rotation Quaternion is NAN");
    valid_rotation_est_ = false;
  }
  else
  {
    valid_rotation_est_ = true;
  }
}

void SnavInterface::UpdatePosVelMessages(tf2::Quaternion q)
{
  // TODO: Move this elsewhere
  est_vel_msg_.linear.x = cached_data_->pos_vel.velocity_estimated[0];
  est_vel_msg_.linear.y = cached_data_->pos_vel.velocity_estimated[1];
  est_vel_msg_.linear.z = cached_data_->pos_vel.velocity_estimated[2];
  est_vel_msg_.angular.x = cached_data_->imu_0_compensated.ang_vel[0];
  est_vel_msg_.angular.y = cached_data_->imu_0_compensated.ang_vel[1];
  est_vel_msg_.angular.z = cached_data_->imu_0_compensated.ang_vel[2];


  tf2::Transform est_tf(tf2::Transform(q, tf2::Vector3(
          cached_data_->pos_vel.position_estimated[0],
          cached_data_->pos_vel.position_estimated[1],
          cached_data_->pos_vel.position_estimated[2])));

  est_transform_msg_.child_frame_id = base_link_frame_;
  est_transform_msg_.header.frame_id = estimation_frame_;

  ros::Time timestamp;
  timestamp = ros::Time((double)(cached_data_->pos_vel.time + (dsp_offset_in_ns_/1e3))/1e6);
  est_transform_msg_.header.stamp = timestamp;

  tf2::convert(est_tf, est_transform_msg_.transform);

  tf2::toMsg(est_tf, est_pose_msg_.pose);
  est_pose_msg_.header.stamp = timestamp;
  est_pose_msg_.header.frame_id = est_transform_msg_.header.frame_id;

  tf2::Quaternion q_des;
  q_des.setEuler(0.0, 0.0, cached_data_->pos_vel.yaw_desired);
  tf2::Transform des_tf(tf2::Transform(q_des, tf2::Vector3(
          cached_data_->pos_vel.position_desired[0],
          cached_data_->pos_vel.position_desired[1],
          cached_data_->pos_vel.position_desired[2])));

  tf2::convert(des_tf, des_transform_msg_.transform);
  des_transform_msg_.child_frame_id = desired_frame_;
  des_transform_msg_.header.frame_id = estimation_frame_;
  des_transform_msg_.header.stamp = timestamp;

  tf2::toMsg(des_tf, des_pose_msg_.pose);
  des_pose_msg_.header.stamp = timestamp;
  des_pose_msg_.header.frame_id = des_transform_msg_.header.frame_id;

  tf2::Matrix3x3 R_eg(tf2::Matrix3x3(
        cached_data_->pos_vel.R_eg[0],
        cached_data_->pos_vel.R_eg[1],
        cached_data_->pos_vel.R_eg[2],
        cached_data_->pos_vel.R_eg[3],
        cached_data_->pos_vel.R_eg[4],
        cached_data_->pos_vel.R_eg[5],
        cached_data_->pos_vel.R_eg[6],
        cached_data_->pos_vel.R_eg[7],
        cached_data_->pos_vel.R_eg[8]));

  tf2::Transform gps_enu_tf(tf2::Transform(R_eg, tf2::Vector3(
        cached_data_->pos_vel.t_eg[0],
        cached_data_->pos_vel.t_eg[1],
        cached_data_->pos_vel.t_eg[2])));

  tf2::convert(gps_enu_tf, gps_enu_transform_msg_.transform);
  gps_enu_transform_msg_.child_frame_id = gps_enu_frame_;
  gps_enu_transform_msg_.header.frame_id = estimation_frame_;
  gps_enu_transform_msg_.header.stamp = timestamp;

  // base_link_no_rot and base_link_stab
  tf2::Matrix3x3 RR_est(q);
  tf2Scalar roll, pitch, yaw;
  RR_est.getRPY(roll, pitch, yaw);

  //tf2::Transform base_link_no_rot_tf(tf2::Transform(
  //      tf2::Quaternion(0.0, 0.0, 0.0, 1.0), tf2::Vector3(
  //        cached_data_->pos_vel.position_estimated[0],
  //        cached_data_->pos_vel.position_estimated[1],
  //        cached_data_->pos_vel.position_estimated[2])));
  tf2::Transform base_link_no_rot_tf(tf2::Transform(
          RR_est.inverse(), tf2::Vector3(0.0, 0.0, 0.0)));

  base_link_no_rot_transform_msg_.child_frame_id = base_link_no_rot_frame_;
  base_link_no_rot_transform_msg_.header.frame_id = base_link_frame_;
  base_link_no_rot_transform_msg_.header.stamp = timestamp;
  tf2::convert(base_link_no_rot_tf, base_link_no_rot_transform_msg_.transform);

  tf2::Matrix3x3 RR_yaw;
  RR_yaw.setRPY(0, 0, yaw);
  tf2::Transform base_link_stab_tf(tf2::Transform(RR_yaw,
        tf2::Vector3(0.0, 0.0, 0.0)));

  base_link_stab_transform_msg_.child_frame_id = base_link_stab_frame_;
  base_link_stab_transform_msg_.header.frame_id = base_link_no_rot_frame_;
  base_link_stab_transform_msg_.header.stamp = timestamp;
  tf2::convert(base_link_stab_tf, base_link_stab_transform_msg_.transform);

}

void SnavInterface::UpdateSimMessages(){

  // Get Rotation Matrix from sn_cached_data_, convert to tf2 Matrix
  tf2::Matrix3x3 RR(
      cached_data_->sim_ground_truth.R[0],
      cached_data_->sim_ground_truth.R[1],
      cached_data_->sim_ground_truth.R[2],
      cached_data_->sim_ground_truth.R[3],
      cached_data_->sim_ground_truth.R[4],
      cached_data_->sim_ground_truth.R[5],
      cached_data_->sim_ground_truth.R[6],
      cached_data_->sim_ground_truth.R[7],
      cached_data_->sim_ground_truth.R[8]);

  // Convert Rotation Matrix to quaternion
  tf2::Quaternion q;
  RR.getRotation(q);

  // Check for NAN in quaternion
  if(q.getX()!=q.getX() || q.getY()!=q.getY() ||
      q.getZ()!=q.getZ() || q.getW()!=q.getW())
  {
    ROS_WARN("Rotation Quaternion is NAN");
    valid_rotation_sim_gt_ = false;
  }
  else
  {
    valid_rotation_sim_gt_ = true;

    tf2::Transform sim_gt_tf(tf2::Transform(q, tf2::Vector3(
            cached_data_->sim_ground_truth.position[0],
            cached_data_->sim_ground_truth.position[1],
            cached_data_->sim_ground_truth.position[2])));

    sim_gt_tf = sim_gt_tf.inverse();
    sim_gt_transform_msg_.child_frame_id = sim_gt_frame_;
    sim_gt_transform_msg_.header.frame_id = base_link_frame_;

    ros::Time timestamp;
    timestamp = ros::Time((double)(cached_data_->sim_ground_truth.time + (dsp_offset_in_ns_/1e3))/1e6);
    sim_gt_transform_msg_.header.stamp = timestamp;

    tf2::convert(sim_gt_tf, sim_gt_transform_msg_.transform);

    tf2::toMsg(sim_gt_tf.inverse(), sim_gt_pose_msg_.pose);
    sim_gt_pose_msg_.header.stamp = timestamp;
    sim_gt_pose_msg_.header.frame_id = sim_gt_frame_;

  }
}

void SnavInterface::UpdateSnavData(){
  if (sn_update_data() != 0)
  {
    ROS_WARN("sn_update_data failed, not publishing");
    return;
  }
  last_sn_update_ = ros::Time::now();

  if(simulation_)
  {
    rosgraph_msgs::Clock simtime;
    simtime.clock = ros::Time((double)(cached_data_->general_status.time)/1e6);
    clock_publisher_.publish(simtime);
  }
}

void SnavInterface::PublishBatteryVoltage(){
  std_msgs::Float32 voltage_msg;
  voltage_msg.data = cached_data_->general_status.voltage;
  battery_voltage_publisher_.publish( voltage_msg );
}

void SnavInterface::PublishOnGroundFlag(){
  std_msgs::Bool on_ground_msg;
  on_ground_msg.data = cached_data_->general_status.on_ground;
  on_ground_publisher_.publish( on_ground_msg );
}

void SnavInterface::PublishPropsStateFlag(){
  std_msgs::Bool props_state_msg;
  SnPropsState props_state = (SnPropsState) cached_data_->general_status.props_state;
  if (props_state == SN_PROPS_STATE_SPINNING)
  {
    props_state_msg.data = true;
  }
  else
  {
    props_state_msg.data = false;
  }
  props_state_publisher_.publish( props_state_msg );
}

void SnavInterface::BroadcastEstTf(){
  if(valid_rotation_est_)
    tf_pub_.sendTransform(est_transform_msg_);
  else
    ROS_ERROR("Tried to broadcast invalid Est Tf");
}

void SnavInterface::BroadcastDesiredTf(){
  if(valid_rotation_est_)
    tf_pub_.sendTransform(des_transform_msg_);
  else
    ROS_ERROR("Tried to broadcast invalid Desired Tf");
}

void SnavInterface::BroadcastGpsEnuTf(){
  if(valid_rotation_est_)
    tf_pub_.sendTransform(gps_enu_transform_msg_);
  else
    ROS_ERROR("Tried to broadcast invalid GPS ENU Tf");
}

void SnavInterface::BroadcastBaseLinkNoRotTf(){
  if (valid_rotation_est_){
    tf_pub_.sendTransform(base_link_no_rot_transform_msg_);
  }
  else
    ROS_ERROR("Tried to broadcast invalid base link no rotation Tf");
}

void SnavInterface::BroadcastBaseLinkStabTf(){
  if (valid_rotation_est_){
    tf_pub_.sendTransform(base_link_stab_transform_msg_);
  }
  else
    ROS_ERROR("Tried to broadcast invalid base link stabilized Tf");
}

void SnavInterface::BroadcastSimGtTf(){
  if (valid_rotation_sim_gt_){
    tf_pub_.sendTransform(sim_gt_transform_msg_);
  }
  else
    ROS_ERROR("Tried to broadcast invalid sim ground truth Tf");
}

void SnavInterface::PublishEstPose(){
  if(valid_rotation_est_)
    pose_est_publisher_.publish(est_pose_msg_);
  else
    ROS_ERROR("Tried to publish invalid Est Pose");
}

void SnavInterface::PublishDesiredPose(){
  if(valid_rotation_est_)
    pose_des_publisher_.publish(des_pose_msg_);
  else
    ROS_ERROR("Tried to publish invalid Desired Pose");
}

void SnavInterface::PublishSimGtPose(){
  if (valid_rotation_sim_gt_)
    pose_est_publisher_.publish(sim_gt_pose_msg_);
  else
    ROS_ERROR("Tried to publish invalid sim ground truth pose");
}

void SnavInterface::PublishEstVel(){
  if(valid_rotation_est_)
    vel_est_publisher_.publish(est_vel_msg_);
  else
    ROS_ERROR("Tried to publish invalid Est Vel");
}
