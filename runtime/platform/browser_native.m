/* System WebKit backend for the original program's embedded HTML control.
 * Guest callbacks are queued, never invoked from an Objective-C delegate. */
#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>
#include "browser_native.h"
#include <string.h>
@interface CS16Browser : NSObject<WKNavigationDelegate,WKUIDelegate>
@property(nonatomic,strong) WKWebView *view;
@property(nonatomic,strong) NSWindow *window;
@property(nonatomic) unsigned events;
@property(nonatomic,strong) WKNavigation *navigation;
@end
@implementation CS16Browser
- (void)webView:(WKWebView *)webView didFinishNavigation:(WKNavigation *)navigation { if(navigation==self.navigation)self.events|=1; }
- (void)webView:(WKWebView *)webView didFailNavigation:(WKNavigation *)navigation withError:(NSError *)error { if(navigation!=self.navigation)return;self.events|=2;NSLog(@"[webbrowser] navigation failed: %@",error.localizedDescription); }
- (void)webView:(WKWebView *)webView didFailProvisionalNavigation:(WKNavigation *)navigation withError:(NSError *)error { if(navigation!=self.navigation)return;self.events|=2;NSLog(@"[webbrowser] navigation failed: %@",error.localizedDescription); }
/* Embedded silent mode: no new application windows or modal JS dialogs. */
- (void)webView:(WKWebView *)webView runJavaScriptAlertPanelWithMessage:(NSString *)message initiatedByFrame:(WKFrameInfo *)frame completionHandler:(void (^)(void))completionHandler { completionHandler(); }
- (void)webView:(WKWebView *)webView runJavaScriptConfirmPanelWithMessage:(NSString *)message initiatedByFrame:(WKFrameInfo *)frame completionHandler:(void (^)(BOOL))completionHandler { completionHandler(NO); }
- (void)webView:(WKWebView *)webView runJavaScriptTextInputPanelWithPrompt:(NSString *)prompt defaultText:(NSString *)defaultText initiatedByFrame:(WKFrameInfo *)frame completionHandler:(void (^)(NSString *))completionHandler { completionHandler(nil); }
@end
static CS16Browser *browser(void *p){return (__bridge CS16Browser *)p;}
static int on_main(void){return [NSThread isMainThread];}
void cs16_browser_native_pump(void){
 if(!on_main())return;
 @autoreleasepool {CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.001,true);}
}
void *cs16_browser_native_create(void){
 if(!on_main())return NULL;
 @autoreleasepool {
  [NSApplication sharedApplication];CS16Browser *b=[CS16Browser new];
  WKWebViewConfiguration *config=[WKWebViewConfiguration new];config.websiteDataStore=[WKWebsiteDataStore nonPersistentDataStore];
  b.view=[[WKWebView alloc] initWithFrame:NSMakeRect(0,0,320,240) configuration:config];
  b.view.navigationDelegate=b;b.view.UIDelegate=b;
  b.window=[[NSWindow alloc] initWithContentRect:NSMakeRect(-20000,-20000,320,240) styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
  b.window.releasedWhenClosed=NO;b.window.contentView=b.view;b.window.ignoresMouseEvents=YES;
  /* A real off-screen backing window lets WebKit composite snapshots. The
   * original VGUI renders those snapshots via IViewObject::Draw. */
  [b.window orderBack:nil];
  fprintf(stderr,"[webbrowser] macOS WKWebView created (off-screen backing)\n");
  return (__bridge_retained void *)b;
 }
}
void cs16_browser_native_destroy(void *p){
 if(!p)return;
 @autoreleasepool {CS16Browser *b=CFBridgingRelease(p);b.view.navigationDelegate=nil;b.view.UIDelegate=nil;[b.view stopLoading];[b.window close];}
}
int cs16_browser_native_resize(void *p,int w,int h){
 if(!p||!on_main()||w<1||h<1||w>4096||h>4096)return 0;
 @autoreleasepool {CS16Browser *b=browser(p);[b.window setContentSize:NSMakeSize(w,h)];b.view.frame=NSMakeRect(0,0,w,h);return 1;}
}
int cs16_browser_native_navigate(void *p,const char *text){
 if(!p||!on_main()||!text)return 0;
 @autoreleasepool {CS16Browser *b=browser(p);NSString *s=[NSString stringWithUTF8String:text];if(!s)return 0;
  NSURL *url=[NSURL URLWithString:s];if(!url)return 0;
  NSString *scheme=url.scheme.lowercaseString;
  if(!([scheme isEqual:@"about"]||[scheme isEqual:@"http"]||[scheme isEqual:@"https"]||[scheme isEqual:@"file"]))return 0;
  b.events=0;
  if(url.isFileURL)b.navigation=[b.view loadFileURL:url allowingReadAccessToURL:[url URLByDeletingLastPathComponent]];
  else b.navigation=[b.view loadRequest:[NSURLRequest requestWithURL:url]];
  return 1;
 }
}
int cs16_browser_native_action(void *p,int action){
 if(!p||!on_main())return 0;
 @autoreleasepool {CS16Browser *b=browser(p);switch(action){case 0:[b.view stopLoading];return 1;case 1:b.events=0;b.navigation=[b.view reload];return b.navigation!=nil;case 2:if(!b.view.canGoBack)return 0;b.events=0;b.navigation=[b.view goBack];return b.navigation!=nil;case 3:if(!b.view.canGoForward)return 0;b.events=0;b.navigation=[b.view goForward];return b.navigation!=nil;default:return 0;}}
}
int cs16_browser_native_busy(void *p){return p?browser(p).view.loading:0;}
unsigned cs16_browser_native_events(void *p){if(!p||!on_main())return 0;CS16Browser *b=browser(p);unsigned result=b.events;b.events=0;return result;}
int cs16_browser_native_snapshot(void *p,uint8_t *rgba,int w,int h){
 if(!p||!rgba||!on_main()||w<1||h<1||w>4096||h>4096)return 0;
 @autoreleasepool {
  __block NSImage *image=nil;__block BOOL done=NO;WKSnapshotConfiguration *c=[WKSnapshotConfiguration new];c.afterScreenUpdates=YES;
  [browser(p).view takeSnapshotWithConfiguration:c completionHandler:^(NSImage *value,NSError *error){image=value;done=YES;}];
  NSDate *limit=[NSDate dateWithTimeIntervalSinceNow:3];while(!done && limit.timeIntervalSinceNow>0)cs16_browser_native_pump();
  if(!image)return 0;CGImageRef cg=[image CGImageForProposedRect:NULL context:nil hints:nil];if(!cg)return 0;
  CGColorSpaceRef colors=CGColorSpaceCreateDeviceRGB();CGContextRef context=CGBitmapContextCreate(rgba,w,h,8,w*4,colors,kCGImageAlphaPremultipliedLast|kCGBitmapByteOrder32Big);CGColorSpaceRelease(colors);if(!context)return 0;
  /* Explicit top-down RGBA, independent of NSImage's coordinate orientation. */
  CGContextDrawImage(context,CGRectMake(0,0,w,h),cg);CGContextRelease(context);return 1;
 }
}
int cs16_browser_native_script(void *p,const char *script,char *out,unsigned size){
 if(!p||!on_main()||!script||!out||!size)return 0;
 @autoreleasepool {
  __block NSString *result=nil;__block BOOL done=NO;
  [browser(p).view evaluateJavaScript:[NSString stringWithUTF8String:script] completionHandler:^(id value,NSError *error){if(!error)result=[value description];done=YES;}];
  NSDate *limit=[NSDate dateWithTimeIntervalSinceNow:3];while(!done && limit.timeIntervalSinceNow>0)cs16_browser_native_pump();
  if(!result)return 0;const char *text=result.UTF8String;if(strlen(text)>=size)return 0;memcpy(out,text,strlen(text)+1);return 1;
 }
}
