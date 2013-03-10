//
//  TrapezeView.h
//  Trapeze
//
//  Created by Danny Espinoza on 8/9/06.
//  Copyright 2006 Mesa Dynamics, LLC. All rights reserved.
//

#import <Cocoa/Cocoa.h>

@interface TrapezeView : NSView {	
	BOOL isDragging;
}

- (void)disableDrag;

@end
