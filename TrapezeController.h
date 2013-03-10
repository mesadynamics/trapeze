//
//  TrapezeController.h
//  Trapeze
//
//  Created by Danny Espinoza on 8/9/06.
//  Copyright 2006 Mesa Dynamics, LLC. All rights reserved.
//

#import <Cocoa/Cocoa.h>


@interface TrapezeController : NSWindowController {
	IBOutlet NSPopUpButton* format;
	IBOutlet NSButton* optionStrip;
	IBOutlet NSButton* optionCollapse;
	IBOutlet NSButton* optionRewrap;
	IBOutlet NSButton* optionMark;
	IBOutlet NSButton* optionRelax;
	IBOutlet NSButton* optionTight;
	
	IBOutlet NSButton* purchase;
	IBOutlet NSButton* cancel;
	IBOutlet NSButton* ok;
	
	IBOutlet NSBox* box;
	IBOutlet NSImageView* logo;
	
	IBOutlet NSPanel* progress;
	IBOutlet NSProgressIndicator* progressBar;
	IBOutlet NSTextField* progressStatus;
	IBOutlet NSTextField* progressFile;
	IBOutlet NSTextField* progressRemaining;
	
	IBOutlet NSWindow* about;

	NSArray* conversionList;
	BOOL appDragConversion;
	BOOL fastQuit;
}

- (IBAction)handleFormat:(id)sender;
- (IBAction)handleOption:(id)sender;
- (IBAction)handleConvert:(id)sender;
- (IBAction)handleStop:(id)sender;

- (void)convert:(id)sender;
- (void)checkProgress:(id)sender;

- (void)setupConversion;

- (void)readPreferences;
- (void)writePreferences;

@end

