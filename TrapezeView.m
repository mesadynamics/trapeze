//
//  TrapezeView.m
//  Trapeze
//
//  Created by Danny Espinoza on 8/9/06.
//  Copyright 2006 Mesa Dynamics, LLC. All rights reserved.
//

#import "TrapezeView.h"
#import "TrapezeController.h"

@implementation TrapezeView

- (void)awakeFromNib
{
	[self registerForDraggedTypes:[NSArray arrayWithObject:NSFilenamesPboardType]];	

	isDragging = NO;	
}

- (void)drawRect:(NSRect)rect
{
    if(isDragging) {
        NSColor* c1 = [NSColor colorWithCalibratedWhite:.80 alpha:1.0];
        NSColor* c2 = [NSColor colorWithCalibratedWhite:.85 alpha:1.0];
        NSGradient* dragGradient = [[NSGradient alloc] initWithStartingColor:c1 endingColor:c2];
        [dragGradient drawInRect:rect angle:90.0];
    }
    else {
        NSColor* c1 = [NSColor colorWithCalibratedWhite:.90 alpha:1.0];
        NSColor* c2 = [NSColor colorWithCalibratedWhite:.95 alpha:1.0];
        NSGradient* normalGradient = [[NSGradient alloc] initWithStartingColor:c1 endingColor:c2];
        [normalGradient drawInRect:rect angle:90.0];
    }
    
    NSImage* logo = [NSImage imageNamed:@"Trapeze"];
    [logo drawAtPoint:NSMakePoint(156.0, 256.0) fromRect:NSMakeRect(0.0, 0.0, logo.size.width, logo.size.height) operation:NSCompositeSourceOver fraction:1.0];
}

- (BOOL)isOpaque
{
	return NO;
}

- (NSDragOperation)draggingEntered:(id <NSDraggingInfo>)sender
{
	NSString* fileName = nil;
	NSArray* array = (NSArray*) [[sender draggingPasteboard] propertyListForType:NSFilenamesPboardType];
	if(array)
		fileName = (NSString*) [array objectAtIndex:0];
	else {
		NSURL* fileURL = [NSURL URLFromPasteboard: [sender draggingPasteboard]];
		fileName = [fileURL absoluteString];
	}
		
	if([fileName hasSuffix:@".pdf"]) {
		isDragging = YES;
        [self performSelectorOnMainThread:@selector(refresh) withObject:nil waitUntilDone:NO];
		
		return NSDragOperationGeneric;
	}
	
   return NSDragOperationNone;
}

- (void)draggingExited:(id <NSDraggingInfo>)sender
{
	if(isDragging) {
		isDragging = NO;
        [self performSelectorOnMainThread:@selector(refresh) withObject:nil waitUntilDone:NO];
	}
}

- (BOOL)performDragOperation:(id <NSDraggingInfo>)sender
{
	if(isDragging) {
		isDragging = NO;
        [self performSelectorOnMainThread:@selector(refresh) withObject:nil waitUntilDone:NO];
	}

	NSArray* inArray = (NSArray*) [[sender draggingPasteboard] propertyListForType:NSFilenamesPboardType];
	NSMutableArray* outArray = nil;
	
	if(inArray == nil) {
		NSURL* fileURL = [NSURL URLFromPasteboard: [sender draggingPasteboard]];
		if(fileURL)
			inArray = [NSArray arrayWithObject:[fileURL absoluteString]];
	}
	
	if(inArray) {
		NSEnumerator* enumerator = [inArray objectEnumerator];
		NSString* fileName;
		
		while(fileName = [enumerator nextObject]) {
			if([fileName hasSuffix:@".pdf"] || [fileName hasSuffix:@".PDF"]) {
				if(outArray == nil)
					outArray = [[NSMutableArray alloc] initWithCapacity:1];
					
				if(outArray)
					[outArray addObject:[NSString stringWithString:fileName]];
			}
		}
	}
	
	if(outArray) {
        [self performSelectorOnMainThread:@selector(openFiles:) withObject:outArray waitUntilDone:NO];
			
		return YES;
	}
	
	return NO;
}

- (void)disableDrag
{
	[self unregisterDraggedTypes];
}

- (void)openFiles:(NSArray*)files
{
    [[NSApp delegate] application:NSApp openFiles:files];
}

- (void)refresh
{
    [self setNeedsDisplay:YES]; 
}

@end
