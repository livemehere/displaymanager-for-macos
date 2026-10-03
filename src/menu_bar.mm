#import <AppKit/AppKit.h>
#include "display_manager.h"
#include <algorithm>
#include <memory>

static NSString* text(const std::string& value) {
    NSString* string = [NSString stringWithUTF8String:value.c_str()];
    return string ? string : @"";
}

@interface DisplayManagerDelegate : NSObject <NSApplicationDelegate, NSMenuDelegate> {
    std::unique_ptr<dm::Manager> manager;
}
@property(nonatomic, strong) NSStatusItem* statusItem;
@property(nonatomic, copy) NSString* lastResult;
@end

@implementation DisplayManagerDelegate
- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;
    manager = std::make_unique<dm::Manager>();
    // Clear obsolete preferences; preserve recovery records in the shared file.
    NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
    [defaults removeObjectForKey:@"MirrorTargetUUID"];
    [defaults removeObjectForKey:@"MirrorOwnedUUIDs"];
    [defaults removeObjectForKey:@"MirrorAutomationPaused"];
    self.statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
    NSImage* image = [NSImage imageWithSystemSymbolName:@"display.2" accessibilityDescription:@"DisplayManager"];
    [image setTemplate:YES];
    if (image) self.statusItem.button.image = image;
    else self.statusItem.button.title = @"DM";
    self.statusItem.button.toolTip = @"DisplayManager · Display Controls";
    NSMenu* menu = [[NSMenu alloc] initWithTitle:@"DisplayManager"];
    menu.autoenablesItems = NO;
    menu.delegate = self;
    self.statusItem.menu = menu;
    [self menuNeedsUpdate:menu];
}
- (NSMenuItem*)addItem:(NSString*)title action:(SEL)action menu:(NSMenu*)menu {
    NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title action:action keyEquivalent:@""];
    item.target = self;
    item.enabled = action != nullptr;
    [menu addItem:item];
    return item;
}
- (NSString*)nameForDisplay:(const dm::Display&)display {
    for (NSScreen* screen in [NSScreen screens]) {
        NSNumber* number = screen.deviceDescription[@"NSScreenNumber"];
        if (number.unsignedIntValue == display.id) return screen.localizedName;
    }
    return display.builtin ? @"Built-in Display" : [NSString stringWithFormat:@"External Display %u", display.id];
}
- (void)menuNeedsUpdate:(NSMenu*)menu {
    [menu removeAllItems];
    [self addItem:@"DisplayManager" action:nullptr menu:menu];
    [menu addItem:[NSMenuItem separatorItem]];
    if (!manager->available()) {
        [self addItem:text(manager->error()) action:nullptr menu:menu];
    } else {
        auto displays = manager->displays();
        auto disabled = manager->disabled();
        auto activeCount = std::count_if(displays.begin(), displays.end(), [](const auto& d) { return d.active; });
        [self addItem:@"Active Displays" action:nullptr menu:menu];
        for (const auto& display : displays) {
            if (!display.active && !display.mirrored) continue;
            NSString* title = [NSString stringWithFormat:@"%@ · %zux%zu%@ — Disable",
                [self nameForDisplay:display], display.width, display.height, display.main ? @" (Main)" : @""];
            NSMenuItem* item = [self addItem:title action:@selector(disableDisplay:) menu:menu];
            item.representedObject = @(display.id);
            item.enabled = (activeCount > 1 || !display.active) && !display.key.empty();
            item.toolTip = item.enabled ? @"Disable this display and save its recovery record."
                : display.key.empty() ? @"Display identification is unavailable." : @"The last active display cannot be disabled.";
        }
        if (activeCount == 0) [self addItem:@"No active displays found." action:nullptr menu:menu];
        [menu addItem:[NSMenuItem separatorItem]];
        [self addItem:@"Displays to Restore" action:nullptr menu:menu];
        for (const auto& key : disabled) {
            auto found = std::find_if(displays.begin(), displays.end(), [&](const auto& d) { return d.key == key; });
            NSString* name = found == displays.end()
                ? [NSString stringWithFormat:@"Display %@", text(key.substr(0, 8))] : [self nameForDisplay:*found];
            NSMenuItem* item = [self addItem:[name stringByAppendingString:@" — Restore"] action:@selector(restoreDisplay:) menu:menu];
            item.representedObject = text(key);
            item.toolTip = @"Reconnect a disconnected display before restoring it.";
        }
        if (disabled.empty()) [self addItem:@"No displays to restore." action:nullptr menu:menu];
        NSMenuItem* restore = [self addItem:@"Restore All Displays" action:@selector(restoreAll:) menu:menu];
        restore.enabled = !disabled.empty();
    }
    if (self.lastResult.length) {
        [menu addItem:[NSMenuItem separatorItem]];
        [self addItem:[self.lastResult componentsSeparatedByString:@"\n"].firstObject action:nullptr menu:menu];
    }
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* restoreQuit = [self addItem:@"Restore All and Quit" action:@selector(restoreAndQuit:) menu:menu];
    restoreQuit.enabled = manager->available();
    NSMenuItem* quit = [self addItem:@"Quit (Keep Display State)" action:@selector(quit:) menu:menu];
    quit.keyEquivalent = @"q";
}
- (void)showResult:(const dm::Result&)result {
    self.lastResult = text(result.message);
    if (!result.ok) {
        [NSApp activateIgnoringOtherApps:YES];
        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = @"Display Control Failed";
        alert.informativeText = text(result.message);
        alert.alertStyle = NSAlertStyleWarning;
        [alert addButtonWithTitle:@"OK"];
        [alert runModal];
    }
}
- (void)disableDisplay:(NSMenuItem*)sender {
    [self showResult:manager->disable([sender.representedObject unsignedIntValue])];
}
- (void)restoreDisplay:(NSMenuItem*)sender {
    [self showResult:manager->restore([sender.representedObject UTF8String])];
}
- (void)restoreAll:(id)sender {
    (void)sender;
    [self showResult:manager->restoreAll()];
}
- (void)restoreAndQuit:(id)sender {
    (void)sender;
    auto result = manager->restoreAll();
    if (result.ok) [NSApp terminate:nil];
    else [self showResult:result];
}
- (void)quit:(id)sender {
    (void)sender;
    [NSApp terminate:nil];
}
@end

int main() {
    @autoreleasepool {
        NSApplication* application = [NSApplication sharedApplication];
        [application setActivationPolicy:NSApplicationActivationPolicyAccessory];
        __attribute__((objc_precise_lifetime)) DisplayManagerDelegate* delegate = [[DisplayManagerDelegate alloc] init];
        application.delegate = delegate;
        [application run];
    }
    return 0;
}
