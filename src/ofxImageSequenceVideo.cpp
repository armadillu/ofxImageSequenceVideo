//
//  ofxImageSequenceVideo.cpp
//  BasicSketch
//
//  Created by Oriol Ferrer Mesià on 28/02/2018.
//
//

#include "ofxImageSequenceVideo.h"
#include "ofxTimeMeasurements.h"
#include "../lib/stb/stb_image.h"

#if defined( TARGET_OSX ) || defined( TARGET_LINUX )
	#include <getopt.h>
	#include <dirent.h>
#else
	#include <dirent_vs.h>
#endif


#define CURRENT_FRAME_ALT frames[currentFrameSet]

//commenting this out to avoid duploicate symobl errors with other addons that use stb_image.
//if no other addon is including stb_image, you may need to add this to your project to include the stb_image implementation.
//#define STB_IMAGE_IMPLEMENTATION
//#include "../lib/stb/stb_image.h"


void ofxImageSequenceVideo::getImageInfo(const std::string & filePath, int & width, int & height, int & numChannels, bool & imgOK){
	std::string path = ofToDataPath(filePath, true);
	int ret = stbi_info(path.c_str(), &width, &height, &numChannels);
	imgOK = (ret != 0);
	if(!imgOK){
		ofLogError("ofxApp::utils") << "getImageDimensions() failed for image \"" << filePath << "\"";
		ofPixels pix;
		bool loadOK = ofLoadImage(pix, filePath);
		if(loadOK){
			width = pix.getWidth();
			height = pix.getHeight();
			imgOK = true;
			numChannels = pix.getNumPlanes();
		}
	}
}


ofxImageSequenceVideo::ofxImageSequenceVideo(){}

ofxImageSequenceVideo::~ofxImageSequenceVideo(){

	//wait for all threads to end
	for(int i = tasks.size() - 1; i >= 0; i--){
		std::future_status status = tasks[i].wait_for(std::chrono::microseconds(0));
		while(status != std::future_status::ready){
			ofSleepMillis(1);
			status = tasks[i].wait_for(std::chrono::microseconds(0));
		}
	}
}


void ofxImageSequenceVideo::setup(int numThreads, int bufferSize, bool useDXTcompression, bool _reverse){
	this->numBufferFrames = bufferSize;
	this->numThreads = numThreads;
	this->keepTexturesInGpuMem = false;
	this->useDXTCompression = useDXTcompression;
    this->reverse = _reverse;
}



static const char *get_filename_extension(const char *filename) {
	const char *dot = strrchr(filename, '.');
	if(!dot || dot == filename) return "";
	return dot + 1;
}


vector<string> ofxImageSequenceVideo::getImagesAtDirectory(const string & path, bool useDxtCompression){

	DIR *dir2;
	struct dirent *ent;
	vector<string> fileNames;
	string fullPath = ofToDataPath(path,true);

	const auto imageTypes = ofxImageSequenceVideo::getSupportedImageTypes();

	if ((dir2 = opendir(fullPath.c_str()) ) != NULL) {

		while ((ent = readdir (dir2)) != NULL) {
			string ext = string(get_filename_extension(ent->d_name));
			bool isVisible = ent->d_name[0] != '.';
			bool isCompatibleType;
			if(!useDxtCompression){
				isCompatibleType = std::find(imageTypes.begin(), imageTypes.end(), ext ) != imageTypes.end();
			}else{
				isCompatibleType = ext == "dxt";
			}
			if ( isVisible && isCompatibleType ){
				fileNames.push_back(string(ent->d_name));
			}
		}
		closedir(dir2);
	}
	std::sort(fileNames.begin(), fileNames.end());
	return fileNames;
}


bool ofxImageSequenceVideo::loadImageSequence(const string & path, float frameRate){


	vector<string> fileNames = ofxImageSequenceVideo::getImagesAtDirectory(path, useDXTCompression);

	ofLogNotice("ofxImageSequenceVideo") << "loadImageSequence() \"" << path << "\"";

	int num = fileNames.size();
	if(num >= 2){
		loaded = true;
		imgSequencePath = path;
		numFrames = num;
		frameDuration = 1.0 / frameRate;
		currentFrame = 0;
		frameOnScreenTime = -1; //force a data load!
		newData = false;
		if(shouldLoadTexture){
			tex.clear();
		}

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
		return true;
	}else{
		loaded = false;
		ofLogError("ofxImageSequenceVideo") << "can't loadImageSequence()! Not enough image files in \"" << path << "\"";
		return false;
	}
}

float ofxImageSequenceVideo::getMovieDuration(){

	float ret = 0;
	if(numFrames > 0){
		ret = frameDuration * numFrames;
	}
	return ret;
}

size_t ofxImageSequenceVideo::getEstimatdVramUse(){

	if(currentFrameSet < 0){
		ofLogError("ofxImageSequenceVideo") << "can't getEstimatdVramUse() because no image sequence was loaded!";
		return 0;
	}

	if(frames[currentFrameSet].size()){

		if(CURRENT_FRAME_ALT[0].state == PixelState::LOADED){
			auto & pix = CURRENT_FRAME_ALT[0].pixels;
			return pix.getWidth() * pix.getHeight() * pix.getNumPlanes() * (size_t)numFrames;
		}
		if(CURRENT_FRAME_ALT[0].texState == TextureState::LOADED){
			auto & tex = CURRENT_FRAME_ALT[0].texture;
			size_t numChannels = ofGetNumChannelsFromGLFormat(tex.getTextureData().glInternalFormat);
			return tex.getWidth() * tex.getHeight() * numChannels * (size_t)numFrames;
		}
		if(!useDXTCompression){
			int w, h, nChannels;
			bool ok;
			ofxImageSequenceVideo::getImageInfo(CURRENT_FRAME_ALT[0].filePath, w, h, nChannels, ok);
			if(ok){
				return (size_t)numFrames * (size_t)w * (size_t)h * (size_t)nChannels;
			}else{
				ofLogError("ofxImageSequenceVideo") << "Can't getEstimatdVramUse(). cant load image! " << CURRENT_FRAME_ALT[0].filePath;
				return 0;
			}
		}else{
			ofxDXT::Data data;
			bool ok = ofxDXT::loadFromDisk(CURRENT_FRAME_ALT[0].filePath, data);
			if(ok){
				size_t bytes;
				if (data.getCompressionType() == ofxDXT::DXT1){
					//dxt1 ratio is 6:1
					bytes = (size_t)((size_t)numFrames * (size_t)data.getWidth() * (size_t)data.getHeight() * 4) / (size_t)6;
				}else{
					//DXT3 & 5 ratios 4:1
					bytes = (size_t)numFrames * (size_t)data.getWidth() * (size_t)data.getHeight();
				}

				return bytes;
			}
			ofLogError("ofxImageSequenceVideo") << "Can't getEstimatdVramUse(). cant load DXT image! " << CURRENT_FRAME_ALT[0].filePath;
			return 0;
		}
	}else{
		ofLogError("ofxImageSequenceVideo") << "Can't getEstimatdVramUse(). Load an image sequence first!";
		return 0;
	}
}

void ofxImageSequenceVideo::update(float dt){

	if(!loaded) return;

	newData = false;

	if(playback){
        if(currentFrame < 0)
        {
            currentFrame = 0;
            reversing = false;
        }
		frameOnScreenTime += dt* playbackSpeed;
	}

	if(numThreads > 0){ //async mode - spawn threads to load frames in the future and wait for them to be done / sync

		FrameInfo & curFrame = CURRENT_FRAME_ALT[currentFrame];

		bool pixelsAreReady = curFrame.state == PixelState::THREAD_FINISHED_LOADING;

		if(pixelsAreReady){ //1st update() call in which the pixels are available - load to GPU and change state to LOADED

			if(shouldLoadTexture){

				if(curFrame.texState == TextureState::NOT_LOADED){

//					TS_SCOPE("load 2 GPU");

					if(keepTexturesInGpuMem){ //load into frames vector
						//TS_START_ACC("load tex KEEP");
						if(!useDXTCompression){
							curFrame.texture.loadData(curFrame.pixels);
						}else{
							ofxDXT::loadDataIntoTexture(curFrame.compressedPixels, curFrame.texture);
						}
						//TS_STOP_ACC("load tex KEEP");
						curFrame.texState = TextureState::LOADED;
					}else{ //load into reusable texture
						//TS_START_ACC("load tex ONE-OFF");
						if(!useDXTCompression){
							tex.loadData(curFrame.pixels);
						}else{
							ofxDXT::loadDataIntoTexture(curFrame.compressedPixels, tex);
						}
						//TS_STOP_ACC("load tex ONE-OFF");
					}
				}
			}
			curFrame.state = PixelState::LOADED;
			newData = true;
		}

		PixelState state = CURRENT_FRAME_ALT[currentFrame].state;

		bool timeToAdvanceFrame = (frameOnScreenTime >= frameDuration || frameOnScreenTime < 0.0f);
		bool pixelsReady = (state == PixelState::THREAD_FINISHED_LOADING|| state == PixelState::LOADED);
		bool loop = (shouldLoop || (!shouldLoop && (currentFrame <= (numFrames - 1))));
		bool isTextureReady = CURRENT_FRAME_ALT[currentFrame].texState == TextureState::LOADED;

		//note we hold playback until pixels are ready (instead of dropping frames - otherwise things get very complicated)
		if(playback && timeToAdvanceFrame && loop && (pixelsReady || isTextureReady)){
			handleScreenTimeCounters(dt);
			advanceFrameInternal();
		}

		handleLooping(true);
		handleThreadCleanup();
		handleThreadSpawn();

		//update buffer statistics
		int numLoaded = 0;
		for(int i = 0; i < numBufferFrames; i++){
			auto state = CURRENT_FRAME_ALT[(currentFrame + i)%numFrames].state;
			auto texState = CURRENT_FRAME_ALT[(currentFrame + i)%numFrames].texState;
			if(state == PixelState::THREAD_FINISHED_LOADING || state == PixelState::LOADED || texState == TextureState::LOADED){
				numLoaded++;
			}
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
			TS_START_ACC("load pix GPU");

			if(!useDXTCompression){
				tex.loadData(currentPixels);
			}else{
				ofxDXT::loadDataIntoTexture(currentPixelsCompressed, tex);
			}
			TS_STOP_ACC("load pix GPU");
		}
	}
}


void ofxImageSequenceVideo::setPlaybackFramerate(float framerate){
	frameDuration = 1.0f / framerate;
}

void ofxImageSequenceVideo::handleLooping(bool triggerEvents){

	if(shouldLoop){ //loop movie
		if(currentFrame >= numFrames){
			
            if(reverse)
            {
                currentFrame = numFrames - 1;
                reversing = true;
            }
            else
            {
                currentFrame = 0;
            }
            
            
			if(triggerEvents){
				EventInfo info;
				info.who = this;
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
    
    if(playback & currentFrame < 0){
        currentFrame = 0;
        reversing = false;
    }
    
	int numToSpawn = numThreads - tasks.size();
	int frameToLoad = currentFrame;
	int furthestFrame = currentFrame + numBufferFrames;

	if(bufferFullness > 0.75 && numBufferFrames < CURRENT_FRAME_ALT.size()){ //dont overspawn if we have enough data already - unless we are trying to load the whole sequence
		numToSpawn = ofClamp(numToSpawn, 0, 1);
	}

	bool fullyLoaded = false;
	for(int i = 0; i < numToSpawn; i++ ){
		//look for a frame that needs loading
		int numChecked = 0;
		while(CURRENT_FRAME_ALT[frameToLoad%numFrames].state != PixelState::NOT_LOADED ){
			frameToLoad++;
			numChecked++;
			if(numChecked >= numFrames){
				fullyLoaded = true;
				break;
			}
		}
		if( !fullyLoaded && frameToLoad < furthestFrame ){ //if the frame is within the buffer zone, spawn a thread to load it
			int moduloFrameToLoad = frameToLoad%numFrames;
			//ofLogNotice("ofxImageSequenceVideo") << ofGetFrameNum() << " - spawn thread to load frame " << moduloFrameToLoad;
			//if keeping textures in mem, dont spawn thread to load pixels if textures are already there
			if( !keepTexturesInGpuMem || (keepTexturesInGpuMem && CURRENT_FRAME_ALT[moduloFrameToLoad].texState != TextureState::LOADED)){
				CURRENT_FRAME_ALT[moduloFrameToLoad].state = PixelState::LOADING;
				tasks.push_back( std::async(std::launch::async, &ofxImageSequenceVideo::loadFrameThread, this, moduloFrameToLoad) );
			}
		}
	}
}


ofxImageSequenceVideo::LoadResults ofxImageSequenceVideo::loadFrameThread(int frame){

	uint64_t t = ofGetElapsedTimeMicros();
	FrameInfo & curFrame = CURRENT_FRAME_ALT[frame];
	if(!useDXTCompression){
		#if defined(USE_TURBO_JPEG)
		string extension = ofToLower(ofFilePath::getFileExt(curFrame.filePath));
		if(extension == "jpeg" || extension == "jpg"){
			ofxTurboJpeg jpeg;
			jpeg.load(curFrame.pixels, curFrame.filePath);
		}else{
			ofLoadImage(curFrame.pixels, curFrame.filePath);
		}
		#else
			ofLoadImage(curFrame.pixels, curFrame.filePath);
		#endif
	}else{
		ofxDXT::loadFromDisk(curFrame.filePath, curFrame.compressedPixels);
	}
	curFrame.state = PixelState::THREAD_FINISHED_LOADING;
	t = ofGetElapsedTimeMicros() - t;
	LoadResults results;
	results.elapsedTime = t / 1000.0f;
	results.frame = frame;
	return results;
}


void ofxImageSequenceVideo::eraseAllPixelCache(){

	for(int i = 0; i < numFrames; i++){
		FrameInfo & curFrame = CURRENT_FRAME_ALT[i];
		if(curFrame.state == PixelState::THREAD_FINISHED_LOADING || curFrame.state == PixelState::LOADED){
			curFrame.pixels.clear();
			//curFrame.compressedPixels.clear(); //note that because ofBuffer internally holds a vector, even if you
												//clear the ofBuffer, the vector class keeps its "capacity" allocation
												//which means it will not release its RAM. That's why we destroy the obj
												//alltogether
			curFrame.compressedPixels = ofxDXT::Data();
			curFrame.state = PixelState::NOT_LOADED;
		}
	}
}

void ofxImageSequenceVideo::eraseAllTextureCache(){

	for(int i = 0; i < numFrames; i++){
		FrameInfo & curFrame = CURRENT_FRAME_ALT[i];
		if(curFrame.texState == TextureState::LOADED ){
			curFrame.texture.clear();
			curFrame.texState = TextureState::NOT_LOADED;
		}
	}
}

void ofxImageSequenceVideo::eraseOutOfBufferPixelCache(){

	int start = 0;
	if(currentFrame + numBufferFrames > numFrames){
		start = numBufferFrames - (numFrames - currentFrame);
	}

	for(int i = start; i < currentFrame; i++){
		FrameInfo & curFrame = CURRENT_FRAME_ALT[i];
		if(curFrame.state == PixelState::THREAD_FINISHED_LOADING || curFrame.state == PixelState::LOADED){
			curFrame.pixels.clear();
			curFrame.compressedPixels = ofxDXT::Data(); //clear pixels data
			curFrame.state = PixelState::NOT_LOADED;
		}
	}

	for(int i = currentFrame + numBufferFrames; i < numFrames; i++){
		FrameInfo & curFrame = CURRENT_FRAME_ALT[i];
		if(curFrame.state == PixelState::THREAD_FINISHED_LOADING || curFrame.state == PixelState::LOADED){
			curFrame.pixels.clear();
			curFrame.compressedPixels = ofxDXT::Data(); //clear pixels data
			curFrame.state = PixelState::NOT_LOADED;
		}
	}
}


std::string ofxImageSequenceVideo::getStatus(){

	if(!loaded) return "";

	string msg = numThreads == 0 ? "Mode: Immediate" : "Mode: Async";
	msg += "\nFrame: " + ofToString(currentFrame) + "/" + ofToString(numFrames);
	msg += "\nPlaybackSpeed: " + ofToString(100 * playbackSpeed) + "%";

	msg += "\nMovieDuration: " + secondsToHumanReadable(getMovieDuration(), 2);
	if(numThreads > 0) msg += "\nNumTasks: " + ofToString(tasks.size()) + "/" + ofToString(numThreads);

	if(numThreads > 0) msg += "\nBuffer: " + ofToString(100 * bufferFullness, 1) + "% [" + ofToString(numBufferFrames) + "]";
	msg += "\nLoadTimeAvg: " + ofToString(loadTimeAvg, 2) + "ms";
	msg += "\nFrameRate: " + ofToString(1.0 / frameDuration, 2) + "fps";
	auto & texture = getTexture();
	msg += "\nRes: " + ofToString(texture.getWidth(),0) + " x " + ofToString(texture.getHeight(),0);
	msg += "\nKeepInGPU: " + string(keepTexturesInGpuMem ? "YES" : "FALSE");
	msg += "\nDXT Compressed: " + string(useDXTCompression ? "YES" : "FALSE");

	return msg;
}


std::string ofxImageSequenceVideo::getBufferStatus(){

	string msg = "[";
	if(numThreads > 0 ){  //buffer only for threaded mode

		int numAhead = numBufferFrames;
		//see if buffer hits the end of the clip - we need to wrap then
		if(currentFrame + numBufferFrames > numFrames) numAhead = numFrames - currentFrame;
		vector<int> framesToTest;
		for(int i = 0; i < numBufferFrames; i++){
			framesToTest.push_back((currentFrame + i) % numFrames);
		}

		for(auto & frameNum : framesToTest){
			switch (CURRENT_FRAME_ALT[frameNum].state) {
				case PixelState::NOT_LOADED: 					msg += "0"; break;
				case PixelState::LOADING: 						msg += "!"; break;
				case PixelState::THREAD_FINISHED_LOADING: 		msg += "#"; break;
				case PixelState::LOADED: 						msg += "#"; break;
			}
		}
	}
	msg += "]";
	return msg;
}


void ofxImageSequenceVideo::drawDebug(float x, float y, float w){
	if(!loaded) return;

	ofMesh m;

	m.setMode(OF_PRIMITIVE_POINTS);

	ofPushMatrix();
	ofTranslate(x, y);

	float step = w / CURRENT_FRAME_ALT.size();
	float sw = step * 0.7;
	float pad = step - sw;
	float h = sw;

	glPointSize( MAX(sw * 1.25, 1.0) ); //TODO!

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

	ofColor c;
	for(int i = 0; i < numFrames; i++){
		switch (CURRENT_FRAME_ALT[i].state) {
			case PixelState::NOT_LOADED: c = ofColor(99); break; //gray
			case PixelState::LOADING: c = ofColor(255,255,0); break; //yellow
			case PixelState::THREAD_FINISHED_LOADING: c = ofColor(0,255,0); break; //green
			case PixelState::LOADED: c = ofColor(255,0,255); break; //magenta
		}
		
		//ofDrawRectangle(pad * 0.5f + i * step, 0, sw, h);
		m.addColor(c);
		float x = pad * 0.5f + i * step + sw * 0.5f;
		m.addVertex( glm::vec3(x, 0, 0) );

		switch (CURRENT_FRAME_ALT[i].texState) {
			case TextureState::NOT_LOADED: c = ofColor(255, 100, 100); break;
			case TextureState::LOADED: c = ofColor(30,190,200); break; //magenta
		}
		m.addColor(c);
		m.addVertex( glm::vec3(x, sw + 2, 0) );
	}

	m.draw();

	ofSetColor(255,0,0);
	float triangleH = MAX(h, 10);
	float xx = step * (currentFrame + 0.5);
	ofDrawTriangle(xx, 0, xx + triangleH * 0.5f, -triangleH, xx - triangleH * 0.5f, -triangleH);

	ofPopMatrix();
	ofSetColor(255);
}


void ofxImageSequenceVideo::advanceFrameInternal(){
	if(!loaded) return;

	if(numThreads > 0){ //ASYNC
		auto & curFrame = CURRENT_FRAME_ALT[currentFrame];
		bool loaded = (curFrame.state == PixelState::THREAD_FINISHED_LOADING || curFrame.state == PixelState::LOADED);
		if(loaded && (curFrame.pixels.isAllocated() || curFrame.compressedPixels.size() > 0)){ //unload old pixels
			curFrame.state = PixelState::NOT_LOADED;
			curFrame.pixels.clear();
			curFrame.compressedPixels = ofxDXT::Data(); //clear pixels data
		}
	}

    if(reversing)
    {
        currentFrame--;
    }
    else
    {
        currentFrame++;
    }
	

	if(!shouldLoop && currentFrame == numFrames -1){
		EventInfo info;
		info.who = this;
        playback = false;
		ofNotifyEvent(eventMovieEnded, info, this);
        //last frame, stop playback
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

bool ofxImageSequenceVideo::isPlaying(){
    return playback;
}

void ofxImageSequenceVideo::loadPixelsNow(int newFrame, int oldFrame){

	if(newFrame < 0 || newFrame >= numFrames){
		ofLogError("ofxImageSequenceVideo") << "cant load frame! out of bounds!";
		return;
	}
	if(currentFrame != oldFrame || (!tex.isAllocated() && shouldLoadTexture)){
		uint64_t t = ofGetElapsedTimeMicros();

		if(oldFrame >= 0){
			CURRENT_FRAME_ALT[oldFrame].state = PixelState::NOT_LOADED;
		}
		auto & newFrameData = CURRENT_FRAME_ALT[newFrame];
		if(!useDXTCompression){
			#if defined(USE_TURBO_JPEG)
			string extension = ofToLower(ofFilePath::getFileExt(newFrameData.filePath));
			if(extension == "jpeg" || extension == "jpg"){
				//TS_START_ACC("load jpg disk");
				ofxTurboJpeg jpeg;
				jpeg.load(currentPixels, newFrameData.filePath);
				//TS_STOP_ACC("load jpg disk");
			}else{
				//TS_START_ACC("load pix disk");
				ofLoadImage(currentPixels, newFrameData.filePath);
				//TS_STOP_ACC("load pix disk");
			}
			#else
			//TS_START_ACC("load pix disk");
			ofLoadImage(currentPixels, newFrameData.filePath); //load pixels from disk
			//TS_STOP_ACC("load pix disk");
			#endif
		}else{
			ofxDXT::loadFromDisk(newFrameData.filePath, currentPixelsCompressed);
		}
		newFrameData.state = PixelState::LOADED;
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


void ofxImageSequenceVideo::setPositionSeconds(float seconds){
	if(!loaded) return;
	setPosition(seconds / getMovieDuration());
}


int ofxImageSequenceVideo::getCurrentFrame(){
	if(!loaded) return -1;
	return currentFrame;
}

float ofxImageSequenceVideo::getPosition(){
	if(!loaded) return -1;
	return (float(currentFrame) / (numFrames-1));
}

float ofxImageSequenceVideo::getPositionSeconds(){
	if (!loaded) return -1;
	return getPosition() * getMovieDuration();
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
	if(frameOnScreenTime < 0.0) oldFrame = -1; //we just loaded a new sequence, we have no valid frame data!
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
			auto & curFrame = CURRENT_FRAME_ALT[currentFrame];
			if(curFrame.state == PixelState::THREAD_FINISHED_LOADING || curFrame.state == PixelState::LOADED){
				return curFrame.pixels;
			}else{
				return pix;
			}
		}else{
			return currentPixels;
		}
	}
}


bool ofxImageSequenceVideo::areAllTexturesPreloaded(){

	if(!keepTexturesInGpuMem) return false;
	else{
		for(auto & f : frames[currentFrameSet]){
			if(f.texState == TextureState::NOT_LOADED) return false;
		}
		return true;
	}
}

ofTexture& ofxImageSequenceVideo::getTexture(){

	static ofTexture nulltex;
	if(!loaded) return nulltex;

	if(numThreads == 0){ //inmedate mode
		return tex;
	}else{
		if(keepTexturesInGpuMem){
			if(CURRENT_FRAME_ALT[currentFrame].texState == TextureState::LOADED){
				return CURRENT_FRAME_ALT[currentFrame].texture;
			}else{
				int prevFrame = currentFrame - 1;
				if (prevFrame < 0 ) prevFrame = numFrames - 1;
				if(CURRENT_FRAME_ALT[prevFrame].texState == TextureState::LOADED){
					return CURRENT_FRAME_ALT[prevFrame].texture;
				}else{
					return nulltex;
				}
			}
		}else{
			return tex;
		}
	}
}

std::string ofxImageSequenceVideo::secondsToHumanReadable(float secs, int decimalPrecision){
	std::string ret;
	if (secs < 60.0f ){ //if in seconds
		ret = ofToString(secs, decimalPrecision) + " seconds";
	}else{
		if (secs < 3600.0f){ //if in min range
			ret = ofToString(secs / 60.0f, decimalPrecision) + " minutes";
		}else{ //hours or days
			if (secs < 86400.0f){ // hours
				ret = ofToString(secs / 3600.0f, decimalPrecision) + " hours";
			}else{ //days
				ret = ofToString(secs / 86400.0f, decimalPrecision) + " days";
			}
		}
	}
	return ret;
}


#undef CURRENT_FRAME_ALT
