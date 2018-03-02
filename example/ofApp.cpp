#include "ofApp.h"


void ofApp::setup(){

	CustomApp::setup();

	// LISTENERS
	ofAddListener(screenSetup.setupChanged, this, &ofApp::setupChanged);
	ofAddListener(RUI_GET_OF_EVENT(), this, &ofApp::remoteUIClientDidSomething);


	// PARAMS
	RUI_NEW_GROUP("PARAMS");
	RUI_SHARE_PARAM(drawDebug);

	int numThreads = 2;
	int buffer = 10; //MAX(1.5 * numThreads, 8);
	float framerate = 60;

	vector<string> videoNames = {
		"TGA_compressed_imgSequence",
		"JPEG_imgSequence",
		"TGA_imgSequence",
		//"PNG_imgSequence",
		//"JPG2000_imgSequence",
		//"TIFF_imgSequence",
		//"PPM_imgSequence"
	};

	for(const auto & n : videoNames){
		VideoUnit * v = new VideoUnit();
		v->name = n;
		v->video.setup(buffer, numThreads);
		v->video.loadImageSequence(v->name, framerate);
		v->video.play();
		videos.push_back(v);
	}
}


void ofApp::update(){

	float dt = 1./60.;
	TSGL_START("videos update");

	for(auto & v : videos){
		TS_START(v->name);
		v->video.update(dt);
		TS_STOP(v->name);
	}
	TSGL_STOP("videos update");
}


void ofApp::draw(){

	int n = videos.size();
	int columns = (int)sqrt(n);
	int rows = (int)ceil(n / (float)columns);

	float gridW = ofGetWidth() / columns;
	float gridH = ofGetHeight() / rows;
	float pad = 20;

	int c = 0;
	for(auto & v : videos){
		int i = (c % columns);
		int j = floor(c / columns);
		float x = i * gridW + pad * 0.5;
		float y = j * gridH + pad * 0.5;
		TS_START(v->name);
		v->video.getTexture().draw(x,y, gridW - pad, gridH - pad);
		float margin = 10;
		if(drawDebug) v->video.drawDebug(x + margin, y + margin, gridW - pad - 2 * margin);
		TS_STOP(v->name);
		ofDrawBitmapStringHighlight( v->name, x, y  + gridH - 1 * pad);
		if(c == selectedVideo){
			ofNoFill();
			int lineW = 5;
			ofSetLineWidth(lineW);
			ofSetColor(255,0,0);
			ofDrawRectangle(i * gridW + lineW, j * gridH + lineW, gridW - 2 * lineW, gridH - 2 * lineW);
			ofSetColor(255);
			ofSetLineWidth(1);
			ofFill();
		}
		c++;
	}
}


void ofApp::keyPressed(int key){

	if(key == 'w'){
		screenSetup.cycleToNextScreenMode();
	}

	int i = selectedVideo;
	if(key == '1'){
		videos[i]->video.play();
	}

	if(key == '2'){
		videos[i]->video.pause();
	}

	if(key == '3'){
		videos[i]->video.advanceOneFrame();
	}

	if(key == '4'){
		float pos = ofRandom(1);
		videos[i]->video.setPosition(pos);
	}

	if(key == '0'){
		videos[i]->video.eraseAllPixelCache();
	}

	if(key == OF_KEY_RIGHT || key == OF_KEY_DOWN){
		selectedVideo ++;
		if(selectedVideo > videos.size()-1) selectedVideo = 0;
	}

	if(key == OF_KEY_LEFT || key == OF_KEY_UP){
		selectedVideo--;
		if(selectedVideo < 0) selectedVideo = videos.size()-1;
	}
}


void ofApp::keyReleased(int key){

}


void ofApp::mouseMoved(int x, int y ){

}


void ofApp::mouseDragged(int x, int y, int button){

}


void ofApp::mousePressed(int x, int y, int button){

}


void ofApp::mouseReleased(int x, int y, int button){

}


void ofApp::windowResized(int w, int h){

}


void ofApp::gotMessage(ofMessage msg){

}


void ofApp::dragEvent(ofDragInfo dragInfo){
	
}


//////// CALLBACKS //////////////////////////////////////

void ofApp::setupChanged(ofxScreenSetup::ScreenSetupArg &arg){
	ofLogNotice()	<< "ofxScreenSetup setup changed from " << screenSetup.stringForMode(arg.oldMode)
	<< " (" << arg.oldWidth << "x" << arg.oldHeight << ") "
	<< " to " << screenSetup.stringForMode(arg.newMode)
	<< " (" << arg.newWidth << "x" << arg.newHeight << ")";
}


//define a callback method to get notifications of client actions
void ofApp::remoteUIClientDidSomething(RemoteUIServerCallBackArg &arg){
	switch (arg.action) {
		case CLIENT_CONNECTED: cout << "CLIENT_CONNECTED" << endl; break;
		case CLIENT_DISCONNECTED: cout << "CLIENT_DISCONNECTED" << endl; break;
		case CLIENT_UPDATED_PARAM: cout << "CLIENT_UPDATED_PARAM: "<< arg.paramName << " - ";
			arg.param.print();
			break;
		case CLIENT_DID_SET_PRESET: cout << "CLIENT_DID_SET_PRESET" << endl; break;
		case CLIENT_SAVED_PRESET: cout << "CLIENT_SAVED_PRESET" << endl; break;
		case CLIENT_DELETED_PRESET: cout << "CLIENT_DELETED_PRESET" << endl; break;
		case CLIENT_SAVED_STATE: cout << "CLIENT_SAVED_STATE" << endl; break;
		case CLIENT_DID_RESET_TO_XML: cout << "CLIENT_DID_RESET_TO_XML" << endl; break;
		case CLIENT_DID_RESET_TO_DEFAULTS: cout << "CLIENT_DID_RESET_TO_DEFAULTS" << endl; break;
		default:
			break;
	}
}
