/*
 * vo.h
 *
 *  Created on: Sep 19, 2016
 *      Author: kevinsheridan
 */

#ifndef PAUVSI_M7_INCLUDE_PAUVSI_VO_VO_H_
#define PAUVSI_M7_INCLUDE_PAUVSI_VO_VO_H_

#include <opencv2/highgui/highgui.hpp>
#include <opencv2/calib3d.hpp>
#include <vector>
#include <ros/ros.h>

class VO
{

#define NUM_KEYFRAMES 1

private:

	ros::NodeHandle nh;

	cv::Mat currentFrame;
	std::vector<cv::Mat> keyFrames; //the keyFrame vector

	std::vector<double> position; // the current position
	std::vector<double> orientation; // the current orientation

public:

	VO();

	void correctPosition(std::vector<double> pos);

	void passNodeHandle(ros::NodeHandle nh);

};



#endif /* PAUVSI_M7_INCLUDE_PAUVSI_VO_VO_LIB_H_ */
