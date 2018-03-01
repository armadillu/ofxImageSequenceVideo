//
//  ofxImageSequenceVideo.cpp
//  BasicSketch
//
//  Created by Oriol Ferrer Mesià on 28/02/2018.
//
//

#include "ofxImageSequenceVideo.h"

#define CURRENT_FRAME_ALT frames[currentFrameSet]

ofxImageSequenceVideo::ofxImageSequenceVideo(){}


void ofxImageSequenceVideo::setup(int bufferSize, int numThreads){
	this->numBufferFrames = bufferSize;
	this->numThreads = numThreads;
}

void ofxImageSequenceVideo::loadImageSequence(const string & path, float frameRate){

	bool ok = false;
	ofDirectory dir;
	dir.allowExt("tga");
	dir.allowExt("jpeg");
	dir.allowExt("bmp");
	dir.allowExt("jpg");
	dir.allowExt("tiff");
	dir.allowExt("tif");
	dir.sort();
	int num = dir.listDir(path);

	if(num >= 2){
		loaded = true;
		imgSequencePath = path;
		numFrames = num;
		frameDuration = 1.0 / frameRate;
		currentFrame = 0;
		frameOnScreenTime = 0.0f;
		newData = false;
		texFrameData = -1;
		tex.clear();

		//move to the next data struct
		currentFrameSet++;
		if(currentFrameSet >= maxFramePingPingDataStructs ) currentFrameSet = 0;

		CURRENT_FRAME_ALT.clear();
		CURRENT_FRAME_ALT.resize(num);
		for(int i = 0; i < num; i++){
			CURRENT_FRAME_ALT[i].filePath = dir.getPath(i);
		}
		handleThreadSpawn();
	}else{
		loaded = false;
	}
}



void ofxImageSequenceVideo::update(float dt){

	if(!loaded) return;

	newData = false;

	if(playback){

		if(currentFrame < 0) currentFrame = 0;
		frameOnScreenTime += dt;

		if(!tex.isAllocated()){ //allocate tex if not allocated || if img size is different
			if(CURRENT_FRAME_ALT[currentFrame].state == LOADED){
				ofPixels & pix = CURRENT_FRAME_ALT[currentFrame].pixels;
				if(tex.getWidth() != pix.getWidth() || tex.getHeight() != pix.getHeight()){
					tex.allocate(pix);
				}
			}
		}

		if(tex.isAllocated() && CURRENT_FRAME_ALT[currentFrame].state == LOADED){
			if(texFrameData != currentFrame){ //if pixels are ready and tex doesn't have their data loaded in, do so
				tex.loadData(CURRENT_FRAME_ALT[currentFrame].pixels);
				texFrameData = currentFrame;
			}
		}

		if((frameOnScreenTime >= frameDuration) && (shouldLoop ||
													(!shouldLoop && (currentFrame <= (numFrames - 1)))
													)){
			frameOnScreenTime -= frameDuration;

			CURRENT_FRAME_ALT[currentFrame].state = NOT_LOADED;
			if(CURRENT_FRAME_ALT[currentFrame].pixels.isAllocated()){ //unload old pixels
				CURRENT_FRAME_ALT[currentFrame].pixels.clear();
			}
			currentFrame++;
			newData = true;
			if(!shouldLoop && currentFrame == numFrames -1){
				EventInfo info;
				ofNotifyEvent(eventMovieEnded, info, this);
			}
		}
	}

	if(shouldLoop){ //loop movie
		if(currentFrame >= numFrames){
			currentFrame = 0;
			EventInfo info;
			ofNotifyEvent(eventMovieLooped, info, this);
		}
	}else{ //movie stops at last frame
		if(currentFrame >= numFrames){
			currentFrame = numFrames - 1;
		}
	}

	handleThreadCleanup();
	handleThreadSpawn();
}

void ofxImageSequenceVideo::handleThreadCleanup(){
	//handle thread spawning / cleaning – if any thread is avialable, put it to work
	for(int i = tasks.size() - 1; i >= 0; i--){

		//see if thread is done, gather results and remove from vector
		std::future_status status = tasks[i].wait_for(std::chrono::microseconds(0));
		if(status == std::future_status::ready){
			int frame = tasks[i].get();
			//ofLogNotice("ofxImageSequenceVideo") << ofGetFrameNum() << " - frame loaded! " << frame;
			tasks.erase(tasks.begin() + i);
		}
	}
}


void ofxImageSequenceVideo::handleThreadSpawn(){
	int numToSpawn = numThreads - tasks.size();
	int frameToLoad = currentFrame;
	int furthestFrame = currentFrame + numBufferFrames;
	for(int i = 0; i < numToSpawn; i++ ){
		//look for a frame that needs loading
		while(CURRENT_FRAME_ALT[frameToLoad%numFrames].state != NOT_LOADED ){
			frameToLoad++;
		}
		if( frameToLoad < furthestFrame ){ //if the frame is within the buffer zone, spawn a thread to load it
			int moduloFrameToLoad = frameToLoad%numFrames;
			//ofLogNotice("ofxImageSequenceVideo") << ofGetFrameNum() << " - spawn thread to load frame " << moduloFrameToLoad;
			CURRENT_FRAME_ALT[moduloFrameToLoad].state = LOADING;
			tasks.push_back( std::async(std::launch::async, &ofxImageSequenceVideo::loadFrameThread, this, moduloFrameToLoad));
		}
	}
}


int ofxImageSequenceVideo::loadFrameThread(int frame){
	//ofSleepMillis(10);
	ofLoadImage(CURRENT_FRAME_ALT[frame].pixels, CURRENT_FRAME_ALT[frame].filePath);
	CURRENT_FRAME_ALT[frame].state = LOADED;
	return frame;
}


void ofxImageSequenceVideo::drawDebug(float x, float y, float w){
	if(!loaded) return;

	ofPushMatrix();
	ofTranslate(x, y);
	float step = w / CURRENT_FRAME_ALT.size();
	float sw = step * 0.7;
	float h = 10;

	for(int i = 0; i < numFrames; i++){
		switch (CURRENT_FRAME_ALT[i].state) {
			case NOT_LOADED: ofSetColor(100); break;
			case LOADING: ofSetColor(255,255,0); break;
			case LOADED: ofSetColor(0,255,0); break;
		}
		ofDrawRectangle(i * step, 0, sw, h);
	}

	ofSetColor(255,0,0);
	ofDrawTriangle(step * (currentFrame + 0.5), 0, step * (currentFrame), -h, step * (currentFrame + 1), -h);

	ofSetColor(128,64);
	ofDrawRectangle(step * currentFrame, -h * 0.3, step * numBufferFrames, h * 1.6);
	ofSetColor(255);
	string msg = "num Tasks: " + ofToString(tasks.size()) + "/" + ofToString(numThreads);
	ofDrawBitmapStringHighlight(msg, 0, h * 3);
	ofPopMatrix();

}


void ofxImageSequenceVideo::play(){
	if(!loaded) return;
	playback = true;
}


void ofxImageSequenceVideo::pause(){
	if(!loaded) return;
	playback = false;
}



void ofxImageSequenceVideo::setPosition(){
	if(!loaded) return;
}



int ofxImageSequenceVideo::getCurrentFrameNum(){
	if(!loaded) return -1;
}


int ofxImageSequenceVideo::getNumFrames(){
	if(!loaded) return -1;
}



bool ofxImageSequenceVideo::arePixelsNew(){

	if(!loaded) return false;
}



ofPixels& ofxImageSequenceVideo::getPixels(){
	static ofPixels pix;
	if(!loaded) return pix;
}


ofTexture& ofxImageSequenceVideo::getTexture(){

	static ofTexture nulltex;
	if(!loaded) return nulltex;

	return tex;
}


#undef CURRENT_FRAME_ALT
