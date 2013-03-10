//
//  TrapezeController.m
//  Trapeze
//
//  Created by Danny Espinoza on 8/9/06.
//  Copyright 2006 Mesa Dynamics, LLC. All rights reserved.
//

#import "TrapezeController.h"
#import "TrapezeView.h"
#include <Carbon/Carbon.h>
#include <CoreServices/CoreServices.h>
#include "CMacPDFParser.h"

long gSaveIndex = -1;
long gSaveProgress = -1;
long gSaveMaxProgress = 0;

long gIndex = -1;
extern long gProgress;
extern long gMaxProgress;

@implementation TrapezeController

- (id)init
{
	[super init];

	if(self) {
		conversionList = nil;
		appDragConversion = NO;
		fastQuit = YES;
	}
	
	return self;
}

- (NSString *)windowNibName
{
    return @"MainMenu";
}

- (void)awakeFromNib
{	
	[NSApp setDelegate:self];

	[self setWindowFrameAutosaveName:@"MainWindow"];
	
	if([[NSUserDefaults standardUserDefaults] stringForKey:@"NSWindow Frame MainWindow"] == nil) {
		[[self window] center];
	}
	
	[about center];
	
	[progress setLevel:NSStatusWindowLevel];
	[progress center];
	
	[format setAutoenablesItems:NO];
		
	[self readPreferences];

	[NSTimer
		scheduledTimerWithTimeInterval:(double) .01
		target:self
		selector:@selector(checkProgress:)
		userInfo:nil
		repeats:YES];
		
}

- (IBAction)handleFormat:(id)sender
{
	if([format indexOfSelectedItem] == 3 || [format indexOfSelectedItem] == 4) {
		NSRect markFrame = [optionMark frame];
		if(markFrame.origin.x < 138) {
			markFrame.origin.x = 138;
			[optionMark setFrame:markFrame];
			
			[self handleOption:self];
		}
	}
	else {
		NSRect markFrame = [optionMark frame];
		if(markFrame.origin.x > 118) {
			markFrame.origin.x = 118;
			[optionMark setFrame:markFrame];
			[optionMark setEnabled:YES];
		}
	}
}

- (IBAction)handleOption:(id)sender
{
	if([optionStrip state] == NSOnState) {
		[optionCollapse setEnabled:YES];
		[optionRewrap setEnabled:YES];

		if([format indexOfSelectedItem] == 3 || [format indexOfSelectedItem] == 4)
			[optionMark setEnabled:YES];
	}
	else {
		[optionCollapse setEnabled:NO];
		[optionRewrap setEnabled:NO];

		if([format indexOfSelectedItem] == 3 || [format indexOfSelectedItem] == 4)
			[optionMark setEnabled:NO];
	}

	if([optionRelax state] == NSOnState) {
		[optionTight setEnabled:YES];
	}
	else {
		[optionTight setEnabled:NO];
	}
}

- (IBAction)handleConvert:(id)sender
{
	[self setupConversion];
			
	[NSThread detachNewThreadSelector:@selector(convert:) toTarget:self withObject:self];
	
	fastQuit = NO;
	[[self window] orderOut:self];
	[progress makeKeyAndOrderFront:self];
}

- (void)handleStop:(id)sender
{
}

- (void)convert:(id)sender
{
	NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];

	[progressBar setUsesThreadedAnimation:YES];
	[progressBar startAnimation:self];

	NSEnumerator* enumerator = [conversionList objectEnumerator];
	NSString* fileName;
	NSString* outFileName;

	gSaveIndex = -1;
	gIndex = -1;
	
	while(fileName = [enumerator nextObject]) {
		gIndex++;
		gSaveProgress = -1;
		gSaveMaxProgress = 0;
		gProgress = -1;
		gMaxProgress = 0;	
		
		char* inData = NULL;
		unsigned long inSize = 0;
		
		NSData* data = [NSData dataWithContentsOfFile:fileName];
		if(data == nil)
			continue;

		long formatType = -1;
		switch([format indexOfSelectedItem]) {
			case 0:
				formatType = CPDFParser::kWriteASCII;
				break;
				
			case 1:
				formatType = CPDFParser::kWriteHTML;
				break;
				
			case 2:
				formatType = CPDFParser::kWritePlainText;
				break;
				
			case 3:
				formatType = CPDFParser::kWriteRTF;
				break;
				
			case 4:
				formatType = CPDFParser::kWriteRTFWord;
				break;
				
			default:
				continue;
		}
			
		inData = (char*)[data bytes];
		inSize = (unsigned long)[data length];

		char* tmpData = NULL;
		unsigned long tmpDataSize = 0;

        HFSUniStr255 pdfName;
        NSString* pdfString = [NSString stringWithFormat:@"trapeze_%d.pdf", (int)[fileName hash]];
		::FSGetHFSUniStrFromString((CFStringRef)pdfString, &pdfName);
        
        HFSUniStr255 txtName;
        NSString* txtString = [NSString stringWithFormat:@"trapeze_%d.dat", (int)[fileName hash]];
		::FSGetHFSUniStrFromString((CFStringRef)txtString, &txtName);
						
		FSRef tempFolder;
		FSRef pdfSpec;
		FSRef txtSpec;
		
        ::FSFindFolder(kUserDomain, kTemporaryFolderType, true, &tempFolder);
        
        ::FSMakeFSRefUnicode(&tempFolder, pdfName.length, pdfName.unicode, kTextEncodingUnknown, &pdfSpec);
        ::FSMakeFSRefUnicode(&tempFolder, txtName.length, txtName.unicode, kTextEncodingUnknown, &txtSpec);
       
		::FSDeleteObject(&pdfSpec);
		::FSDeleteObject(&txtSpec);
		                
        ::FSCreateFileUnicode(&tempFolder, pdfName.length, pdfName.unicode, kFSCatInfoNone, NULL, &pdfSpec, NULL);
		
        FSIORefNum refNum;
        
        HFSUniStr255 dataForkName;
        ::FSGetDataForkName(&dataForkName);
        
		if(::FSOpenFork(&pdfSpec, dataForkName.length, dataForkName.unicode, fsRdWrPerm, &refNum) == noErr) {
            ::FSWriteFork(refNum, fsAtMark, 0, inSize, inData, NULL);
			::FSCloseFork(refNum);

			{
				CMacPDFParser parser;
				parser.SetPromptForPassword(false);
				
				if([optionStrip state] == NSOnState)
					parser.SetPadStripping(true);
					
				if([optionCollapse state] == NSOnState)
					parser.SetSorting(false);
					
				if([optionRewrap state] == NSOnState)
					parser.SetRewrapping(true);
					
				if([optionMark state] == NSOnState)
					parser.SetShowBreaks(true);
					
				if([optionRelax state] == NSOnState)
					parser.SetRelaxSpacing(true);
					
				if([optionTight state] == NSOnState)
					parser.SetTightSpacing(true);
                					
				if(parser.ConvertToFile(&pdfSpec, NULL, formatType, 0, NULL, &tempFolder, &txtName) == CPDFParser::kNoError) {
                    ::FSMakeFSRefUnicode(&tempFolder, txtName.length, txtName.unicode, kTextEncodingUnknown, &txtSpec);
                    if(::FSOpenFork(&txtSpec, dataForkName.length, dataForkName.unicode, fsRdPerm, &refNum) == noErr) {
						SInt64 eof;
						::FSGetForkSize(refNum, &eof);
						eof += (4096 - (eof % 4096));
						
						tmpData = (char*) malloc(eof);
						if(tmpData) {
							::FSReadFork(refNum, fsAtMark, 0, eof, tmpData, &tmpDataSize);
						}
						
                        ::FSCloseFork(refNum);
					}
                    else {
                        // error
                        ::FSDeleteObject(&pdfSpec);
                        ::FSDeleteObject(&txtSpec);
                    }
				}
				else {
					// error
                    ::FSDeleteObject(&pdfSpec);
                    ::FSDeleteObject(&txtSpec);
				}
			}
		}
        else {
            // error
            ::FSDeleteObject(&pdfSpec);
            ::FSDeleteObject(&txtSpec);
        }
						
		::FSDeleteObject(&pdfSpec);
		::FSDeleteObject(&txtSpec);
		
		if(tmpData) {
			NSData* outData = [NSData dataWithBytesNoCopy:tmpData length:tmpDataSize freeWhenDone:YES];
			if(outData) {
				switch([format indexOfSelectedItem]) {
					case 0:
						outFileName = [fileName stringByAppendingPathExtension:@"ascii.txt"];
					break;
					
					case 1:
						outFileName = [fileName stringByAppendingPathExtension:@"html"];
					break;
					
					case 2:
						outFileName = [fileName stringByAppendingPathExtension:@"txt"];
					break;
					
					case 3:
						outFileName = [fileName stringByAppendingPathExtension:@"rtf"];
					break;
					
					case 4:
						outFileName = [fileName stringByAppendingPathExtension:@"doc"];
					break;
				}
				
				if([[NSFileManager defaultManager] createFileAtPath:outFileName contents:outData attributes:nil]) {
				}
				else {
					// error
				}
			}
			else {
				// error
				free(tmpData);
			}
		}
	}

	[progressBar stopAnimation:self];
    [self performSelectorOnMainThread:@selector(endConvert) withObject:nil waitUntilDone:NO];

	[pool release];
}

- (void)endConvert
{
 	[progress orderOut:self];
	
	if(appDragConversion == YES)
		[NSApp terminate:self];
    
	[NSApp abortModal];
}

- (void)checkProgress:(id)sender
{
	if(gSaveIndex != gIndex) {
		NSString* currentFile = [conversionList objectAtIndex:gIndex];
		NSString* fileName = [NSString stringWithFormat:@"%@: %@", NSLocalizedString(@"File", @""), [currentFile lastPathComponent]];
		[progressFile setStringValue:fileName];
		
		if([conversionList count] > 1) {
			NSString* filesList = [NSString stringWithFormat:@"%@: %d", NSLocalizedString(@"Remaining", @""), (int)([conversionList count] - (gIndex + 1))];
			[progressRemaining setStringValue:filesList];
		}
			
		gSaveIndex = gIndex;
	}
	
	if(gSaveProgress != gProgress) {
		if(gSaveProgress == -1)
			[progressBar setIndeterminate:NO];
		
		[progressBar setDoubleValue:(double)gProgress];
		
		gSaveProgress = gProgress;
	}
	
	if(gSaveMaxProgress != gMaxProgress) {
		NSString* statusCount;
		if(gMaxProgress == 1)
			statusCount =  NSLocalizedString(@"ConvertOne", @"");
		else {
			NSString* statusFormat = NSLocalizedString(@"ConvertMany", @"");
			statusCount = [NSString stringWithFormat:statusFormat, gMaxProgress];
		}
			
		NSString* status = [NSString stringWithFormat:@"%@ %@", statusCount, [format titleOfSelectedItem]];
		[progressStatus setStringValue:status];
				
		[progressBar setMaxValue:(double)gMaxProgress];
		
		gSaveMaxProgress = gMaxProgress;
	}
}

- (void)readPreferences
{
	NSNumber* setting = nil;
	
	setting = (NSNumber*) CFPreferencesCopyAppValue(CFSTR("Format"), kCFPreferencesCurrentApplication);
	if(setting) {
		[format selectItemAtIndex:[setting intValue]];
		CFRelease(setting);
	}
	
	setting = (NSNumber*) CFPreferencesCopyAppValue(CFSTR("OptionStrip"), kCFPreferencesCurrentApplication);
	if(setting) {
		[optionStrip setState:[setting intValue]];
		CFRelease(setting);
	}
	
	setting = (NSNumber*) CFPreferencesCopyAppValue(CFSTR("OptionCollapse"), kCFPreferencesCurrentApplication);
	if(setting) {
		[optionCollapse setState:[setting intValue]];
		CFRelease(setting);
	}
	
	setting = (NSNumber*) CFPreferencesCopyAppValue(CFSTR("OptionRewrap"), kCFPreferencesCurrentApplication);
	if(setting) {
		[optionRewrap setState:[setting intValue]];
		CFRelease(setting);
	}
	
	setting = (NSNumber*) CFPreferencesCopyAppValue(CFSTR("OptionMark"), kCFPreferencesCurrentApplication);
	if(setting) {
		[optionMark setState:[setting intValue]];
		CFRelease(setting);
	}
	
	setting = (NSNumber*) CFPreferencesCopyAppValue(CFSTR("OptionRelax"), kCFPreferencesCurrentApplication);
	if(setting) {
		[optionRelax setState:[setting intValue]];
		CFRelease(setting);
	}
	
	setting = (NSNumber*) CFPreferencesCopyAppValue(CFSTR("OptionTight"), kCFPreferencesCurrentApplication);
	if(setting) {
		[optionTight setState:[setting intValue]];
		CFRelease(setting);
	}
	
	[self handleFormat:self];
	[self handleOption:self];
}

- (void)writePreferences
{
	CFPreferencesSetAppValue(CFSTR("Format"), [NSNumber numberWithInt:[format indexOfSelectedItem]], kCFPreferencesCurrentApplication);	
	CFPreferencesSetAppValue(CFSTR("OptionStrip"), [NSNumber numberWithInt:[optionStrip state]], kCFPreferencesCurrentApplication);	
	CFPreferencesSetAppValue(CFSTR("OptionCollapse"), [NSNumber numberWithInt:[optionCollapse state]], kCFPreferencesCurrentApplication);	
	CFPreferencesSetAppValue(CFSTR("OptionRewrap"), [NSNumber numberWithInt:[optionRewrap state]], kCFPreferencesCurrentApplication);	
	CFPreferencesSetAppValue(CFSTR("OptionMark"), [NSNumber numberWithInt:[optionMark state]], kCFPreferencesCurrentApplication);	
	CFPreferencesSetAppValue(CFSTR("OptionRelax"), [NSNumber numberWithInt:[optionRelax state]], kCFPreferencesCurrentApplication);	
	CFPreferencesSetAppValue(CFSTR("OptionTight"), [NSNumber numberWithInt:[optionTight state]], kCFPreferencesCurrentApplication);	

	CFPreferencesAppSynchronize(kCFPreferencesCurrentApplication);
}

// NSApplication delegate
- (void)applicationDidFinishLaunching:(NSNotification *)aNotification
{
	[[self window] makeKeyAndOrderFront:self];
}

- (void)applicationWillTerminate:(NSNotification* )aNotification
{
	[self writePreferences];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)theApplication
{
	return fastQuit;
}

- (void)application:(NSApplication *)sender openFiles:(NSArray *)filenames
 {
	[NSApp activateIgnoringOtherApps: YES];
	
	NSMutableArray* outArray = nil;
	
	NSEnumerator* enumerator = [filenames objectEnumerator];
	NSString* fileName;
	
	while(fileName = [enumerator nextObject]) {
		if([fileName hasSuffix:@".pdf"] || [fileName hasSuffix:@".PDF"]) {
			if(outArray == nil)
				outArray = [[NSMutableArray alloc] initWithCapacity:1];
				
			if(outArray)
				[outArray addObject:[NSString stringWithString:fileName]];
		}
	}
	
	if(outArray) {
		[conversionList release];
		conversionList = nil;
	
		conversionList = outArray;
		
		NSWindow* window = [self window];
		
		if(appDragConversion == NO && [window isVisible] == NO) {
			[cancel setHidden:NO];
			[ok setHidden:NO];
			[logo setHidden:NO];
			
			NSRect boxFrame = [box frame];
			boxFrame.origin.x += 64;
			[box setFrame:boxFrame];
			
			NSRect buyFrame = [purchase frame];
			buyFrame.origin.x = 14;
			[purchase setFrame:buyFrame];
			
			NSRect frame = [window frame];
			frame.size.height = 280;
			[window setFrame:frame display:NO];
			
			TrapezeView* trapeze = (TrapezeView*) [window contentView];
			[trapeze disableDrag];
			
			appDragConversion = YES;
		}
		else {
			[self setupConversion];
			
			[NSThread detachNewThreadSelector:@selector(convert:) toTarget:self withObject:self];
			
			NSModalSession session = [NSApp beginModalSessionForWindow:progress];
			for(;;) {
				if ([NSApp runModalSession:session] != NSRunContinuesResponse)
					break;
				
				[self checkProgress:self];
			}
			
			[NSApp endModalSession:session];
		}
	}
}

- (void)setupConversion
{
	[progressBar setIndeterminate:YES];
	[progressBar setMaxValue:0.0];
	[progressBar setDoubleValue:0.0];

	[progressStatus setStringValue:NSLocalizedString(@"Preprocessing", @"")];
	[progressFile setStringValue:@""];
	[progressRemaining setStringValue:@""];
}

@end
