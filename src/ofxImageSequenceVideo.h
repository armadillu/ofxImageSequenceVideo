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

#include "ofxDXT.h"
#if defined(USE_TURBO_JPEG) //you can define this in your pre-processor macros to use turbojpeg to speed up jpeg loading 
	#include "ofxTurboJpeg.h"
#endif

class ofxImageSequenceVideo{

public:

	static const int maxFramePingPongDataStructs = 5;

	ofxImageSequenceVideo();
	~ofxImageSequenceVideo();

	//There are basically 2 operation modes, ASYNC and INMEDIATE. if you specify numThreads = 0,
	//everything will be in immediate mode. All work will be done in the main thread, blocking on update().
	//if you specify  numThreads >= 1, threads are spawned to pre-load frames up to the buffer size
	//you request, so that main thread only loads tex data to GPU (preferred for realtime).
	//note that bufferSize is irrelevant in immediate mode.
	//
	//useDXTcompression == TRUE >> assumes all your images are in a .dxt format on disk;
	//look into ofxDXT to see how to compress them
    void setup(int numThreads, int bufferSize, bool useDXTcompression, bool _reverse = false);
	//NOTE - dont change those on the fly, to be setup once before you load the IMG sequence

	//TODO - don't reuse objects, it will probably fail to load a second img sequence so only load once
	//otherwise things might go wrong
	bool loadImageSequence(const std::string & path, float frameRate);

	//if TRUE, it effectivelly loads the whole img sequence into GPU (during the 1st playback)
	//during the first playback pass, all frames will be loaded from disk, but on the following
	//passes, no more loading will happen as the ofTextures already will be in the GPU.
	//This of course uses tons of GPU memory, so use wisely. See getEstimatdVramUse(); to get an estimate
	//of how much memory your sequence will use.
	//defaults to FALSE
	void setKeepTexturesInGpuMem(bool keep){keepTexturesInGpuMem = keep; }
	bool getKeepTexturesInGpuMem(){return keepTexturesInGpuMem;}

	//this call is a bit expensive as we need to walk all the textures, use sparsingly or for debug only.
	bool areAllTexturesPreloaded(); //(in In Gpu Mem), only makes sense when setKeepTexturesInGpuMem(TRUE);

	//set to FALSE for it to avoid GL calls - only ofPixels will be loaded (handy to use it from a thread)
	void setUseTexture(bool useTex){shouldLoadTexture = useTex;};
	void setReportFileSize(bool report); //if true, player checks and reports file size of each frame - mostly to debug choque points / bottlenecks

	//set the img sequence framerate (playback speed)
	void setPlaybackFramerate(float framerate);
	float getPlaybackFramerate(){return 1.0f / frameDuration;}
	int getNumBufferFrames();

	void update(float dt);

	//returns estimated number of bytes it would take to load the whole img sequence in VRAM
	//this is a rather expensive operation if no frame is loaded already, as we need to load
	//a frame from disk to find out
	size_t getEstimatdVramUse();

	static void getImageInfo(const std::string & filePath, int & width, int & height, int & numChannels, bool & imgOK);

	void play();
	void pause();
    
    bool isPlaying(); 

	void setPlaybackSpeed(float speed){playbackSpeed = speed;}; //1.0 means normal speed. don't go negative or weird things will happen!
	float getPlaybackSpeed(){return playbackSpeed;}

	void advanceOneFrame();
	void seekToFrame(int frame);

	void setPosition(float normalizedPos); //[0..1]
	void setPositionSeconds(float seconds);
	void setLoop(bool loop); //does the sequence loop when it reaches the end during playback?

	int getCurrentFrame();
	int getNumFrames();
	float getPosition(); //as percentage[0..1], -1 if no video is loaded
	float getPositionSeconds();

	float getMovieDuration(); //in seconds

	bool isLoaded(){return loaded;}
	std::string getMoviePath(){return imgSequencePath;}
	float getMovieFramerate(){return 1.0f / frameDuration;}

	bool arePixelsNew(); //since last update

	void eraseAllPixelCache(); //delete all pixel cache
	void eraseAllTextureCache(); //delete all ofTexture cache

	ofPixels& getPixels();
	ofTexture& getTexture();

	//draws a timeline with all frames and their status, the playhead and the buffer size
	void drawDebug(float x, float y, float w);

	//get img sequence stats
	std::string getStatus();
	std::string getBufferStatus(int extendBeyondBuffer = 0);
	std::string getGpuBufferStatus(int extendBeyondBuffer = 0);
	std::string getNumTasks(){ return ofToString(tasks.size()) + "/" + ofToString(numThreads); }
	float getBufferFullness(){ return bufferFullness;}
	float getLoadTimeAvg(){ return loadTimeAvg; } //avg time to load a single frame from disk to pixels, in ms

	struct EventInfo{
		ofxImageSequenceVideo * who = nullptr;
	};

	ofFastEvent<EventInfo> eventMovieLooped;
	ofFastEvent<EventInfo> eventMovieEnded;

	//get a sorted list of all imgs in a dir
	static vector<string> getImagesAtDirectory(const string & path, bool useDxtCompression);

protected:

	static vector<string> getSupportedImageTypes(){ return{"tga", "gif", "jpeg", "jpg", "jp2", "bmp", "png", "tif", "tiff"};}

	enum class PixelState{
		NOT_LOADED,
		LOADING,
		THREAD_FINISHED_LOADING,
		LOADED
	};

	enum class TextureState{
		NOT_LOADED,
		LOADED
	};

	struct FrameInfo{
		string filePath;
		ofPixels pixels;
		ofxDXT::Data compressedPixels;
		PixelState state = PixelState::NOT_LOADED;
		float loadTime = 0; //ms

		TextureState texState = TextureState::NOT_LOADED;
		ofTexture texture; 	//only to be kept around when we are trying to
							//cache the whole anim (bufferSize == numFrames)
	};

	bool loaded = false;
	string imgSequencePath;
	bool keepTexturesInGpuMem = false;

	void advanceFrameInternal();
	
	void handleThreadCleanup();
	void handleThreadSpawn();
	void handleScreenTimeCounters(float dt);
	void handleLooping(bool triggerEvents);

	int currentFrame = 0;
	float frameOnScreenTime = 0.0f;

	int numFrames = 0;
	float frameDuration = 0.0f; //1.0f/framerate

	bool newData = false; //keeps track of state of pixels THIS FRAME
	bool texNeedsLoad = false;

	bool playback = false;
	float playbackSpeed = 1.0;
	bool shouldLoop = true;

	vector<FrameInfo> frames[maxFramePingPongDataStructs];
	int currentFrameSet = -1;
	//this weird thing is to make the "frames" structure ping-poing so that when we load a new
	//video, we can safely move to a new struct and leave the running threads do its thing on the
	//old struct, without having to worry about destroying the memory they are working on
	//TODO - sloppy and wasteful!

	ofTexture tex;
	ofPixels currentPixels; //used in immediate mode only (numThreads==0)
	ofxDXT::Data currentPixelsCompressed; //same as above, but with DXT compression
	bool shouldLoadTexture = true; //use setUseTexture() to disable texture load (and GL calls) alltogether
									//this allows using this class from non-main thread

	struct LoadResults{
		int frame;
		float elapsedTime;
		float filesizeKb;
	};

	float loadTimeAvg = 0.0f;

	vector<std::future<LoadResults>> tasks; //store thread futures

	int numBufferFrames = 8;
	int numThreads = 3;

	bool useDXTCompression = false;

	ofxImageSequenceVideo::LoadResults loadFrameThread(int frame);

	void eraseOutOfBufferPixelCache();

	float bufferFullness = 0.0f; //just to smooth out buffer len 

	void loadPixelsNow(int newFrame, int oldFrame);

	//utils
	std::string secondsToHumanReadable(float secs, int decimalPrecision);
	
    bool reverse = false; // set to true if we need to reverse the sequence
    bool reversing = false; // state for playing in reverse or sequentially

	bool reportFileSize = true;
	float fileSizeAvgKb = 0.0f;
};

