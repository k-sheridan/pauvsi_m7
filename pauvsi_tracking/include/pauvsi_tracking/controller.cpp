#include "controller.hpp"

Controller::Controller()
{
	redObjectTrackers[0] = new Tracker(CAMERA_TOPIC_1, CAMERA_FRAME_1, REDILOWHUE, REDIHIGHHUE,
								REDILOWSATURATION, REDIHIGHSATURATION, REDILOWVALUE, REDIHIGHVALUE);
	redObjectTrackers[1] = new Tracker(CAMERA_TOPIC_2, CAMERA_FRAME_2, REDILOWHUE, REDIHIGHHUE,
								REDILOWSATURATION, REDIHIGHSATURATION, REDILOWVALUE, REDIHIGHVALUE);
	redObjectTrackers[2] = new Tracker(CAMERA_TOPIC_3, CAMERA_FRAME_3, REDILOWHUE, REDIHIGHHUE,
								REDILOWSATURATION, REDIHIGHSATURATION, REDILOWVALUE, REDIHIGHVALUE);
	redObjectTrackers[3] = new Tracker(CAMERA_TOPIC_4, CAMERA_FRAME_4, REDILOWHUE, REDIHIGHHUE,
								REDILOWSATURATION, REDIHIGHSATURATION, REDILOWVALUE, REDIHIGHVALUE);
	redObjectTrackers[4] = new Tracker(CAMERA_TOPIC_5, CAMERA_FRAME_5, REDILOWHUE, REDIHIGHHUE,
								REDILOWSATURATION, REDIHIGHSATURATION, REDILOWVALUE, REDIHIGHVALUE);

	greenObjectTrackers[0] = new Tracker(CAMERA_TOPIC_1, CAMERA_FRAME_1, GREENILOWHUE, GREENIHIGHHUE,
										GREENILOWSATURATION, GREENIHIGHSATURATION, GREENILOWVALUE, GREENIHIGHVALUE);
	greenObjectTrackers[1] = new Tracker(CAMERA_TOPIC_2, CAMERA_FRAME_2, GREENILOWHUE, GREENIHIGHHUE,
										GREENILOWSATURATION, GREENIHIGHSATURATION, GREENILOWVALUE, GREENIHIGHVALUE);
	greenObjectTrackers[2] = new Tracker(CAMERA_TOPIC_3, CAMERA_FRAME_3, GREENILOWHUE, GREENIHIGHHUE,
										GREENILOWSATURATION, GREENIHIGHSATURATION, GREENILOWVALUE, GREENIHIGHVALUE);
	greenObjectTrackers[3] = new Tracker(CAMERA_TOPIC_4, CAMERA_FRAME_4, GREENILOWHUE, GREENIHIGHHUE,
										GREENILOWSATURATION, GREENIHIGHSATURATION, GREENILOWVALUE, GREENIHIGHVALUE);
	greenObjectTrackers[4] = new Tracker(CAMERA_TOPIC_5, CAMERA_FRAME_5, GREENILOWHUE, GREENIHIGHHUE,
										GREENILOWSATURATION, GREENIHIGHSATURATION, GREENILOWVALUE, GREENIHIGHVALUE);

}
