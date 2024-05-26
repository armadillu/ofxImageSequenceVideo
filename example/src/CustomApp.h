//
//  CustomApp.h
//  BaseApp
//
//  Created by Oriol Ferrer Mesià on 25/01/15.
//
//


#ifndef BaseApp_CustomApp_h
#define BaseApp_CustomApp_h

#include "ofMain.h"
#include "ofxTimeMeasurements.h"
#include "ofxRemoteUIServer.h"
#include "ofxScreenSetup.h"

#define CONFIGS_DIRECTORY	"configs"


class CustomApp : public ofBaseApp {

protected:

	void setup() {

		ofSetFrameRate(60);
		ofSetVerticalSync(true);
		ofEnableAlphaBlending();
		ofBackground(22);

		// setup a listener for changed window modes /////////
		screenSetup.setup(ofGetWidth(), ofGetHeight(), ofxScreenSetup::WINDOWED);

		// SETUP REMOTE UI ///////////////////////////////////
		RUI_SET_CONFIGS_DIR(CONFIGS_DIRECTORY);
		RUI_GET_INSTANCE()->setUiColumnWidth(400);
		RUI_GET_INSTANCE()->setBuiltInUiScale(1.0f); //bigger ui in 4k screens
		RUI_GET_INSTANCE()->drawUiWithFontStash("fonts/VeraMono-Bold.ttf");

		RUI_SETUP();

		// SETUP TIME SAMPLE ///////////////////////////////////

		TIME_SAMPLE_SET_FRAMERATE(60);
		TIME_SAMPLE_SET_CONFIG_DIR(CONFIGS_DIRECTORY);
		TIME_SAMPLE_DISABLE_AVERAGE();
		//TIME_SAMPLE_SET_AVERAGE_RATE(0.04);
		TIME_SAMPLE_SET_DRAW_LOCATION(TIME_MEASUREMENTS_BOTTOM_RIGHT);
		TIME_SAMPLE_SET_REMOVE_EXPIRED_THREADS(true);
		TIME_SAMPLE_GET_INSTANCE()->setDeadThreadTimeDecay(0.985);
		TIME_SAMPLE_GET_INSTANCE()->setUiScale(1.0); //bigger ui in 4k screens
		TIME_SAMPLE_GET_INSTANCE()->drawUiWithFontStash("fonts/VeraMono.ttf");
		TIME_SAMPLE_GET_INSTANCE()->setSavesSettingsOnExit(false);
		TIME_SAMPLE_ENABLE();

	}



	ofxScreenSetup screenSetup;

};

#endif
