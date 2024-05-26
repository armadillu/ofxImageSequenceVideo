#pragma once

#include "ofMain.h"
#include "CustomApp.h"
#include "ofxImageSequenceVideo.h"

class ofApp : public CustomApp, public ofThread{

public:
	void setup();
	void update();
	void draw();
	void exit();

	void keyPressed(int key);
	void keyReleased(int key);
	void mouseMoved(int x, int y );
	void mouseDragged(int x, int y, int button);
	void mousePressed(int x, int y, int button);
	void mouseReleased(int x, int y, int button);
	void windowResized(int w, int h);
	void dragEvent(ofDragInfo dragInfo);
	void gotMessage(ofMessage msg);


	// APP CALLBACKS ////////////////////////////////////////

	void setupChanged(ofxScreenSetup::ScreenSetupArg &arg);
	void remoteUIClientDidSomething(RemoteUIServerCallBackArg & arg);


	// APP SETUP ////////////////////////////////////////////

	struct VideoUnit{
		string name;
		ofxImageSequenceVideo video;
	};

	vector<VideoUnit*> videos;
	int selectedVideo = 0;
	bool drawDebug;

	void threadedFunction();

	std::mutex mutex;
	ofPixels threadPixels;

};
