//
//  AppDelegate.m
//  AVDecoder
//
//  Created by Grishka on 28.08.2025.
//

#import <IOKit/usb/IOUSBLib.h>
#import <IOKit/IOCFPlugIn.h>
#import <CoreFoundation/CoreFoundation.h>

#include <FLAC/stream_encoder.h>
#include <stdexcept>

#import "AppDelegate.h"
#import "RedrawingBitmapView.h"
#import "ScopeView.h"
#include "Decoder.hpp"
#include "threading.h"
#include "SignalSource.hpp"


@interface AppDelegate (){
	IOUSBInterfaceInterface800** usbInterface;
	BOOL running;
	NSBitmapImageRep *bitmap;
	Decoder* decoder;
	NSString* recordingDirectory;
	AVCaptureDeviceDiscoverySession *audioDeviceDiscoverySession;
	AVCaptureDevice *audioInputDevice;
	AVCaptureSession *audioMonitoringSession;
	AVCaptureAudioPreviewOutput *audioOutput;
	AVAssetWriterInput *audioWriterInput;
	CFRunLoopSourceRef runLoopSource;

	SignalSource* source;
	
	bool recordingFlac;
	FLAC__StreamEncoder* flacEncoder;
	NSThread* recordingThread;
	tgvoip::BlockingQueue<tgvoip::Buffer>* flacEncoderBufferQueue;
	tgvoip::BufferPool<BUFFER_SIZE, 10> flacEncoderBufferPool;
	FILE *vbiRecordingFile;
	
	id<NSObject> activityToken;
	dispatch_queue_t sampleCallbackQueue;
	NSTimer *recordingTimeUpdateTimer;
	NSDate *recordingStartDate;
	
	bool stopSourceAfterRecording;
}

@property (strong) IBOutlet NSWindow *window;
@property (strong) IBOutlet RedrawingBitmapView *bitmapView;
@property (strong) IBOutlet ScopeView *scopeView;

@property (strong) IBOutlet NSSlider *whiteLevelSlider;
@property (strong) IBOutlet NSSlider *signalOffsetSlider;

@property (strong) IBOutlet NSSegmentedControl *colorModeSelector;
@property (strong) IBOutlet NSSegmentedControl *levelsModeSelector;
@property (strong) IBOutlet NSSegmentedControl *fieldDisplaySelector;

@property (strong) IBOutlet NSButton *startAdcButton;
@property (strong) IBOutlet NSButton *openFileButton;
@property (strong) IBOutlet NSButton *stopSourceButton;

@property (strong) IBOutlet NSButton *startFlacRecordingButton;
@property (strong) IBOutlet NSButton *startVbiRecordingButton;
@property (strong) IBOutlet NSButton *startVideoRecordingButton;
@property (strong) IBOutlet NSButton *stopFlacRecordingButton;
@property (strong) IBOutlet NSButton *includeBlankingIntervalsCheckbox;

@property (strong) IBOutlet NSPopUpButton *audioDeviceDropdown;
@property (strong) IBOutlet NSSlider *audioVolumeSlider;

@property (strong) IBOutlet NSTextField *recordingStatusText;
@property (strong) IBOutlet NSTextField *recordingTimeText;

@property NSNumber *scopeLineNumber;

@end

@implementation AppDelegate

- (instancetype)init{
	self=[super init];
	if(self){
		_scopeLineNumber=@(16);
	}
	return self;
}

void runLoopCallback(void *info){
	AppDelegate *delegate=(__bridge AppDelegate*)info;
	delegate->_bitmapView.needsDisplay=true;
	delegate->_scopeView.needsDisplay=true;
}

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
	recordingDirectory=[[NSHomeDirectory() stringByAppendingPathComponent:@"Movies"] stringByAppendingPathComponent:@"AVDecoder"];
	recordingFlac=false;
	flacEncoderBufferQueue=new tgvoip::BlockingQueue<tgvoip::Buffer>(10);
	
	int bitmapWidth=942;
	int bitmapHeight=625;
	bitmap=[[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL
												   pixelsWide:bitmapWidth
												   pixelsHigh:bitmapHeight
												bitsPerSample:8
											  samplesPerPixel:4
													 hasAlpha:YES
													 isPlanar:NO
											   colorSpaceName:NSCalibratedRGBColorSpace
												  bytesPerRow:4*bitmapWidth
												 bitsPerPixel:32];
	
	unsigned char* data=bitmap.bitmapData;
	// Set alpha to 255 for all pixels
	for(int i=3;i<bitmap.bytesPerRow*bitmap.pixelsHigh;i+=4){
		data[i]=0xff;
	}
	
	CFRunLoopSourceContext runLoopCtx={0};
	runLoopCtx.info=(__bridge void*)self;
	runLoopCtx.perform=runLoopCallback;
	runLoopSource=CFRunLoopSourceCreate(kCFAllocatorDefault, 0, &runLoopCtx);
	CFRunLoopAddSource(CFRunLoopGetMain(), runLoopSource, kCFRunLoopCommonModes);
	
	decoder=new Decoder(data, (unsigned int)bitmap.pixelsWide, (unsigned int)bitmap.pixelsHigh, (unsigned int)bitmap.bytesPerRow, runLoopSource);
	
	_bitmapView.bitmap=bitmap;
	[_scopeView setData:&decoder->scopeData1 secondary:&decoder->scopeData2 lines:&decoder->scopeLines];
	_bitmapView.needsDisplay=true;
	
	audioDeviceDiscoverySession=[AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes:@[AVCaptureDeviceTypeMicrophone] mediaType:AVMediaTypeAudio position:AVCaptureDevicePositionUnspecified];
	[audioDeviceDiscoverySession addObserver:self forKeyPath:@"devices" options:NSKeyValueObservingOptionNew | NSKeyValueObservingOptionInitial context:NULL];
	sampleCallbackQueue=dispatch_queue_create("Audio recording callback queue", NULL);
}


- (void)applicationWillTerminate:(NSNotification *)aNotification {
}


- (BOOL)applicationSupportsSecureRestorableState:(NSApplication *)app {
	return YES;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender{
	return YES;
}

- (void)runFlacRecordingThread{
	int32_t* intBuffer=(int32_t*)malloc(4*BUFFER_SIZE);
	while(recordingFlac){
		tgvoip::Buffer buf=flacEncoderBufferQueue->GetBlocking();
		for(int i=0;i<buf.Length();i++){
			intBuffer[i]=(int32_t)buf[i]-128;
		}
		bool success=FLAC__stream_encoder_process_interleaved(flacEncoder, intBuffer, (uint32_t)buf.Length());
		if(!success){
			FLAC__StreamEncoderState state=FLAC__stream_encoder_get_state(flacEncoder);
			printf("encoder failed %d\n", state);
		}
	}
	free(intBuffer);
	recordingFlac=false;
	FLAC__stream_encoder_finish(flacEncoder);
	FLAC__stream_encoder_delete(flacEncoder);
	flacEncoder=NULL;
	dispatch_sync(dispatch_get_main_queue(), ^{
		[self resetUiAfterRecording];
		if(self->stopSourceAfterRecording)
			[self stopSource];
	});
}

- (void)runVideoRecordingThread{
	[audioMonitoringSession stopRunning];
	int videoWidth, videoHeight;
	if(decoder->includeBlankingIntervalsInOutput){
		videoWidth=942;
		videoHeight=625;
	}else{
		videoWidth=768;
		videoHeight=576;
	}
	NSError *err=nil;
	
	AVAssetWriter *assetWriter=[AVAssetWriter assetWriterWithURL:[NSURL fileURLWithPath:[self filePathForRecordingWithExtension:@"mov"]] fileType:AVFileTypeQuickTimeMovie error:nil];
	assetWriter.shouldOptimizeForNetworkUse=true;
	AVAssetWriterInput *videoWriterInput=[AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo outputSettings:@{
		AVVideoCodecKey: AVVideoCodecTypeAppleProRes422LT,
		AVVideoWidthKey: @(videoWidth),
		AVVideoHeightKey: @(videoHeight),
		AVVideoCompressionPropertiesKey: @{
			//AVVideoAverageBitRateKey: @15000000
			//AVVideoExpectedSourceFrameRateKey: @25,
		}
	}];
	AVAssetWriterInputPixelBufferAdaptor *inputAdaptor=[AVAssetWriterInputPixelBufferAdaptor assetWriterInputPixelBufferAdaptorWithAssetWriterInput:videoWriterInput sourcePixelBufferAttributes:@{
		(NSString*)kCVPixelBufferWidthKey: @(videoWidth),
		(NSString*)kCVPixelBufferHeightKey: @(videoHeight),
		(NSString*)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)
	}];
	videoWriterInput.expectsMediaDataInRealTime=true;
	[assetWriter addInput:videoWriterInput];
	
	AVCaptureSession *session=[AVCaptureSession new];
	AVCaptureDeviceInput *audioCaptureInput=[AVCaptureDeviceInput deviceInputWithDevice:audioInputDevice error:&err];
	if(err)
		NSLog(@"audio capture input failed %@", err);
	[session addInput:audioCaptureInput];
	AVCaptureAudioDataOutput *output=[AVCaptureAudioDataOutput new];
	output.audioSettings=@{
		AVFormatIDKey: @(kAudioFormatLinearPCM),
		AVSampleRateKey: @48000,
	};
	[output setSampleBufferDelegate:self queue:sampleCallbackQueue];
	[session addOutput:output];
	[audioMonitoringSession removeOutput:audioOutput];
	[session addOutput:audioOutput];
	audioWriterInput=[AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeAudio outputSettings:@{
		AVFormatIDKey: @(kAudioFormatMPEG4AAC),
		AVSampleRateKey: @48000,
		AVNumberOfChannelsKey: @1,
		AVEncoderBitRateKey: @256000
	}];
	audioWriterInput.expectsMediaDataInRealTime=true;
	[assetWriter addInput:audioWriterInput];

	[assetWriter startWriting];
	
	bool first=true;
	CMTime initialTime;
	
	int64_t frameCount=-3; // Compensate for audio delay
	while(decoder->outputEnabled){
		tgvoip::Buffer frame=decoder->getOutputFrame();
		if(!decoder->outputEnabled)
			break;
		if(first){
			first=false;
			[session startRunning];
			CMClockRef clock=session.masterClock;
			initialTime=CMClockGetTime(clock);
			[assetWriter startSessionAtSourceTime:initialTime];
			NSLog(@"Asset writer error: %@", assetWriter.error);
		}
		
		assert(inputAdaptor.pixelBufferPool);
		CVPixelBufferRef pixelBuffer;
		CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, inputAdaptor.pixelBufferPool, &pixelBuffer);
		CVPixelBufferLockBaseAddress(pixelBuffer, 0);
		void *bufferAddr=CVPixelBufferGetBaseAddress(pixelBuffer);
		assert(bufferAddr);
		size_t stride=CVPixelBufferGetBytesPerRow(pixelBuffer);
		for(int y=0;y<videoHeight;y++){
			memcpy(((uint8_t*)bufferAddr)+stride*y, *frame+videoWidth*4*y, videoWidth*4);
		}
		CVPixelBufferUnlockBaseAddress(pixelBuffer, 0);
		[inputAdaptor appendPixelBuffer:pixelBuffer withPresentationTime:CMTimeAdd(CMTimeMake(-5, 25), CMClockGetTime(session.masterClock))];
		CFRelease(pixelBuffer);

		frameCount++;
	}
	[session stopRunning];
	[videoWriterInput markAsFinished];
	[audioWriterInput markAsFinished];
	[assetWriter finishWritingWithCompletionHandler:^{
		dispatch_sync(dispatch_get_main_queue(), ^{
			[self resetUiAfterRecording];
			if(self->stopSourceAfterRecording)
				[self stopSource];
			[self->audioMonitoringSession addOutput:self->audioOutput];
			[self->audioMonitoringSession startRunning];
		});
	}];
}

- (void) captureOutput:(AVCaptureOutput *) output didOutputSampleBuffer:(CMSampleBufferRef) sampleBuffer fromConnection:(AVCaptureConnection *) connection{
	[audioWriterInput appendSampleBuffer:sampleBuffer];
}

- (void)startSource{
	Decoder* decoder=self->decoder;
	bool* recordingFlac=&self->recordingFlac;
	auto flacEncoderBufferPool=&self->flacEncoderBufferPool;
	auto flacEncoderBufferQueue=self->flacEncoderBufferQueue;
	source->callback=[decoder, recordingFlac, flacEncoderBufferPool, flacEncoderBufferQueue](uint8_t* data, size_t length){
		decoder->handleSampleData(data, length);
		if(*recordingFlac){
			tgvoip::Buffer rbuf=flacEncoderBufferPool->Get();
			rbuf.CopyFrom(data, 0, BUFFER_SIZE);
			flacEncoderBufferQueue->Put(std::move(rbuf));
		}
	};
	try{
		source->start();
	}catch(std::runtime_error& err){
		NSString *errorStr=[NSString stringWithCString:err.what() encoding:NSUTF8StringEncoding];
		dispatch_async(dispatch_get_main_queue(), ^{
			NSAlert *alert=[NSAlert alertWithMessageText:NSLocalizedString(@"Signal source error", @"") defaultButton:nil alternateButton:nil otherButton:nil informativeTextWithFormat:@"%@", errorStr];
			alert.alertStyle=NSAlertStyleCritical;
			[alert beginSheetModalForWindow:self->_window completionHandler:nil];
		});
		return;
	}
	
	_startAdcButton.enabled=false;
	_openFileButton.enabled=false;
	_stopSourceButton.enabled=true;
	_startFlacRecordingButton.enabled=true;
	_startVideoRecordingButton.enabled=true;
	_startVbiRecordingButton.enabled=true;

	activityToken=[NSProcessInfo.processInfo beginActivityWithOptions: NSActivityIdleSystemSleepDisabled | NSActivityUserInitiated reason:@"Video capture"];
}

- (void)updateAudioDevices:(NSArray<AVCaptureDevice*>*) devices{
	[_audioDeviceDropdown.menu removeAllItems];
	bool foundExistingDevice=false;
	NSString *deviceID;
	if(audioInputDevice)
		deviceID=audioInputDevice.uniqueID;
	else
		deviceID=[NSUserDefaults.standardUserDefaults stringForKey:@"AudioDevice"];
	for(int i=0;i<devices.count;i++){
		AVCaptureDevice *device=devices[i];
		[_audioDeviceDropdown.menu addItem:[[NSMenuItem alloc] initWithTitle:device.localizedName action:nil keyEquivalent:@""]];
		if(deviceID && [deviceID isEqualToString:device.uniqueID]){
			foundExistingDevice=true;
			[_audioDeviceDropdown selectItemAtIndex:i];
			if(!audioInputDevice){
				[self startMonitoringWithAudioDevice:device];
			}
		}
	}
	if(!foundExistingDevice && devices.count){
		[self startMonitoringWithAudioDevice:devices[0]];
	}
}

- (void)startMonitoringWithAudioDevice:(AVCaptureDevice*) device{
	if(audioMonitoringSession){
		[audioMonitoringSession stopRunning];
	}
	audioInputDevice=device;
	audioMonitoringSession=[AVCaptureSession new];
	AVCaptureDeviceInput *audioCaptureInput=[AVCaptureDeviceInput deviceInputWithDevice:device error:nil];
	assert([audioMonitoringSession canAddInput:audioCaptureInput]);
	[audioMonitoringSession addInput:audioCaptureInput];
	audioOutput=[AVCaptureAudioPreviewOutput new];
	assert([audioMonitoringSession canAddOutput:audioOutput]);
	audioOutput.volume=_audioVolumeSlider.floatValue/_audioVolumeSlider.maxValue;
	[audioMonitoringSession addOutput:audioOutput];
	[audioMonitoringSession startRunning];
}

- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary *)change context:(void *)context{
	if(object==audioDeviceDiscoverySession){
		[self updateAudioDevices:change[@"new"]];
	}else{
		[super observeValueForKeyPath:keyPath ofObject:object change:change context:context];
	}
}

- (NSString*)filePathForRecordingWithExtension:(NSString*)ext{
	NSCalendar* calendar=[[NSCalendar alloc] initWithCalendarIdentifier:NSCalendarIdentifierGregorian];
	NSDateComponents* date=[calendar components:NSCalendarUnitYear | NSCalendarUnitMonth | NSCalendarUnitDay | NSCalendarUnitHour | NSCalendarUnitMinute | NSCalendarUnitSecond fromDate:[NSDate now]];
	NSString* filename=[recordingDirectory stringByAppendingPathComponent:[NSString stringWithFormat:@"%04d%02d%02d_%02d%02d%02d.%@",
																		   (int)date.year, (int)date.month, (int)date.day, (int)date.hour, (int)date.minute, (int)date.second, ext]];
	return filename;
}

- (void)startRecordingTimer{
	recordingStartDate=NSDate.now;
	recordingTimeUpdateTimer=[NSTimer timerWithTimeInterval:0.25 repeats:true block:^(NSTimer * _Nonnull timer) {
		int duration=(int)[NSDate.now timeIntervalSinceDate:self->recordingStartDate];
		self->_recordingTimeText.stringValue=[NSString stringWithFormat:@"%d:%02d:%02d", duration/3600, (duration%3600)/60, duration%60];
	}];
	[NSRunLoop.mainRunLoop addTimer:recordingTimeUpdateTimer forMode:NSDefaultRunLoopMode];
}

- (void)resetUiAfterRecording{
	_startFlacRecordingButton.enabled=true;
	_startVideoRecordingButton.enabled=true;
	_startVbiRecordingButton.enabled=true;
	_stopFlacRecordingButton.enabled=false;
	_recordingStatusText.stringValue=NSLocalizedString(@"Nothing is being recorded", @"");
	_recordingTimeText.stringValue=@"";
	[recordingTimeUpdateTimer invalidate];
	recordingTimeUpdateTimer=nil;
}

- (void)stopSource{
	stopSourceAfterRecording=false;
	_stopSourceButton.enabled=false;
	_startFlacRecordingButton.enabled=false;
	_startVideoRecordingButton.enabled=false;
	_startVbiRecordingButton.enabled=false;
	source->stop();
	delete source;
	source=NULL;
	_startAdcButton.enabled=true;
	_openFileButton.enabled=true;
	[NSProcessInfo.processInfo endActivity:activityToken];
	activityToken=nil;
}

- (IBAction)onStartAdcClick:(id)sender{
	ADCSignalSource *source=new ADCSignalSource();
	source->offset=_signalOffsetSlider.intValue;
	self->source=source;
	[self startSource];
}

- (IBAction)onOpenFileClick:(id)sender{
	NSOpenPanel* openPanel=[NSOpenPanel openPanel];
	openPanel.allowedFileTypes=@[@"flac"];
	openPanel.directory=recordingDirectory;
	[openPanel beginSheetModalForWindow:_window completionHandler:^(NSModalResponse returnCode) {
		if(returnCode==NSModalResponseOK){
			self->source=new FileSignalSource(std::string([openPanel.URL.path UTF8String]));
			[self startSource];
		}
	}];
}

- (IBAction)onStopSourceClick:(id)sender{
	if(recordingFlac || decoder->outputEnabled){
		stopSourceAfterRecording=true;
		[self stopRecording:nil];
	}else{
		[self stopSource];
	}
}

- (IBAction)onColorModeClick:(id)sender{
	switch(_colorModeSelector.selectedSegment){
		case 0:
			decoder->colorMode=ColorDisplayMode::Full;
			break;
		case 1:
			decoder->colorMode=ColorDisplayMode::Y;
			break;
		case 2:
			decoder->colorMode=ColorDisplayMode::Db;
			break;
		case 3:
			decoder->colorMode=ColorDisplayMode::Dr;
			break;
		case 4:
			decoder->colorMode=ColorDisplayMode::Raw;
			break;
	}
}

- (IBAction)onLevelsModeClick:(id)sender{
	switch(_levelsModeSelector.selectedSegment){
		case 0:
			decoder->levelsMode=DisplayLevelsMode::Auto;
			break;
		case 1:
			decoder->levelsMode=DisplayLevelsMode::BlackIsSync;
			break;
		case 2:
			decoder->levelsMode=DisplayLevelsMode::Raw;
			break;
	}
}

- (IBAction)onFieldModeClick:(id)sender{
	decoder->displayFieldsSequentially=_fieldDisplaySelector.selectedSegment==1;
}

- (IBAction)startRecordingFlac:(id)sender{
	flacEncoder=FLAC__stream_encoder_new();
	bool res=FLAC__stream_encoder_set_channels(flacEncoder, 1);
	if(!res) printf("set channels failed\n");
	res=FLAC__stream_encoder_set_bits_per_sample(flacEncoder, 8);
	if(!res) printf("set bits failed\n");
	res=FLAC__stream_encoder_set_sample_rate(flacEncoder, 200000);
	if(!res) printf("set sample rate failed\n");
	FLAC__StreamEncoderInitStatus initStatus=FLAC__stream_encoder_init_file(flacEncoder, [[self filePathForRecordingWithExtension:@"flac"] cStringUsingEncoding:NSUTF8StringEncoding], NULL, NULL);
	if(initStatus!=FLAC__STREAM_ENCODER_INIT_STATUS_OK) printf("init file failed, %d\n", initStatus);
	recordingFlac=true;
	recordingThread=[[NSThread alloc] initWithTarget:self selector:@selector(runFlacRecordingThread) object:nil];
	[recordingThread start];
	_startFlacRecordingButton.enabled=false;
	_startVideoRecordingButton.enabled=false;
	_startVbiRecordingButton.enabled=false;
	_stopFlacRecordingButton.enabled=true;
	_recordingStatusText.stringValue=NSLocalizedString(@"Recording signal", @"");
	[self startRecordingTimer];
}

- (IBAction)stopRecording:(id)sender{
	_stopFlacRecordingButton.enabled=false;
	if(recordingFlac){
		recordingFlac=false;
	}else if(vbiRecordingFile){
		decoder->vbiDataCallback=NULL;
		fclose(vbiRecordingFile);
		vbiRecordingFile=NULL;
		[self resetUiAfterRecording];
	}else{
		decoder->stopOutput();
	}
}

- (IBAction)startRecordingVBI:(id)sender{
	_startFlacRecordingButton.enabled=false;
	_startVideoRecordingButton.enabled=false;
	_startVbiRecordingButton.enabled=false;
	_stopFlacRecordingButton.enabled=true;
	_recordingStatusText.stringValue=NSLocalizedString(@"Recording VBI", @"");
	[self startRecordingTimer];
	FILE *output=fopen([[self filePathForRecordingWithExtension:@"vbi"] cStringUsingEncoding:NSUTF8StringEncoding], "wb");
	vbiRecordingFile=output;
	decoder->vbiDataCallback=[output](uint8_t *data, size_t length){
		fwrite(data, 1, length, output);
	};
}

- (IBAction)changeAudioDevice:(id)sender{
	AVCaptureDevice *device=audioDeviceDiscoverySession.devices[_audioDeviceDropdown.indexOfSelectedItem];
	if(device!=audioInputDevice){
		[self startMonitoringWithAudioDevice:device];
		[NSUserDefaults.standardUserDefaults setObject:device.uniqueID forKey:@"AudioDevice"];
	}
}

- (IBAction)changeAudioVolume:(id)sender{
	audioOutput.volume=_audioVolumeSlider.floatValue/_audioVolumeSlider.maxValue;
}

- (IBAction)startRecordingVideo:(id)sender{
	decoder->includeBlankingIntervalsInOutput=_includeBlankingIntervalsCheckbox.state==NSControlStateValueOn;
	decoder->startOutput();
	recordingThread=[[NSThread alloc] initWithTarget:self selector:@selector(runVideoRecordingThread) object:nil];
	[recordingThread start];
	_startFlacRecordingButton.enabled=false;
	_startVideoRecordingButton.enabled=false;
	_stopFlacRecordingButton.enabled=true;
	_recordingStatusText.stringValue=NSLocalizedString(@"Recording video", @"");
	[self startRecordingTimer];
}

- (IBAction)updateScopeLine:(id)sender{
	decoder->scopeLineIndex=_scopeLineNumber.intValue;
}

- (IBAction)updateWhiteLevel:(id)sender{
	decoder->whiteLevelRatio=0.3f+0.15f*(1.0f-_whiteLevelSlider.floatValue/_whiteLevelSlider.maxValue);
}

- (IBAction)updateSignalOffset:(id)sender{
	ADCSignalSource *adcSource=dynamic_cast<ADCSignalSource*>(source);
	if(adcSource){
		adcSource->offset=_signalOffsetSlider.intValue;
	}
}

@end
