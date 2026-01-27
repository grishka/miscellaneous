//
//  RedrawingBitmapView.m
//  AVDecoder
//
//  Created by Grishka on 28.08.2025.
//

#import "RedrawingBitmapView.h"

@implementation RedrawingBitmapView
@synthesize bitmap;

- (void)drawRect:(NSRect)dirtyRect {
    [super drawRect:dirtyRect];
	[bitmap bitmapData]; // forces an update
	[bitmap drawInRect:NSMakeRect(0, 0, self.frame.size.width, self.frame.size.height)];
}

@end
