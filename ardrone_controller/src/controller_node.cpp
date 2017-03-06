#include <ros/ros.h>
#include <std_msgs/String.h>
#include <sstream>
#include <sensor_msgs/Imu.h>
#include <nav_msgs/Odometry.h>
#include <vector>
#include <assert.h>
#include <ros/time.h>
#include <geometry_msgs/Vector3.h>
#include <tf/LinearMath/Quaternion.h>
#include <tf/transform_datatypes.h>
#include <tf/transform_listener.h>
#include <tf/transform_broadcaster.h>
#include <sensor_fusion_comm/DoubleArrayStamped.h>
#include <geometry_msgs/PoseWithCovarianceStamped.h>

class OdometryIMU
{
public:
    geometry_msgs::Vector3 position,velocity,acceleration,acc_avg;//All in world frame alligned with magnetometer
    
    // accelerations and magnetic field
    tf::Vector3 curr_acc, curr_mag;
    
    double t_now, t_last ,t_start;
    double dt;
    bool first_msg;
    unsigned int count;
    
    // calibration transform
    tf::Transform calib_tf;
    
    tf::TransformListener slam_listener;
    tf::Vector3 slam_pos;
    tf::Vector3 odom_pos;
    tf::Quaternion slam_quat;
    tf::Quaternion imu_quat;
    tf::Quaternion quat_cam_drone;
    tf::TransformBroadcaster br;
    tf::Transform level;
    geometry_msgs::PoseWithCovarianceStamped ekf_pose_out_;
    nav_msgs::Odometry ekf_odom_out_;
    
    OdometryIMU():count(0)
    {
        first_msg = true;
        position.x=position.y=position.z=0.0;
        velocity=acceleration=position;
        //Rough calibration for imu alignment
//         acc_avg.x=1.55;
//         acc_avg.y=1.31;
//         acc_avg.z=9.23;
        t_last = t_now = t_start = NULL; 
    }

    //TODO account for attitude in estimate
    void imu_msg_callback(sensor_msgs::Imu imu_data)//DO NOT PASS BY REFERENCE
    {
        if(first_msg){
            first_msg = false;
            t_last=t_now=t_start=imu_data.header.stamp.toSec();
        }
        else{
            t_last=t_now;
            t_now=imu_data.header.stamp.toSec();
        }
    
        dt=t_now-t_last;
        tf::quaternionMsgToTF(imu_data.orientation,imu_quat);
        acceleration = bodyToWorldFrame(imu_data.linear_acceleration,imu_data.orientation);
	
	// set curr acceleration
	curr_acc = tf::Vector3(tfScalar(imu_data.linear_acceleration.x), 
		               tfScalar(imu_data.linear_acceleration.y),
			       tfScalar(imu_data.linear_acceleration.z));
    }
    
    geometry_msgs::Vector3 bodyToWorldFrame(const geometry_msgs::Vector3& x_body, 
                                            const geometry_msgs::Quaternion& _q)
    {
    
        tf::quaternionMsgToTF(_q,imu_quat);
        tf::Quaternion z(x_body.x,x_body.y,x_body.z,0.0);// 
        z=imu_quat*z*(imu_quat.inverse());
        
        geometry_msgs::Vector3 x_world;
        x_world.x=z.x();x_world.y=z.y();x_world.z=z.z();
        return x_world;
    }
    
    tf::Vector3 rotateVector(const tf::Vector3& vec, const tf::Quaternion& q)
    {
        
        tf::Quaternion z(vec.getX(),vec.getY(),vec.getZ(),0.0);// 
        z=q*z*(q.inverse());
        
        tf::Vector3 out(z.getX(),z.getY(),z.getZ());
        return out;
    }
    
    void get_slam_tf()
    {
        //try to get stuff from tf node
        try{
            tf::StampedTransform slam_tf;
            //slam_listener.lookupTransform("/level", "/camera",ros::Time(0), slam_tf);
            slam_quat = slam_tf.getRotation();
            slam_pos = slam_tf.getOrigin();
        }
        catch(tf::TransformException &ex) {
            ROS_ERROR("%s",ex.what());
        }
    }
    
    void EKF_callback(sensor_fusion_comm::DoubleArrayStamped msg)
    {
	ekf_pose_out_.pose.pose.position.x = msg.data[0];
	ekf_pose_out_.pose.pose.position.y = msg.data[1];
	ekf_pose_out_.pose.pose.position.z = msg.data[2];
	
	ekf_pose_out_.pose.pose.orientation.x = msg.data[7];
	ekf_pose_out_.pose.pose.orientation.y = msg.data[8];
	ekf_pose_out_.pose.pose.orientation.z = msg.data[9];
	ekf_pose_out_.pose.pose.orientation.w = msg.data[6];
	
	ekf_pose_out_.header.frame_id = "world";
	ekf_pose_out_.header.stamp = ros::Time::now();
	
	ekf_odom_out_.header.stamp = ros::Time::now();
	ekf_odom_out_.pose.covariance = ekf_pose_out_.pose.covariance;
	//ekf_odom_out_.pose.covariance[1] = 10000;
	ekf_odom_out_.pose.pose = ekf_pose_out_.pose.pose;
	ekf_odom_out_.header.frame_id = "world";
	ekf_odom_out_.child_frame_id = "v_odom";
	
    }
    
    // TODO test
    void calibrate(int N, float time)
    {
      tf::Vector3 avg_acc(tfScalar(0),tfScalar(0),tfScalar(0)); // x-axis
      tf::Vector3 avg_mag(tfScalar(0),tfScalar(0),tfScalar(0)); // z-axis
      
      for (int it=0; it<N ; it++)
      {
	     // TODO remove after testing
     std::cout << curr_acc.x() << "\t" << curr_mag.x() << std::endl;
     std::cout << curr_acc.y() << "\t" << curr_mag.y() << std::endl;
     std::cout << curr_acc.z() << "\t" << curr_mag.z() << std::endl;
	
	// wait for imu data to be received
	ros::spinOnce();
	// add to average
	avg_acc += curr_acc * 1.0 / N;
	avg_mag += curr_mag * 1.0 / N;
	
	// wait for next 
	sleep(time);
      }
     
     // create z axis through cross product
     tf::Vector3 y_axis = avg_acc.cross(avg_mag);
     
     // TODO remove after testing
     std::cout << avg_acc.x() << "\t" << y_axis.x() << "\t" << avg_mag.x() << std::endl;
     std::cout << avg_acc.y() << "\t" << y_axis.y() << "\t" << avg_mag.y() << std::endl;
     std::cout << avg_acc.z() << "\t" << y_axis.z() << "\t" << avg_mag.z() << std::endl;
     
     // create a transform tf::Transform from x,y,z axes
     tf::Matrix3x3 m = tf::Matrix3x3(avg_acc.x(), y_axis.x(), avg_mag.x(), 
				     avg_acc.y(), y_axis.y(), avg_mag.y(), 
				     avg_acc.z(), y_axis.z(), avg_mag.z());
     
     calib_tf = tf::Transform(m);
     sleep(2);
    }
    
    void mag_callback(geometry_msgs::Vector3Stamped msg)
    {
      std::cout << "mag callback"<< std::endl;
      curr_mag = tf::Vector3(tfScalar(msg.vector.x), 
			     tfScalar(msg.vector.y), 
			     tfScalar(msg.vector.z));
    }
};

int main(int argc, char **argv)
{
    
    ros::init(argc, argv, "controller");
    OdometryIMU od_IMU;
    ros::NodeHandle nh;
    
    // subscribe to magnetic and accelerometer data from imu
    ros::Subscriber imu_sub = nh.subscribe<sensor_msgs::Imu>( "/ardrone/imu", 10,  &OdometryIMU::imu_msg_callback, &od_IMU);
    ros::Subscriber mag_sub = nh.subscribe<geometry_msgs::Vector3Stamped>("/ardrone/mag", 10, &OdometryIMU::mag_callback, &od_IMU);
    
    // fused data from ekf
    ros::Subscriber EKF_sub = nh.subscribe<sensor_fusion_comm::DoubleArrayStamped>("/ssf_core/state_out", 2, &OdometryIMU::EKF_callback, &od_IMU);
    
    ros::Publisher ekf_pub = nh.advertise<geometry_msgs::PoseWithCovarianceStamped>("/ekf_pose", 2);
    ros::Publisher ekf_publisher = nh.advertise<nav_msgs::Odometry>("/vo",2);
    
    ros::Rate loop_rate(30); //the received msg is published at 200Hz.
    
    // calibrate imu to align average down direction with positive z
    int N_samples = 100;
    od_IMU.calibrate(100, 0.01);
    
    std::cout << od_IMU.calib_tf.getRotation().getAngle() << std::endl;
    
    while (ros::ok())
    {    
        od_IMU.get_slam_tf();
        ros::spinOnce();
        
	ekf_pub.publish(od_IMU.ekf_pose_out_);
	ekf_publisher.publish(od_IMU.ekf_odom_out_);
	
        loop_rate.sleep();
    }
    
    return 0;
}