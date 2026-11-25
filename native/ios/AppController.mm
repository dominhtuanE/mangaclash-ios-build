// iOS entry, modelled on the Cocos2d-x 3.17 iOS template: one EAGL view in a landscape/portrait-locked root controller.
#import "AppController.h"

#import "platform/ios/CCEAGLView-ios.h"
#include "cocos2d.h"
#include "../app/AppDelegate.h"

@interface RootViewController : UIViewController
@end

@implementation RootViewController
- (BOOL)prefersStatusBarHidden { return YES; }
- (BOOL)shouldAutorotate { return YES; }
- (UIInterfaceOrientationMask)supportedInterfaceOrientations { return UIInterfaceOrientationMaskLandscape; }
@end

static AppDelegate* s_app = nullptr;

@implementation AppController

- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)launchOptions {
    self.window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
    CCEAGLView* eaglView = [CCEAGLView viewWithFrame:[self.window bounds]
                                         pixelFormat:kEAGLColorFormatRGBA8
                                         depthFormat:GL_DEPTH24_STENCIL8_OES
                                  preserveBackbuffer:NO
                                          sharegroup:nil
                                       multiSampling:NO
                                     numberOfSamples:0];
    [eaglView setMultipleTouchEnabled:YES];

    self.viewController = [[RootViewController alloc] init];
    self.viewController.view = eaglView;
    [self.window setRootViewController:self.viewController];
    [self.window makeKeyAndVisible];
    [[UIApplication sharedApplication] setStatusBarHidden:YES];

    cocos2d::GLView* glview = cocos2d::GLViewImpl::createWithEAGLView((__bridge void*)eaglView);
    cocos2d::Director::getInstance()->setOpenGLView(glview);

    s_app = new AppDelegate();
    s_app->setResourceDir([[[NSBundle mainBundle] resourcePath] UTF8String] + std::string("/resource"));
    cocos2d::Application::getInstance()->run();
    return YES;
}

- (void)applicationWillResignActive:(UIApplication*)application { cocos2d::Director::getInstance()->pause(); }
- (void)applicationDidBecomeActive:(UIApplication*)application { cocos2d::Director::getInstance()->resume(); }
- (void)applicationDidEnterBackground:(UIApplication*)application { cocos2d::Application::getInstance()->applicationDidEnterBackground(); }
- (void)applicationWillEnterForeground:(UIApplication*)application { cocos2d::Application::getInstance()->applicationWillEnterForeground(); }
- (void)applicationDidReceiveMemoryWarning:(UIApplication*)application { cocos2d::Director::getInstance()->purgeCachedData(); }

@end
