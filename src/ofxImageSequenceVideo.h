//
//  ofxImageSequenceVideo.h
//  BasicSketch
//
//  Created by Oriol Ferrer Mesià on 28/02/2018.
//
//

#pragma once
#include "ofMain.h"
#include <future>

class ofxImageSequenceVideo{

public:

	static const int maxFramePingPingDataStructs = 10;

	ofxImageSequenceVideo();
	~ofxImageSequenceVideo();

	void setup(int bufferSize, int numThreads = std::thread::hardware_concurrency());

	void loadImageSequence(const string & path, float frameRate);

	void update(float dt);

	void play();
	void pause();

	void advanceOneFrame();

	void setPosition(float normalizedPos);
	void setLoop(bool loop);

	int getCurrentFrameNum();
	int getNumFrames();

	bool arePixelsNew();

	void eraseAllPixelCache();

	ofPixels& getPixels();
	ofTexture& getTexture();

	struct EventInfo{
		ofVec2f movieTexSize;
		string movieFile;
		float frameScreenTime; // 1/framerate
	};

	ofFastEvent<EventInfo> eventMovieLooped;
	ofFastEvent<EventInfo> eventMovieEnded;

	void drawDebug(float x, float y, float w);

protected:

	enum PixelState{
		NOT_LOADED,
		LOADING,
		THREAD_FINISHED_LOADING,
		LOADED
	};

	struct FrameInfo{
		string filePath;
		ofPixels pixels;
		PixelState state = NOT_LOADED;
	};

	bool loaded = false;
	string imgSequencePath;

	void advanceFrameInternal();
	void handleThreadCleanup();
	void handleThreadSpawn();

	void handleLooping();

	int currentFrame = 0;
	float frameOnScreenTime = 0;

	int numFrames = 0;
	float frameDuration = 0; //1.0f/framerate
	bool newData = false; //keeps track of state of pixels THIS FRAME

	bool playback = false;
	bool shouldLoop = true;

	vector<FrameInfo> frames[maxFramePingPingDataStructs];
	int currentFrameSet = -1;
	//this weird thing is to do frames structures ping-poing so that when we load a new
	//video, we can safely move to a new struct and leave the running threads do its thing on the
	//old struct, without having ot worry about destroying the memory they are working on

	ofTexture tex;

	vector<std::future<int>> tasks; //store thread futures

	int numBufferFrames = 8;
	int numThreads = 3;

	int loadFrameThread(int frame);

	void eraseOutOfBufferPixelCache();
};

