/*
 * vio.cpp
 *
 *  Created on: Sep 19, 2016
 *      Author: kevinsheridan
 */

#include "vio.h"

/*
 * starts all state vectors at 0.0
 */
VIO::VIO()
{
	this->readROSParameters();

	//set up image transport
	image_transport::ImageTransport it(nh);
	this->cameraSub = it.subscribeCamera(this->getCameraTopic(), 1, &VIO::cameraCallback, this);

	//setup imu sub
	this->imuSub = nh.subscribe(this->getIMUTopic(), 100, &VIO::imuCallback, this);

	this->pose = geometry_msgs::PoseStamped();
	this->pose.pose.orientation.w = 1.0;
	this->pose.pose.orientation.x = 0.0;
	this->pose.pose.orientation.y = 0.0;
	this->pose.pose.orientation.z = 0.0;
	this->pose.header.stamp = ros::Time::now();

	this->velocity.vector.x = 0.0;
	this->velocity.vector.y = 0.0;
	this->velocity.vector.z = 0.0;

	this->angular_velocity.vector.x = 0.0;
	this->angular_velocity.vector.y = 0.0;
	this->angular_velocity.vector.z = 0.0;

	this->broadcastWorldToOdomTF();
}

VIO::~VIO()
{

}

void VIO::cameraCallback(const sensor_msgs::ImageConstPtr& img, const sensor_msgs::CameraInfoConstPtr& cam)
{
	ros::Time start = ros::Time::now();
	cv::Mat temp = cv_bridge::toCvShare(img, "mono8")->image.clone();

	//set the K and D matrices
	this->setK(get3x3FromVector(cam->K));
	this->setD(cv::Mat(cam->D, false));

	/* sets the current frame and its time created
	 * It also runs a series of functions which ultimately estimate
	 * the motion of the camera
	 */
	this->setCurrentFrame(temp, cv_bridge::toCvCopy(img, "mono8")->header.stamp);

	ROS_DEBUG_STREAM_THROTTLE(0.5, (ros::Time::now().toSec() - start.toSec()) * 1000 << " milliseconds runtime");

	this->viewImage(this->getCurrentFrame());

	//ros::Duration d = ros::Duration(0.1);
	//d.sleep();
}

void VIO::imuCallback(const sensor_msgs::ImuConstPtr& msg)
{
	//ROS_DEBUG_STREAM_THROTTLE(0.1, "accel: " << msg->linear_acceleration);
	this->addIMUReading(*msg);
	//ROS_DEBUG_STREAM("time compare " << ros::Time::now().toNSec() - msg->header.stamp.toNSec());
}

cv::Mat VIO::get3x3FromVector(boost::array<double, 9> vec)
{
	cv::Mat mat = cv::Mat(3, 3, CV_32F);
	for(int i = 0; i < 3; i++)
	{
		mat.at<float>(i, 0) = vec.at(3 * i + 0);
		mat.at<float>(i, 1) = vec.at(3 * i + 1);
		mat.at<float>(i, 2) = vec.at(3 * i + 2);
	}

	ROS_DEBUG_STREAM_ONCE("K = " << mat);
	return mat;
}

/*
 * shows cv::Mat
 */
void VIO::viewImage(cv::Mat img, bool rectify){
	if(rectify)
	{
		cv::Matx33d newK = K;
		newK(0, 0) = 100;
		newK(1, 1) = 100;
		cv::fisheye::undistortImage(img, img, this->K, this->D, newK);
		//ROS_DEBUG_STREAM(newK);
	}
	cv::imshow("test", img);
	cv::waitKey(30);
}

/*
 * draws frame with its features
 */
void VIO::viewImage(Frame frame){
	cv::Mat img;
	cv::drawKeypoints(frame.image, frame.getKeyPointVectorFromFeatures(), img, cv::Scalar(0, 0, 255));
	this->viewImage(img, false);

}

/*
 * sets the current frame and computes important
 * info about it
 * finds corners
 * describes corners
 */
void VIO::setCurrentFrame(cv::Mat img, ros::Time t)
{
	if(currentFrame.isFrameSet())
	{
		//first set the last frame to current frame
		lastFrame = currentFrame;
	}

	currentFrame = Frame(img, t, lastFrame.nextFeatureID); // create a frame with a starting ID of the last frame's next id

	this->run();
}

/*
 * runs:
 * feature detection, ranking, flowing
 * motion estimation
 * feature mapping
 */
void VIO::run()
{
	// if there is a last frame, flow features and estimate motion
	if(lastFrame.isFrameSet())
	{
		if(lastFrame.features.size() > 0)
		{
			this->flowFeaturesToNewFrame(lastFrame, currentFrame);
			currentFrame.cleanUpFeaturesByKillRadius(this->KILL_RADIUS);
			//this->checkFeatureConsistency(currentFrame, this->FEATURE_SIMILARITY_THRESHOLD);
		}

		//MOTION ESTIMATION

		double certainty = this->estimateMotion();
	}

	//check the number of 2d features in the current frame
	//if this is below the required amount refill the feature vector with
	//the best new feature. It must not be redundant.

	//ROS_DEBUG_STREAM("feature count: " << currentFrame.features.size());

	if(currentFrame.features.size() < this->NUM_FEATURES)
	{
		//add n new unique features
		//ROS_DEBUG("low on features getting more");
		currentFrame.getAndAddNewFeatures(this->NUM_FEATURES - currentFrame.features.size(), this->FAST_THRESHOLD, this->KILL_RADIUS, this->MIN_NEW_FEATURE_DISTANCE);
		//currentFrame.describeFeaturesWithBRIEF();
	}

	this->broadcastWorldToOdomTF();

	//ROS_DEBUG_STREAM("imu readings: " << this->imuMessageBuffer.size());
}

/*
 * gets parameters from ROS param server
 */
void VIO::readROSParameters()
{
	//CAMERA TOPIC
	ROS_WARN_COND(!ros::param::has("~cameraTopic"), "Parameter for 'cameraTopic' has not been set");
	ros::param::param<std::string>("~cameraTopic", cameraTopic, DEFAULT_CAMERA_TOPIC);
	ROS_DEBUG_STREAM("camera topic is: " << cameraTopic);

	//IMU TOPIC
	ROS_WARN_COND(!ros::param::has("~imuTopic"), "Parameter for 'imuTopic' has not been set");
	ros::param::param<std::string>("~imuTopic", imuTopic, DEFAULT_IMU_TOPIC);
	ROS_DEBUG_STREAM("IMU topic is: " << imuTopic);

	ros::param::param<std::string>("~imu_frame_name", imu_frame, DEFAULT_IMU_FRAME_NAME);
	ros::param::param<std::string>("~camera_frame_name", camera_frame, DEFAULT_CAMERA_FRAME_NAME);
	ros::param::param<std::string>("~odom_frame_name", odom_frame, DEFAULT_ODOM_FRAME_NAME);
	ros::param::param<std::string>("~center_of_mass_frame_name", CoM_frame, DEFAULT_COM_FRAME_NAME);
	ros::param::param<std::string>("~world_frame_name", world_frame, DEFAULT_WORLD_FRAME_NAME);

	ros::param::param<int>("~fast_threshold", FAST_THRESHOLD, DEFAULT_FAST_THRESHOLD);

	ros::param::param<float>("~feature_kill_radius", KILL_RADIUS, DEFAULT_2D_KILL_RADIUS);

	ros::param::param<int>("~feature_similarity_threshold", FEATURE_SIMILARITY_THRESHOLD, DEFAULT_FEATURE_SIMILARITY_THRESHOLD);
	ros::param::param<bool>("~kill_by_dissimilarity", KILL_BY_DISSIMILARITY, false);

	ros::param::param<float>("~min_eigen_value", MIN_EIGEN_VALUE, DEFAULT_MIN_EIGEN_VALUE);

	ros::param::param<int>("~num_features", NUM_FEATURES, DEFAULT_NUM_FEATURES);

	ros::param::param<int>("~min_new_feature_distance", MIN_NEW_FEATURE_DISTANCE, DEFAULT_MIN_NEW_FEATURE_DIST);

	ros::param::param<double>("~starting_gravity_mag", GRAVITY_MAG, DEFAULT_GRAVITY_MAGNITUDE);
}


/*
 * This will match feature descriptors between two images
 *
 * In the first variant of this method, the train descriptors are passed as an input argument. In the
 * second variant of the method, train descriptors collection that was set by DescriptorMatcher::add is
 * used. Optional mask (or masks) can be passed to specify which query and training descriptors can be
 * matched. Namely, queryDescriptors[i] can be matched with trainDescriptors[j] only if
 * mask.at\<uchar\>(i,j) is non-zero.
 */
std::vector<cv::DMatch> VIO::matchFeaturesWithFlann(cv::Mat query, cv::Mat train){
	std::vector<cv::DMatch> matches;
	cv::FlannBasedMatcher matcher(new cv::flann::LshIndexParams(20, 10, 2));
	matcher.match(query, train, matches);

	ROS_DEBUG_STREAM_THROTTLE(2, "query size: " << query.rows << " train size: " << train.rows << " matches size: " << matches.size());

	return matches;
}

/*
 * uses optical flow to find a vector of features in another image
 * This function does not require a prediction
 * This will set the feature vector within the new frame with the
 * flowed points
 */
bool VIO::flowFeaturesToNewFrame(Frame& oldFrame, Frame& newFrame){

	std::vector<cv::Point2f> oldPoints = oldFrame.getPoint2fVectorFromFeatures();
	ROS_DEBUG_STREAM_ONCE("got " << oldPoints.size() << " old point2fs from the oldframe which has " << oldFrame.features.size() << " features");
	std::vector<cv::Point2f> newPoints;

	std::vector<uchar> status; // status vector for each point
	cv::Mat error; // error vector for each point

	ROS_DEBUG_ONCE("running lucas kande optical flow algorithm");
	/*
	 * this calculates the new positions of the old features in the new image
	 * status tells us whether or not a point index has been flowed over to the new frame
	 * last value is a minimum eigen value thresh
	 * it will kill bad features
	 */
	cv::calcOpticalFlowPyrLK(oldFrame.image, newFrame.image, oldPoints, newPoints, status, error, cv::Size(21, 21), 3,
			cv::TermCriteria(cv::TermCriteria::COUNT+cv::TermCriteria::EPS, 30, 0.01), 0, this->MIN_EIGEN_VALUE);

	ROS_DEBUG_STREAM_ONCE("ran optical flow and got " << newPoints.size() << " points out");

	int lostFeatures = 0;
	//next add these features into the new Frame
	for (int i = 0; i < newPoints.size(); i++)
	{
		//check if the point was able to flow
		if(status.at(i) == 1)
		{
			// the id number is not that important because it will be handled by the frame
			VIOFeature2D feat(newPoints.at(i), oldFrame.features.at(i).getFeatureID(), i, -1); // create a matched feature with id = -1
			//if the previous feature was described
			if(oldFrame.features.at(i).isFeatureDescribed())
			{
				//ROS_DEBUG("transferring feature description");
				feat.setFeatureDescription(oldFrame.features.at(i).getFeatureDescription()); // transfer previous description to new feature
				//ROS_DEBUG_STREAM_THROTTLE(0, feat.getFeatureDescription());
			}

			newFrame.addFeature(feat); // add this feature to the new frame
		}
		else
		{
			lostFeatures++;
		}
	}

	//ROS_DEBUG_STREAM_COND(lostFeatures, "optical flow lost " << lostFeatures <<  " feature(s)");

	//if user wants to kill by similarity
	if(KILL_BY_DISSIMILARITY)
	{
		//ROS_DEBUG("killing by similarity");
		this->checkFeatureConsistency(newFrame, this->FEATURE_SIMILARITY_THRESHOLD);
	}

	return true;
}

std::vector<double> placeFeatureInSpace(cv::Point2f point1,cv::Point2f point2,cv::Mat rotation, cv::Mat translation)
						{
	std::vector<double> point3D;
	point3D.reserve(3);

						}

/*
 * gets corresponding points between the two frames as two vectors of point2f
 * checks if index and id match for saftey
 */
void VIO::getCorrespondingPointsFromFrames(Frame lastFrame, Frame currentFrame, std::vector<cv::Point2f>& lastPoints, std::vector<cv::Point2f>& currentPoints)
{

	for (int i = 0; i < currentFrame.features.size(); i++)
	{
		if(currentFrame.features.at(i).isMatched() &&
				lastFrame.features.at(currentFrame.features.at(i).getMatchedIndex()).getFeatureID() ==
						currentFrame.features.at(i).getMatchedID()){
			lastPoints.push_back(lastFrame.features.at(currentFrame.features.at(i).getMatchedIndex()).getFeature().pt);
			currentPoints.push_back(currentFrame.features.at(i).getFeature().pt);
		}
		else
		{
			ROS_WARN("could not match feature id to index");
		}
	}
}

/*
 * checks to see if current descriptor is similar to actual feature
 * if similarity is bellow threshold, feature is kept and descriptor is updated
 * otherwise feature is removed from feature vector
 */
void VIO::checkFeatureConsistency(Frame& checkFrame, int killThreshold ){
	cv::Mat newDescription = checkFrame.describeFeaturesWithBRIEF(checkFrame.image, checkFrame.features);

	std::vector<VIOFeature2D> tempFeatures;

	for (int i = 0; i < checkFrame.features.size(); i++){

		if(!checkFrame.features.at(i).isFeatureDescribed())
			break;

		cv::Mat row = newDescription.row(i);

		//ROS_DEBUG_STREAM_ONCE("got feature description " << row);

		int x = checkFrame.compareDescriptors(row, checkFrame.features.at(i).getFeatureDescription());
		//int x = checkFrame.compareDescriptors(row, row);

		if (x <= killThreshold){

			//ROS_DEBUG_STREAM("features match " << i <<" : "<<checkFrame.features.size()<<" : "<< newDescription.rows <<" : " << x);
			//ROS_DEBUG_STREAM("i+1: "<< checkFrame.features.at(i+1).getFeatureDescription()<<":"<<checkFrame.features.at(i+1).isFeatureDescribed());
			//ROS_DEBUG_STREAM("description size " << checkFrame.features.at(i).getFeatureDescription().cols);

			checkFrame.features.at(i).setFeatureDescription(row);

			//ROS_DEBUG("modified feature");

			tempFeatures.push_back(checkFrame.features.at(i));

			//ROS_DEBUG("pushed back modified feature");
		}
		else{
			ROS_DEBUG("feature does'nt match enough, killing");
		}
	}

	//ROS_DEBUG("setting new features");
	checkFrame.features = tempFeatures;
	//ROS_DEBUG("set new features");
}

/*
 * find the average change in position
 * for all feature correspondences
 * vectors must be same sizes
 */
double VIO::averageFeatureChange(std::vector<cv::Point2f> points1, std::vector<cv::Point2f> points2)
{
	double diff = 0;
	double dx, dy;
	for(int i = 0; i < points1.size(); i++)
	{
		dx = (double)(points1.at(i).x - points2.at(i).x);
		dy = (double)(points1.at(i).y - points2.at(i).y);
		diff += sqrt(pow(dx, 2) + pow(dy, 2));
	}

	return diff / (double)points1.size();
}

/*
 * broadcasts the world to odom transform
 */
void VIO::broadcastWorldToOdomTF()
{
	static tf::TransformBroadcaster br;
	tf::Transform transform;
	transform.setOrigin(tf::Vector3(this->pose.pose.position.x, this->pose.pose.position.y, this->pose.pose.position.z));
	tf::Quaternion q;
	q.setW(this->pose.pose.orientation.w);
	q.setX(this->pose.pose.orientation.x);
	q.setY(this->pose.pose.orientation.y);
	q.setZ(this->pose.pose.orientation.z);
	//ROS_DEBUG_STREAM(this->pose.pose.orientation.w << " " << this->pose.pose.orientation.x);
	transform.setRotation(q);
	br.sendTransform(tf::StampedTransform(transform, ros::Time::now(), this->world_frame, this->odom_frame));
}

/*
 * broadcasts the odom to tempIMU trans
 */
ros::Time VIO::broadcastOdomToTempIMUTF(double roll, double pitch, double yaw, double x, double y, double z)
{
	static tf::TransformBroadcaster br;
	tf::Transform transform;
	transform.setOrigin(tf::Vector3(x, y, z));
	tf::Quaternion q;
	q.setRPY(roll, pitch, yaw);
	//ROS_DEBUG_STREAM(q.getW() << ", " << q.getX() << ", " << q.getY() << ", " << q.getZ());
	transform.setRotation(q);
	ros::Time sendTime = ros::Time::now();
	br.sendTransform(tf::StampedTransform(transform, sendTime, this->camera_frame, "temp_imu_frame"));
	return sendTime;
}

/*
 * returns the certainty
 * predicts the new rotation and position of the camera.
 * transfroms it to the odometry frame
 * and publishes a pose estimate
 *
 * FOR BACK UP
 * ROS_DEBUG_STREAM("velo: " << inertialVelocityChange.getX() << ", " << inertialVelocityChange.getY() << ", " << inertialVelocityChange.getZ());
 * ROS_DEBUG_STREAM("pos: " << inertialPositionChange.getX() << ", " << inertialPositionChange.getY() << ", " << inertialPositionChange.getZ());
 * ROS_DEBUG_STREAM("angle: " << inertialAngleChange.getX() << ", " << inertialAngleChange.getY() << ", " << inertialAngleChange.getZ());
 * ROS_ASSERT(inertialVelocityChange.getX() == inertialVelocityChange.getX() && inertialAngleChange.getX() == inertialAngleChange.getX());
 */
double VIO::estimateMotion()
{
	tf::Vector3 inertialAngleChange, inertialPositionChange, inertialVelocityChange; // change in angle and pos from imu
	tf::Vector3 visualAngleChange, visualPositionChange;
	tf::Vector3 finalAngleChange(0.0, 0.0, 0.0), finalVelocityChange(0.0, 0.0, 0.0), finalPositionChange(0.0, 0.0, 0.0);
	double visualMotionCertainty;
	double averageMovement;

	tf::Vector3 lastVelocity(this->velocity.vector.x, this->velocity.vector.y, this->velocity.vector.z);
	tf::Vector3 lastAngularVelocity(this->angular_velocity.vector.x, this->angular_velocity.vector.y, this->angular_velocity.vector.z);

	// get motion estimate from the IMU
	this->getInertialMotionEstimate(this->pose.header.stamp, this->currentFrame.timeImageCreated,
			lastVelocity, lastAngularVelocity, inertialAngleChange,
			inertialPositionChange, inertialVelocityChange);

	finalAngleChange = inertialAngleChange;
	finalPositionChange = inertialPositionChange;
	finalVelocityChange = inertialVelocityChange;

	//infer motion from images
	tf::Vector3 unitVelocityInference;
	bool visualMotionInferenceSuccessful = false;

	//get motion inference from visual odometry
	visualMotionInferenceSuccessful = this->visualMotionInference(lastFrame, currentFrame, inertialAngleChange,
			visualAngleChange, unitVelocityInference, averageMovement);


	//set the time stamp of the pose to the time of current frame
	this->pose.header.stamp = this->currentFrame.timeImageCreated;

	this->assembleStateVectors(finalPositionChange, finalAngleChange, finalVelocityChange);

	this->broadcastOdomToTempIMUTF(0.0, 0.0, 0.0, unitVelocityInference.getX(), unitVelocityInference.getY(), unitVelocityInference.getZ());

}

/*
 * uses delta vectors from camera frame to alter the odom frame
 */
void VIO::assembleStateVectors(tf::Vector3 finalPositionChange, tf::Vector3 finalAngleChange, tf::Vector3 finalVelocityChange)
{
	//get the last angle in terms of tf::Quaternion - in frame ODOM
	tf::Quaternion q0(this->pose.pose.orientation.x, this->pose.pose.orientation.y,
			this->pose.pose.orientation.z, this->pose.pose.orientation.w);

	//get the last position in terms of tf::Vector3 - in frame ODOM
	tf::Vector3 r0(this->pose.pose.position.x, this->pose.pose.position.y, this->pose.pose.position.z);

	//get the last velocity in frame ODOM
	tf::Vector3 v0(this->velocity.vector.x, this->velocity.vector.y, this->velocity.vector.z);

	//create the change vectors
	tf::Stamped<tf::Vector3> r, v, a;

	//create the stamped change vectors
	tf::Stamped<tf::Vector3> r_camera, v_camera, a_camera;
	a_camera.frame_id_ = this->camera_frame;
	r_camera.frame_id_ = this->camera_frame;
	v_camera.frame_id_ = this->camera_frame;

	a_camera.stamp_ = ros::Time(0);
	r_camera.stamp_ = ros::Time(0);
	v_camera.stamp_ = ros::Time(0);

	a_camera.setData(finalAngleChange);
	r_camera.setData(finalPositionChange);
	v_camera.setData(finalVelocityChange);

	//transform the stamped vectors into odom frame
	try{
		this->tf_listener.transformVector(this->odom_frame, a_camera, a);
		this->tf_listener.transformVector(this->odom_frame, r_camera, r);
		this->tf_listener.transformVector(this->odom_frame, v_camera, v);
	}
	catch (tf::TransformException e) {
		ROS_WARN_STREAM("IN ME 1 " << e.what());
	}

	//alter update the last state
	tf::Quaternion q;
	q.setRPY(a.getX(), a.getY(), a.getZ());
	tf::Quaternion q_final = q0 * q;
	tf::Vector3 r_final = r0 + (tf::Vector3)r;
	tf::Vector3 v_final = v0 + (tf::Vector3)v;

	//update the state vectors
	this->pose.pose.orientation.w = q_final.getW();
	this->pose.pose.orientation.x = q_final.getX();
	this->pose.pose.orientation.y = q_final.getY();
	this->pose.pose.orientation.z = q_final.getZ();
	this->pose.pose.position.x = r_final.getX();
	this->pose.pose.position.y = r_final.getY();
	this->pose.pose.position.z = r_final.getZ();

	ROS_DEBUG_STREAM(this->pose);

	this->velocity.vector.x = v_final.getX();
	this->velocity.vector.y = v_final.getY();
	this->velocity.vector.z = v_final.getZ();

}

/*
 * uses epipolar geometry from two frames to
 * estimate relative motion of the frame;
 */
bool VIO::visualMotionInference(Frame frame1, Frame frame2, tf::Vector3 angleChangePrediction,
		tf::Vector3& rotationInference, tf::Vector3& unitVelocityInference, double& averageMovement)
{
	//first get the feature deltas from the two frames
	std::vector<cv::Point2f> prevPoints, currentPoints;
	this->getCorrespondingPointsFromFrames(frame1, frame2, prevPoints, currentPoints);

	//undistort points using fisheye model
	cv::fisheye::undistortPoints(prevPoints, prevPoints, this->K, this->D);
	cv::fisheye::undistortPoints(currentPoints, currentPoints, this->K, this->D);

	//get average movement bewteen images
	averageMovement = this->averageFeatureChange(prevPoints, currentPoints);

	//ensure that there are enough points to estimate motion with vo
	if(currentPoints.size() < 5)
	{
		return false;
	}

	cv::Mat mask;

	//calculate the essential matrix
	cv::Mat essentialMatrix = cv::findEssentialMat(prevPoints, currentPoints, this->K, cv::RANSAC, 0.999, 1.0, mask);

	//ensure that the essential matrix is the correct size
	if(essentialMatrix.rows != 3 || essentialMatrix.cols != 3)
	{
		return false;
	}

	//recover pose change from essential matrix
	cv::Mat translation;
	cv::Mat rotation;

	//decompose matrix to get possible deltas
	cv::recoverPose(essentialMatrix, prevPoints, currentPoints, this->K, rotation, translation, mask);


	//set the unit velocity inference
	unitVelocityInference.setX(translation.at<double>(0, 0));
	unitVelocityInference.setY(translation.at<double>(1, 0));
	unitVelocityInference.setZ(translation.at<double>(2, 0));

	return true;
}

/*
 * this gets the inertial motion estimate from the imu buffer to the specified time
 * this is gotten from the fromPose and fromVelocity at their times.
 * the results are output in the angle, pos, vel change vectors
 * it returns the number of IMU readings used
 *
 * angle change is in radians RPY
 * removes all imu messages from before the toTime
 *
 * inputs in odom frame
 * outputs in camera frame
 *
 * sets the last measured angular velocity to the state vector
 *
 * this was very poorly written sorry
 * it contained many bugs and becae very messy as a result
 *
 * NOTE:
 * this assumes a rigid transformation between the IMU - Camera - odom-base
 * do not move the imu and camera relative to each other!
 *
 * NOTE 2:
 * the fromVelocity and from Angular velocity must be in terms of the camera frame
 */
int VIO::getInertialMotionEstimate(ros::Time fromTime, ros::Time toTime, tf::Vector3 fromVelocity,
		tf::Vector3 fromAngularVelocity, tf::Vector3& angleChange,
		tf::Vector3& positionChange, tf::Vector3& velocityChange)
{
	int startingIMUBufferSize = this->imuMessageBuffer.size();

	//check if there are any imu readings
	if(this->imuMessageBuffer.size() == 0)
	{
		return 0;
	}

	tf::StampedTransform odom2camera;
	try{
		this->tf_listener.lookupTransform(this->camera_frame, this->odom_frame, ros::Time(0), odom2camera);
	}
	catch(tf::TransformException e)
	{
		ROS_WARN_STREAM("IN ME 2 " << e.what());
	}

	//transform the two from vectors
	fromAngularVelocity = odom2camera * fromAngularVelocity - odom2camera * tf::Vector3(0.0, 0.0, 0.0);
	fromVelocity = odom2camera * fromVelocity - odom2camera * tf::Vector3(0.0, 0.0, 0.0);

	//ROS_DEBUG_STREAM("ang velo: " << fromAngularVelocity.getX() << " velo: " << fromVelocity.getX());

	int startingIMUIndex, endingIMUIndex;

	/* first find the IMU reading index such that the toTime is greater than
	 * the stamp of the IMU reading.
	 */
	for(int i = this->imuMessageBuffer.size() - 1; i >= 0; i--)
	{
		if(this->imuMessageBuffer.at(i).header.stamp.toSec() < toTime.toSec())
		{
			endingIMUIndex = i;
			break;
		}
	}

	/*
	 * second find the IMU reading index such that the fromTime is lesser than the
	 * stamp of the IMU message
	 */
	//ROS_DEBUG_STREAM("buffer size " << this->imuMessageBuffer.size());
	/*
	 * this will catch the fromTime > toTime error
	 */
	if(fromTime.toNSec() < toTime.toNSec())
	{
		for(int i = 0; i < this->imuMessageBuffer.size(); i++)
		{
			if(this->imuMessageBuffer.at(i).header.stamp.toSec() > fromTime.toSec())
			{
				startingIMUIndex = i;
				break;
			}
		}
	}
	else
	{
		ROS_ERROR_STREAM("from Time is " << fromTime.toNSec() << " to time is " << toTime.toNSec() << " starting from i = 0");
		startingIMUIndex = 0;
	}

	//now we have all IMU readings between the two times.

	/*
	 * third create the corrected accelerations vector
	 * 1) remove centripetal acceleration
	 * 2) remove gravity
	 */

	tf::Vector3 dTheta(0, 0, 0);
	double piOver180 = CV_PI / 180.0;

	tf::Vector3 gravity(0.0, 0.0, this->GRAVITY_MAG); // the -gravity accel vector in the world frame gravity magnitude is variable to adjust for biases

	//get the world->odom transform
	tf::StampedTransform world2Odom;
	try{
		this->tf_listener.lookupTransform(this->odom_frame, this->world_frame, ros::Time(0), world2Odom);
	}
	catch(tf::TransformException e)
	{
		ROS_WARN_STREAM(e.what());
	}

	//get the imu->camera transform
	tf::StampedTransform imu2camera;
	try{
		this->tf_listener.lookupTransform(this->camera_frame, this->imu_frame, ros::Time(0), imu2camera);
	}
	catch(tf::TransformException e)
	{
		ROS_WARN_STREAM(e.what());
	}

	gravity = world2Odom * gravity - world2Odom * tf::Vector3(0.0, 0.0, 0.0); // rotate gravity vector into odom frame

	//ROS_DEBUG("nothing to do with tf stuff!");

	//std::vector<tf::Vector3> correctedIMUAccels;
	std::vector<tf::Vector3> cameraAccels;
	std::vector<ros::Time> cameraAccelTimes;

	//ROS_DEBUG_STREAM("starting end indexes " << startingIMUIndex << ", " << endingIMUIndex << " buffer size " << this->imuMessageBuffer.size());

	for(int i = startingIMUIndex; i <= endingIMUIndex; i++)
	{
		//get the tf::vectors for the raw IMU readings
		//ROS_DEBUG_STREAM("i value is" << i << "starting value is " << startingIMUIndex << "ending value is " << endingIMUIndex);

		if(i < 0 || i >= this->imuMessageBuffer.size())
		{
			ROS_ERROR_STREAM("i value is " << i << " starting value is " << startingIMUIndex << " ending value is " << endingIMUIndex << " vec size " << this->imuMessageBuffer.size() << " CONTINUE");
			continue;
		}

		sensor_msgs::Imu msg = this->imuMessageBuffer.at(i);
		//ROS_DEBUG("pass 1");
		tf::Vector3 omegaIMU(msg.angular_velocity.x, msg.angular_velocity.y, msg.angular_velocity.z);
		//ROS_DEBUG_STREAM("1 omega " << omegaIMU.getX());
		//convert the omega vec to rads
		omegaIMU = piOver180 * omegaIMU;
		//ROS_DEBUG_STREAM("2 omega " << omegaIMU.getX());
		double omegaIMU_mag = omegaIMU.length();

		//ROS_DEBUG_STREAM("3 omega length" << omegaIMU_mag);
		tf::Vector3 omegaIMU_hat;
		if(omegaIMU_mag != 0)
		{
			omegaIMU_hat = (1 / omegaIMU_mag) * omegaIMU;
		}
		else
		{
			omegaIMU_hat = omegaIMU;
		}

		tf::Vector3 alphaIMU(msg.linear_acceleration.x, msg.linear_acceleration.y, msg.linear_acceleration.z);

		//compute the centripetal accel
		tf::StampedTransform distFromRotationAxisTF;

		// the transformation to the CoM frame in the imu_frame
		try{
			this->tf_listener.lookupTransform(this->imu_frame, this->CoM_frame, ros::Time(0), distFromRotationAxisTF);
		}
		catch(tf::TransformException e){
			ROS_WARN_STREAM_ONCE("THIS! " << e.what());
		}

		//get the centripetal acceleration expected
		tf::Vector3 deltaR = distFromRotationAxisTF.getOrigin();
		double deltaR_mag = sqrt(pow(deltaR.getX(), 2) + pow(deltaR.getY(), 2) + pow(deltaR.getZ(), 2));
		tf::Vector3 deltaR_hat = (1 / deltaR_mag) * deltaR;
		//mag accel = omega^2 * r
		//the accel is proportional to the perpendicularity of omega and deltaR
		//ROS_DEBUG_STREAM("ca unit vecs: " << deltaR_hat.getX() << ", " << omegaIMU_hat.getX());
		double perpCoeff = deltaR_hat.cross(omegaIMU_hat).length();
		//calculate
		tf::Vector3 centripetalAccel = perpCoeff * omegaIMU_mag * omegaIMU_mag * deltaR_mag * deltaR_hat;

		//ROS_DEBUG_STREAM("ca: " << centripetalAccel.getX() << ", " << centripetalAccel.getY() << ", " << centripetalAccel.getZ() << " perp: " << perpCoeff);

		//if this is not the first iteration
		if(i != startingIMUIndex)
		{
			//get the last angular velocity
			sensor_msgs::Imu last_msg = this->imuMessageBuffer.at(i - 1);
			//ROS_DEBUG("pass 2");
			tf::Vector3 last_omegaIMU(last_msg.angular_velocity.x, last_msg.angular_velocity.y, last_msg.angular_velocity.z);
			//convert the omega vec to rads
			last_omegaIMU = piOver180 * last_omegaIMU;

			//get the new dTheta - dTheta = dTheta + omega * dt
			dTheta = dTheta + last_omegaIMU * (msg.header.stamp.toSec() - last_msg.header.stamp.toSec());
			//ROS_DEBUG_STREAM("dt: " << (msg.header.stamp.toSec() - last_msg.header.stamp.toSec()));
		}
		else
		{
			tf::Transform camera2IMU = imu2camera.inverse();

			tf::Vector3 omega = camera2IMU * (piOver180 * fromAngularVelocity) - camera2IMU * tf::Vector3(0.0, 0.0, 0.0);

			//get the new dTheta - dTheta = dTheta + omega * dt
			dTheta = dTheta + omega * (msg.header.stamp.toSec() - fromTime.toSec());
		}

		//publish the temp IMU transform
		//ROS_DEBUG_STREAM("dTheta: " << dTheta.getX() << ", " << dTheta.getY() << ", " << dTheta.getZ());

		//this->broadcastWorldToOdomTF(); // tell tf what the current world -> odom is
		//this->broadcastOdomToTempIMUTF(dTheta.getX(), dTheta.getY(), dTheta.getZ(), 0, 1, 0);

		//create a transform from odom to the tempIMU
		tf::Quaternion q;
		q.setRPY(dTheta.getX(), dTheta.getY(), dTheta.getZ());
		tf::Transform odom2TempIMU(q);

		//transform the gravity vector into the temp IMU frame
		tf::Vector3 imuGravity = odom2TempIMU * gravity - odom2TempIMU * tf::Vector3(0.0, 0.0, 0.0);

		//push the corrected acceleration to the correct accel vector

		tf::Vector3 correctedIMUAccel = (alphaIMU - imuGravity - centripetalAccel);
		//tf::Vector3 correctedIMUAccel = (alphaIMU - imuGravity);

		//transform the imu accel to the camera frame
		cameraAccels.push_back(imu2camera * correctedIMUAccel - tf::Vector3(0.0, 0.0, 0.0));
		cameraAccelTimes.push_back(msg.header.stamp);

		//output
		//ROS_DEBUG_STREAM("raw accel: " << alphaIMU.getX() << ", " << alphaIMU.getY() << ", " << alphaIMU.getZ());
		//ROS_DEBUG_STREAM("grav: " << gravity.getX() << ", " << gravity.getY() << ", " << gravity.getZ());
		//ROS_DEBUG_STREAM("ca: " << centripetalAccel.getX() << ", " << centripetalAccel.getY() << ", " << centripetalAccel.getZ() << " perp: " << perpCoeff);
		//ROS_DEBUG_STREAM("trans grav: " << imuGravity.getX() << ", " << imuGravity.getY() << ", " << imuGravity.getZ());
		//ROS_DEBUG_STREAM("corrected accels: " << correctedIMUAccel.getX() << ", " << correctedIMUAccel.getY() << ", " << correctedIMUAccel.getZ());
		//ROS_DEBUG_STREAM("camera accels: " << cameraAccels.at(cameraAccels.size()-1).getX() << ", " << cameraAccels.at(cameraAccels.size()-1).getY() << ", " << cameraAccels.at(cameraAccels.size()-1).getZ());
	}

	//finish the dTheta for the last time segment
	sensor_msgs::Imu lastMsg = this->imuMessageBuffer.at(endingIMUIndex);
	tf::Vector3 omega(lastMsg.angular_velocity.x, lastMsg.angular_velocity.y, lastMsg.angular_velocity.z);

	dTheta = dTheta + (piOver180 * omega) * (toTime.toSec() - lastMsg.header.stamp.toSec());

	/*
	 * transform and set the last measured
	 * omega into the odom frame
	 */
	tf::Stamped<tf::Vector3> w;
	tf::Stamped<tf::Vector3> w_odom;
	w.setData(piOver180 * omega);
	w.stamp_ = ros::Time(0);
	w.frame_id_ = this->imu_frame;
	try{
		this->tf_listener.transformVector(this->odom_frame, w, w_odom);
	}
	catch(tf::TransformException e){
		ROS_WARN_STREAM(e.what());
	}
	this->angular_velocity.vector.x = w_odom.getX();
	this->angular_velocity.vector.y = w_odom.getY();
	this->angular_velocity.vector.z = w_odom.getZ();
	this->angular_velocity.header.frame_id = this->odom_frame;
	this->angular_velocity.header.stamp = lastMsg.header.stamp;


	//ROS_DEBUG_STREAM("dTheta: " << dTheta.getX() << ", " << dTheta.getY() << ", " << dTheta.getZ());
	//ROS_DEBUG_STREAM("time diff " << currentFrame.timeImageCreated.toSec() - this->imuMessageBuffer.at(endingIMUIndex).header.stamp.toSec());

	/*
	 * at this point we have a dTheta from inside the IMU's frame and correct accelerations from inside the camera's frame
	 * now we must transform the dTheta into the camera frame
	 * we must also integrate the accels inside the camera frame
	 */

	angleChange = imu2camera * dTheta - imu2camera * tf::Vector3(0.0, 0.0, 0.0); //set the angle change vector

	//INTEGRATE the cameraAccels

	positionChange = tf::Vector3(0.0, 0.0, 0.0); //0,0,0 initial pos
	velocityChange = fromVelocity; // initial velocity (subtract out fromVelocity at end!!!)

	if(cameraAccels.size() == 0) // if there are not accelerations
	{
		double dt = toTime.toSec() - fromTime.toSec();
		positionChange += velocityChange * dt;
	}
	else // integrate all time segments
	{
		double dt = toTime.toSec() - cameraAccelTimes.at(0).toSec();
		positionChange += velocityChange * dt;
		for(int i = 0; i < cameraAccels.size(); i++)
		{
			if(i != cameraAccels.size() - 1) // if this is not the last element
			{
				double dt = cameraAccelTimes.at(i + 1).toSec() - cameraAccelTimes.at(i).toSec();
				positionChange += 0.5 * cameraAccels.at(i) * (dt * dt) + velocityChange * dt;
				velocityChange += cameraAccels.at(i) * dt;
			}
			else
			{
				double dt = toTime.toSec() - cameraAccelTimes.at(i).toSec();
				positionChange += 0.5 * cameraAccels.at(i) * (dt * dt) + velocityChange * dt;
				velocityChange += cameraAccels.at(i) * dt;
			}
		}
	}

	//remove from velocity from the velo Change
	velocityChange = velocityChange - fromVelocity;

	//finally once everything has been estimated remove all IMU messages from the buffer that have been used and before that
	//ROS_ASSERT(this->imuMessageBuffer.size() == startingIMUBufferSize);

	std::vector<sensor_msgs::Imu> newIMUBuffer;
	for(int i = endingIMUIndex + 1; i < startingIMUBufferSize; i++)
	{
		newIMUBuffer.push_back(this->imuMessageBuffer.at(i));
	}

	this->imuMessageBuffer = newIMUBuffer; //erase all old IMU messages


	return endingIMUIndex - startingIMUIndex + 1;
}


