//
//  ScopeView.m
//  AVDecoder
//
//  Created by Grishka on 08.01.2026.
//

#import "ScopeView.h"

@implementation ScopeView{
	std::vector<float>* data1;
	std::vector<float>* data2;
	std::vector<float>* lines;
}

- (void)setData:(std::vector<float>*)data1 secondary:(std::vector<float>*)data2 lines:(std::vector<float>*)lines {
	self->data1=data1;
	self->data2=data2;
	self->lines=lines;
}

- (void)drawRect:(NSRect)dirtyRect {
    [super drawRect:dirtyRect];
	[NSColor.blackColor set];
	NSRectFill(self.bounds);
	
	if(data1 && data1->size()>2){
		[NSColor.yellowColor set];
		NSBezierPath* path=[NSBezierPath new];
		float height=self.bounds.size.height;
		std::vector<float> data=*(self->data1);
		[path moveToPoint:NSMakePoint(0, data[0]*height)];
		float xStep=self.bounds.size.width/data.size();
		for(int i=1;i<data.size();i++){
			float y=data[i]*height;
			float x=xStep*i;
			[path lineToPoint:NSMakePoint(x, y)];
		}
		path.lineWidth=0.5;
		[path stroke];
	}
	
	if(data2 && data2->size()>2){
		[NSColor.redColor set];
		NSBezierPath* path=[NSBezierPath new];
		float height=self.bounds.size.height;
		std::vector<float> data=*(self->data2);
		[path moveToPoint:NSMakePoint(0, data[0]*height)];
		float xStep=self.bounds.size.width/data.size();
		for(int i=1;i<data.size();i++){
			float y=data[i]*height;
			float x=xStep*i;
			[path lineToPoint:NSMakePoint(x, y)];
		}
		path.lineWidth=0.5;
		[path stroke];
	}
	
	if(lines && lines->size()){
		for(int i=0;i<lines->size();i++){
			switch(i){
				case 0: [NSColor.greenColor set]; break;
				case 1: [NSColor.blueColor set]; break;
				case 2: [NSColor.orangeColor set]; break;
				case 3: [NSColor.purpleColor set]; break;
			}
			NSBezierPath* path=[NSBezierPath new];
			float y=(*lines)[i]*self.frame.size.height;
			[path moveToPoint:NSMakePoint(0, y)];
			[path lineToPoint:NSMakePoint(self.frame.size.width, y)];
			path.lineWidth=0.5;
			[path stroke];
		}
	}
}

@end
