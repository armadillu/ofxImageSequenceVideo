//
//  ofxImageSequenceVideo.cpp
//  BasicSketch
//
//  Created by Oriol Ferrer Mesià on 28/02/2018.
//
//

#include "ofxImageSequenceVideo.h"
#include "ofxTimeMeasurements.h"

#if defined( TARGET_OSX ) || defined( TARGET_LINUX )
	#include <getopt.h>
	#include <dirent.h>
#endif


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



static const char *get_filename_extension(const char *filename) {
	const char *dot = strrchr(filename, '.');
	if(!dot || dot == filename) return "";
	return dot + 1;
}


void ofxImageSequenceVideo::loadImageSequence(const string & path, float frameRate){

	DIR *dir2;
	struct dirent *ent;

	vector<string> fileNames;
	string fullPath = ofToDataPath(path,true);

	if ((dir2 = opendir(fullPath.c_str()) ) != NULL) {

		while ((ent = readdir (dir2)) != NULL) {
			const char * ext = get_filename_extension(ent->d_name);
			if ( strcmp( ext, "tga") == 0 ||
				strcmp( ext, "jpeg") == 0 ||
				strcmp( ext, "jpg") == 0 ||
				strcmp( ext, "jp2") == 0 ||
				strcmp( ext, "bmp") == 0 ||
				strcmp( ext, "png") == 0 ||
				strcmp( ext, "tif") == 0 ||
				strcmp( ext, "tiff") == 0
				){
				fileNames.push_back(string(ent->d_name));
			}
		}
		closedir(dir2);
	}

	std::sort(fileNames.begin(), fileNames.end());
	ofLogNotice("ofxImageSequenceVideo") << "loadImageSequence: \"" << path << "\"";

	int num = fileNames.size();
	if(num >= 2){
		loaded = true;
		imgSequencePath = path;
		numFrames = num;
		frameDuration = 1.0 / frameRate;
		currentFrame = 0;
		frameOnScreenTime = -1; //force a data load!
		newData = false;
		tex.clear();

		//move to the next data struct
		currentFrameSet++;
		if(currentFrameSet >= maxFramePingPongDataStructs ) currentFrameSet = 0;

		CURRENT_FRAME_ALT.clear();
		CURRENT_FRAME_ALT.resize(num);
		for(int i = 0; i < num; i++){
			//CURRENT_FRAME_ALT[i].filePath = dir.getPath(i);
			CURRENT_FRAME_ALT[i].filePath = path + "/" + fileNames[i];
			//ofLogNotice("ofxImageSequenceVideo") << CURRENT_FRAME_ALT[i].filePath;
		}
		if(numThreads > 0){
			handleThreadSpawn();
		}
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

	if(numThreads > 0){ //async mode - spawn threads to load frames in the future and wait for them to be done / sync

		bool pixelsAreReady = CURRENT_FRAME_ALT[currentFrame].state == THREAD_FINISHED_LOADING;

		if(pixelsAreReady){ //1st update() call in which the pixels are available - load to GPU and change state to LOADED
			if(shouldLoadTexture){
				tex.loadData(CURRENT_FRAME_ALT[currentFrame].pixels);
			}
			CURRENT_FRAME_ALT[currentFrame].state = LOADED;
			newData = true;
		}

		PixelState state = CURRENT_FRAME_ALT[currentFrame].state;

		//note we hold playback until pixels are ready (instead of dropping frames)
		if(playback && (state == THREAD_FINISHED_LOADING|| state == LOADED) &&
		   (frameOnScreenTime >= frameDuration || frameOnScreenTime < 0.0f) &&
		   (shouldLoop || (!shouldLoop && (currentFrame <= (numFrames - 1))))
		   ){
			handleScreenTimeCounters(dt);
			advanceFrameInternal();
		}

		handleLooping(true);
		handleThreadCleanup();
		handleThreadSpawn();

		int numLoaded = 0;
		for(int i = 0; i < numBufferFrames; i++){
			auto state = CURRENT_FRAME_ALT[(currentFrame + i)%numFrames].state;
			if(state == THREAD_FINISHED_LOADING || state == LOADED) numLoaded++;
		}
		bufferFullness = ofLerp(bufferFullness,(numLoaded / float(numBufferFrames)), 0.1);

	}else{ //immediate mode, we load what we need on demand on the main frame blocking

		if(playback && (frameOnScreenTime >= frameDuration || frameOnScreenTime < 0.0f) &&
		   (shouldLoop || (!shouldLoop && (currentFrame <= (numFrames - 1))))
		   ){
			handleScreenTimeCounters(dt);
			int oldFrame = currentFrame;
			advanceFrameInternal();
			handleLooping(true);
			if(oldFrame != currentFrame){ //data is not new if we are not looping and we are stuck in the last frame
				newData = true;
				loadPixelsNow(currentFrame, oldFrame);
			}
		}

		if(texNeedsLoad && shouldLoadTexture){
			texNeedsLoad = false;
			TS_START("immediate load pix GPU");
			tex.loadData(currentPixels);
			TS_STOP("immediate load pix GPU");
		}
	}
}


void ofxImageSequenceVideo::setPlaybackFramerate(float framerate){
	frameDuration = 1.0f / framerate;
}

void ofxImageSequenceVideo::handleLooping(bool triggerEvents){

	if(shouldLoop){ //loop movie
		if(currentFrame >= numFrames){
			currentFrame = 0;
			if(triggerEvents){
				EventInfo info;
				ofNotifyEvent(eventMovieLooped, info, this);
			}
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


void ofxImageSequenceVideo::handleScreenTimeCounters(float dt){
	if(frameOnScreenTime >= 0.0f){
		frameOnScreenTime -= frameDuration;
	}else{
		frameOnScreenTime = dt;
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
	#if defined(USE_TURBO_JPEG)
	string extension = ofToLower(ofFilePath::getFileExt(CURRENT_FRAME_ALT[frame].filePath));
	if(extension == "jpeg" || extension == "jpg"){
		ofxTurboJpeg jpeg;
		jpeg.load(CURRENT_FRAME_ALT[frame].pixels, CURRENT_FRAME_ALT[frame].filePath);
	}else{
		ofLoadImage(CURRENT_FRAME_ALT[frame].pixels, CURRENT_FRAME_ALT[frame].filePath);
	}
	#else
		ofLoadImage(CURRENT_FRAME_ALT[frame].pixels, CURRENT_FRAME_ALT[frame].filePath);
	#endif
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

	int start = 0;
	if(currentFrame + numBufferFrames > numFrames){
		start = numBufferFrames - (numFrames - currentFrame);
	}

	for(int i = start; i < currentFrame; i++){
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

	if(numThreads > 0){  //draw buffer zone
		ofSetColor(255,128);
		int numAhead = numBufferFrames;
		//see if buffer hits the end of the clip - we need to wrap then
		if(currentFrame + numBufferFrames > numFrames) numAhead = numFrames - currentFrame;
		ofDrawRectangle(step * currentFrame, -h, step * numAhead, h * 3 );
		if(numAhead != numBufferFrames) ofDrawRectangle(0, -h, step * (numBufferFrames - numAhead), h * 3 );
		ofSetColor(0);
		ofDrawRectangle(0, - h * 0.25, w, h * 1.5);
		ofSetColor(255);
	}

	for(int i = 0; i < numFrames; i++){
		switch (CURRENT_FRAME_ALT[i].state) {
			case NOT_LOADED: ofSetColor(99); break;
			case LOADING: ofSetColor(255,255,0); break; //yellow
			case THREAD_FINISHED_LOADING: ofSetColor(0,255,0); break; //green
			case LOADED: ofSetColor(255,0,255); break; //magenta
		}
		ofDrawRectangle(pad * 0.5f + i * step, 0, sw, h);
	}

	string msg = numThreads == 0 ? "Mode: Immediate" : "Mode: Async";
	msg += "\nFrame: " + ofToString(currentFrame) + "/" + ofToString(numFrames);
	if(numThreads > 0) msg += "\nNumTasks: " + ofToString(tasks.size()) + "/" + ofToString(numThreads);

	ofSetColor(255,0,0);
	float triangleH = MAX(h, 10);
	float xx = step * (currentFrame + 0.5);
	ofDrawTriangle(xx, 0, xx + triangleH * 0.5f, -triangleH, xx - triangleH * 0.5f, -triangleH);

	if(numThreads > 0) msg += "\nBuffer: " + ofToString(100 * bufferFullness, 1) + "%";
	msg += "\nLoadTimeAvg: " + ofToString(loadTimeAvg, 2) + "ms";
	msg += "\nRes: " + ofToString(tex.getWidth(),0) + " x " + ofToString(tex.getHeight(),0);
	ofDrawBitmapStringHighlight(msg, 0, 16 + h * 1.25);
	ofPopMatrix();

	ofSetColor(255);
}


void ofxImageSequenceVideo::advanceFrameInternal(){
	if(!loaded) return;
	if(numThreads > 0){
		if((CURRENT_FRAME_ALT[currentFrame].state == THREAD_FINISHED_LOADING || CURRENT_FRAME_ALT[currentFrame].state == LOADED) &&
		   CURRENT_FRAME_ALT[currentFrame].pixels.isAllocated()){ //unload old pixels
			CURRENT_FRAME_ALT[currentFrame].state = NOT_LOADED;
			CURRENT_FRAME_ALT[currentFrame].pixels.clear();
		}
	}
	currentFrame++;
	if(!shouldLoop && currentFrame == numFrames -1){
		EventInfo info;
		ofNotifyEvent(eventMovieEnded, info, this);
	}
}

void ofxImageSequenceVideo::advanceOneFrame(){
	if(!loaded) return;
	int oldFrame = currentFrame;
	advanceFrameInternal();
	handleLooping(false);
	if(numThreads > 0){
		eraseOutOfBufferPixelCache();
	}else{ //immediate mode - load frame right here
		loadPixelsNow(currentFrame, oldFrame);
	}
}

void ofxImageSequenceVideo::play(){
	if(!loaded) return;
	playback = true;
}


void ofxImageSequenceVideo::pause(){
	if(!loaded) return;
	playback = false;
}

void ofxImageSequenceVideo::loadPixelsNow(int newFrame, int oldFrame){
	if(currentFrame != oldFrame || (!tex.isAllocated() && shouldLoadTexture)){
		uint64_t t = ofGetElapsedTimeMicros();

		CURRENT_FRAME_ALT[oldFrame].state = NOT_LOADED;
		TS_START("immediate load pix");
		#if defined(USE_TURBO_JPEG)
		string extension = ofToLower(ofFilePath::getFileExt(CURRENT_FRAME_ALT[newFrame].filePath));
		if(extension == "jpeg" || extension == "jpg"){
			ofxTurboJpeg jpeg;
			jpeg.load(currentPixels, CURRENT_FRAME_ALT[newFrame].filePath);
		}else{
			ofLoadImage(currentPixels, CURRENT_FRAME_ALT[newFrame].filePath);
		}
		#else
		ofLoadImage(currentPixels, CURRENT_FRAME_ALT[newFrame].filePath); //load pixels from disk
		#endif
		TS_STOP("immediate load pix");
		CURRENT_FRAME_ALT[newFrame].state = LOADED;
		loadTimeAvg = ofLerp(loadTimeAvg, (ofGetElapsedTimeMicros() - t) / 1000.0f, 0.1);
		texNeedsLoad = true;
	}
}

void ofxImageSequenceVideo::setPosition(float normalizedPos){
	if(!loaded) return;
	int oldFrame = currentFrame;
	currentFrame = ofClamp(normalizedPos,0,1) * (numFrames-1);
	frameOnScreenTime = 0;
	if(numThreads > 0){
		eraseOutOfBufferPixelCache();
	}else{
		loadPixelsNow(currentFrame, oldFrame);
	}
}


int ofxImageSequenceVideo::getCurrentFrame(){
	if(!loaded) return -1;
	return currentFrame;
}

float ofxImageSequenceVideo::getPosition(){
	if(!loaded) return -1;
	return (float(currentFrame) / (numFrames-1));
}


int ofxImageSequenceVideo::getNumFrames(){
	if(!loaded) return -1;
	return numFrames;
}


bool ofxImageSequenceVideo::arePixelsNew(){
	if(!loaded) return false;
	else return newData;
}


void ofxImageSequenceVideo::setLoop(bool loop){
	shouldLoop = loop;
}

void ofxImageSequenceVideo::seekToFrame(int frame){
	if(!loaded) return;
	int oldFrame = ofClamp(currentFrame, 0, numFrames-1);
	currentFrame = ofClamp(frame, 0, numFrames-1);
	frameOnScreenTime = 0;
	if(numThreads > 0){
		eraseOutOfBufferPixelCache();
	}else{
		loadPixelsNow(currentFrame, oldFrame);
	}
}


ofPixels& ofxImageSequenceVideo::getPixels(){
	static ofPixels pix;
	if(!loaded) return pix;
	else{
		if(numThreads > 0){
			bool pixelsAreReady =
				CURRENT_FRAME_ALT[currentFrame].state == THREAD_FINISHED_LOADING ||
				CURRENT_FRAME_ALT[currentFrame].state == LOADED;
			if(pixelsAreReady){
				return CURRENT_FRAME_ALT[currentFrame].pixels;
			}else{
				return pix;
			}
		}else{
			return currentPixels;
		}
	}
}


ofTexture& ofxImageSequenceVideo::getTexture(){

	static ofTexture nulltex;
	if(!loaded) return nulltex;

	return tex;
}


#undef CURRENT_FRAME_ALT
