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

ofxImageSequenceVideo::~ofxImageSequenceVideo(){

	//wait for all threads to end
	for(int i = tasks.size() - 1; i >= 0; i--){
		std::future_status status = tasks[i].wait_for(std::chrono::microseconds(0));
		while(status != std::future_status::ready){
			ofSleepMillis(5);
			status = tasks[i].wait_for(std::chrono::microseconds(0));
		}
	}
}

void ofxImageSequenceVideo::setup(int bufferSize, int numThreads){
	this->numBufferFrames = bufferSize;
	this->numThreads = numThreads;
}

void ofxImageSequenceVideo::loadImageSequence(const string & path, float frameRate){

	bool ok = false;
	ofDirectory dir;
	dir.allowExt("tga");
	dir.allowExt("jpeg");
	dir.allowExt("jpg");
	dir.allowExt("jp2");
	dir.allowExt("bmp");
	dir.allowExt("png");
	dir.allowExt("tiff");
	dir.allowExt("tif");
	dir.allowExt("ppm");
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
	}

	bool pixelsAreReady = CURRENT_FRAME_ALT[currentFrame].state == THREAD_FINISHED_LOADING;

	if(!tex.isAllocated() && pixelsAreReady){ //allocate tex if not allocated || if img size is different
		ofPixels & pix = CURRENT_FRAME_ALT[currentFrame].pixels;
		if(tex.getWidth() != pix.getWidth() || tex.getHeight() != pix.getHeight()){
			tex.allocate(pix, false);
		}
	}

	if(tex.isAllocated() && pixelsAreReady){
		tex.loadData(CURRENT_FRAME_ALT[currentFrame].pixels);
		CURRENT_FRAME_ALT[currentFrame].state = LOADED;
		newData = true;
	}

	PixelState state = CURRENT_FRAME_ALT[currentFrame].state;

	//note we hold playback until pixels are ready (instead of dropping frames)
	if(playback && (state == THREAD_FINISHED_LOADING|| state == LOADED) &&
	   (frameOnScreenTime >= frameDuration) &&
	   (shouldLoop || (!shouldLoop && (currentFrame <= (numFrames - 1))))
	   ){
		frameOnScreenTime -= frameDuration;
		advanceFrameInternal();
	}

	handleLooping();
	handleThreadCleanup();
	handleThreadSpawn();

	int numLoaded = 0;
	for(int i = 0; i < numBufferFrames; i++){
		auto state = CURRENT_FRAME_ALT[(currentFrame + i)%numFrames].state;
		if(state == THREAD_FINISHED_LOADING || state == LOADED) numLoaded++;
	}
	bufferFullness = ofLerp(bufferFullness,(numLoaded / float(numBufferFrames)), 0.1);

}

void ofxImageSequenceVideo::handleLooping(){

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
}

void ofxImageSequenceVideo::handleThreadCleanup(){
	//handle thread spawning / cleaning – if any thread is avialable, put it to work
	for(int i = tasks.size() - 1; i >= 0; i--){

		//see if thread is done, gather results and remove from vector
		std::future_status status = tasks[i].wait_for(std::chrono::microseconds(0));
		if(status == std::future_status::ready){
			LoadResults results = tasks[i].get();
			loadTimeAvg = ofLerp(loadTimeAvg, results.elapsedTime, 0.1);
			//ofLogNotice("ofxImageSequenceVideo") << ofGetFrameNum() << " - frame loaded! " << frame;
			tasks.erase(tasks.begin() + i);
		}
	}
}


void ofxImageSequenceVideo::handleThreadSpawn(){
	int numToSpawn = numThreads - tasks.size();
	int frameToLoad = currentFrame;
	int furthestFrame = currentFrame + numBufferFrames;

	if(bufferFullness > 0.75){ //dont overspawn if we have enought data already
		numToSpawn = ofClamp(numToSpawn, 0, 1);
	}

	for(int i = 0; i < numToSpawn; i++ ){
		//look for a frame that needs loading
		while(CURRENT_FRAME_ALT[frameToLoad%numFrames].state != NOT_LOADED ){
			frameToLoad++;
		}
		if( frameToLoad < furthestFrame ){ //if the frame is within the buffer zone, spawn a thread to load it
			int moduloFrameToLoad = frameToLoad%numFrames;
			//ofLogNotice("ofxImageSequenceVideo") << ofGetFrameNum() << " - spawn thread to load frame " << moduloFrameToLoad;
			CURRENT_FRAME_ALT[moduloFrameToLoad].state = LOADING;
			tasks.push_back( std::async(std::launch::async, &ofxImageSequenceVideo::loadFrameThread, this, moduloFrameToLoad) );
		}
	}
}


ofxImageSequenceVideo::LoadResults ofxImageSequenceVideo::loadFrameThread(int frame){
	uint64_t t = ofGetElapsedTimeMicros();
	ofLoadImage(CURRENT_FRAME_ALT[frame].pixels, CURRENT_FRAME_ALT[frame].filePath);
	CURRENT_FRAME_ALT[frame].state = THREAD_FINISHED_LOADING;
	t = ofGetElapsedTimeMicros() - t;
	LoadResults results;
	results.elapsedTime = t / 1000.0f;
	results.frame = frame;
	return results;
}


void ofxImageSequenceVideo::eraseAllPixelCache(){

	for(int i = 0; i < numFrames; i++){
		if(CURRENT_FRAME_ALT[i].state == THREAD_FINISHED_LOADING || CURRENT_FRAME_ALT[i].state == LOADED){
			CURRENT_FRAME_ALT[i].pixels.clear();
			CURRENT_FRAME_ALT[i].state = NOT_LOADED;
		}
	}
}

void ofxImageSequenceVideo::eraseOutOfBufferPixelCache(){

	for(int i = 0; i < currentFrame; i++){
		if(CURRENT_FRAME_ALT[i].state == THREAD_FINISHED_LOADING || CURRENT_FRAME_ALT[i].state == LOADED){
			CURRENT_FRAME_ALT[i].pixels.clear();
			CURRENT_FRAME_ALT[i].state = NOT_LOADED;
		}
	}

	for(int i = currentFrame + numBufferFrames; i < numFrames; i++){
		if(CURRENT_FRAME_ALT[i].state == THREAD_FINISHED_LOADING || CURRENT_FRAME_ALT[i].state == LOADED){
			CURRENT_FRAME_ALT[i].pixels.clear();
			CURRENT_FRAME_ALT[i].state = NOT_LOADED;
		}
	}
}

void ofxImageSequenceVideo::drawDebug(float x, float y, float w){
	if(!loaded) return;

	ofPushMatrix();
	ofTranslate(x, y);
	float step = w / CURRENT_FRAME_ALT.size();
	float sw = step * 0.7;
	float pad = step - sw;
	float h = sw;

	ofSetColor(255,128);
	ofDrawRectangle(step * currentFrame, -h, step * numBufferFrames, h * 3 );
	ofSetColor(0);
	ofDrawRectangle(0, - h * 0.25, w, h * 1.5);
	ofSetColor(255);

	for(int i = 0; i < numFrames; i++){
		switch (CURRENT_FRAME_ALT[i].state) {
			case NOT_LOADED: ofSetColor(99); break;
			case LOADING: ofSetColor(255,255,0); break; //yellow
			case THREAD_FINISHED_LOADING: ofSetColor(0,255,0); break; //green
			case LOADED: ofSetColor(255,0,255); break; //magenta
		}
		ofDrawRectangle(pad * 0.5f + i * step, 0, sw, h);
	}

	string msg;
	msg += "frame: " + ofToString(currentFrame) + "/" + ofToString(numFrames);
	msg += "\nnum Tasks: " + ofToString(tasks.size()) + "/" + ofToString(numThreads);

	ofSetColor(255,0,0);
	float triangleH = MAX(h, 10);
	float xx = step * (currentFrame + 0.5);
	ofDrawTriangle(xx, 0, xx + triangleH * 0.5f, -triangleH, xx - triangleH * 0.5f, -triangleH);

	msg += "\nBuffer: " + ofToString(100 * bufferFullness, 1) + "%";
	msg += "\nloadTimeAvg: " + ofToString(loadTimeAvg, 2) + "ms";
	ofDrawBitmapStringHighlight(msg, 0, 16 + h * 1.25);
	ofPopMatrix();

	ofSetColor(255);
}


void ofxImageSequenceVideo::advanceFrameInternal(){
	if(!loaded) return;
	if((CURRENT_FRAME_ALT[currentFrame].state == THREAD_FINISHED_LOADING || CURRENT_FRAME_ALT[currentFrame].state == LOADED) &&
	   CURRENT_FRAME_ALT[currentFrame].pixels.isAllocated()){ //unload old pixels
		CURRENT_FRAME_ALT[currentFrame].state = NOT_LOADED;
		CURRENT_FRAME_ALT[currentFrame].pixels.clear();
	}
	currentFrame++;
	if(!shouldLoop && currentFrame == numFrames -1){
		EventInfo info;
		ofNotifyEvent(eventMovieEnded, info, this);
	}
}

void ofxImageSequenceVideo::advanceOneFrame(){
	if(!loaded) return;
	advanceFrameInternal();
	handleLooping();
	eraseOutOfBufferPixelCache();
}

void ofxImageSequenceVideo::play(){
	if(!loaded) return;
	playback = true;
}


void ofxImageSequenceVideo::pause(){
	if(!loaded) return;
	playback = false;
}


void ofxImageSequenceVideo::setPosition(float normalizedPos){
	if(!loaded) return;
	currentFrame = ofClamp(normalizedPos,0,1) * (numFrames-1);
	frameOnScreenTime = 0;
	eraseOutOfBufferPixelCache();
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


void ofxImageSequenceVideo::setLoop(bool loop){
	shouldLoop = loop;
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
