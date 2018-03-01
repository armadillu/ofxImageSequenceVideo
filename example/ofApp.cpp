#include "ofApp.h"


void ofApp::setup(){

	CustomApp::setup();

	// LISTENERS
	ofAddListener(screenSetup.setupChanged, this, &ofApp::setupChanged);
	ofAddListener(RUI_GET_OF_EVENT(), this, &ofApp::remoteUIClientDidSomething);


	// PARAMS
	RUI_NEW_GROUP("PARAMS");
	RUI_SHARE_PARAM(p1, -1, 1);

	int numThreads = 2;
	int buffer = 3 * numThreads;
	video.setup(buffer, numThreads);
	video.loadImageSequence("Bouncing_Ball_v2_SOURCE_imgSequence", 60);
	video.play();
}


void ofApp::update(){

	float dt = 1./60.;
	video.update(dt);
}


void ofApp::draw(){

	ofTexture & tex = video.getTexture();
	if(tex.isAllocated()){
		tex.draw(0,0, ofGetWidth(), ofGetHeight());
	}
	video.drawDebug(20, 20, ofGetWidth() - 40);
}


void ofApp::keyPressed(int key){

	if(key == 'w'){
		screenSetup.cycleToNextScreenMode();
	}

	if(key == '1'){
		video.play();
	}

	if(key == '2'){
		video.pause();
	}

	if(key == '3'){
		video.advanceOneFrame();
	}

	if(key == '4'){
		video.setPosition(ofRandom(1));
	}

	if(key == '0'){
		video.eraseAllPixelCache();
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
