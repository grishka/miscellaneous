//
//  ScopeView.h
//  AVDecoder
//
//  Created by Grishka on 08.01.2026.
//

#import <Cocoa/Cocoa.h>
#include <vector>

NS_ASSUME_NONNULL_BEGIN

@interface ScopeView : NSView
- (void)setData:(std::vector<float>*)data1 secondary:(std::vector<float>*)data2 lines:(std::vector<float>*)lines;
@end

NS_ASSUME_NONNULL_END
