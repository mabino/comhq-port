#import <Cocoa/Cocoa.h>
#include <cmath>
#include <cstring>
#include <filesystem>

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

#include "app/macos/macos_bridge.h"
#include "core/save_file.h"
#include "net/multiplayer_manager.h"

static NSString* const kPrefFullScreenByDefault = @"FullScreenByDefault";
static NSString* const kPrefSoundMuted = @"SoundMuted";
static NSString* const kPrefSoundVolume = @"SoundVolume";
static NSString* const kPrefPrefillManualWord = @"PrefillManualWord";
static NSString* const kPrefAutoSubmitManualWord = @"AutoSubmitManualWord";

@interface ChqMacAppController : NSObject <NSWindowDelegate>

@property (nonatomic, strong, nullable) NSWindow* settingsWindow;
@property (nonatomic, strong, nullable) NSButton* fullScreenCheckbox;
@property (nonatomic, strong, nullable) NSButton* prefillManualWordCheckbox;
@property (nonatomic, strong, nullable) NSButton* settingsMuteCheckbox;
@property (nonatomic, strong, nullable) NSSlider* settingsVolumeSlider;
@property (nonatomic, strong, nullable) NSTextField* settingsVolumeLabel;

@property (nonatomic, strong, nullable) NSWindow* helpWindow;

@property (nonatomic, strong, nullable) NSMenuItem* soundMenuMuteItem;
@property (nonatomic, strong, nullable) NSMenuItem* soundMenuIncreaseItem;
@property (nonatomic, strong, nullable) NSMenuItem* soundMenuDecreaseItem;
@property (nonatomic, strong, nullable) NSMenuItem* soundMenuVolumeIndicator;
@property (nonatomic, strong, nullable) NSTimer* trackingTimer;

@property (nonatomic, strong, nullable) NSMenuItem* mpHostItem;
@property (nonatomic, strong, nullable) NSMenuItem* mpJoinItem;
@property (nonatomic, strong, nullable) NSMenuItem* mpDirectItem;
@property (nonatomic, strong, nullable) NSMenuItem* mpDisconnectItem;
@property (nonatomic, strong, nullable) NSMenuItem* mpStatusItem;

@property (nonatomic, assign) chq::macos::AppBridgeCallbacks callbacks;

- (void)openSettingsWindow:(nullable id)sender;
- (void)openHelpWindow:(nullable id)sender;
- (void)toggleMute:(nullable id)sender;
- (void)increaseVolume:(nullable id)sender;
- (void)decreaseVolume:(nullable id)sender;
- (void)syncSoundUIWithVolume:(float)vol muted:(BOOL)muted;
- (void)startMenuTrackingTimer;
- (void)stopMenuTrackingTimer;

- (void)newGame:(nullable id)sender;
- (void)saveGame:(nullable id)sender;
- (void)saveGameAs:(nullable id)sender;
- (void)openGame:(nullable id)sender;
- (void)revealSavesFolder:(nullable id)sender;

- (void)hostInternetMatch:(nullable id)sender;
- (void)joinInternetMatch:(nullable id)sender;
- (void)directMatch:(nullable id)sender;
- (void)disconnectMultiplayer:(nullable id)sender;

- (BOOL)validateMenuItem:(NSMenuItem*)item;

@end

static ChqMacAppController* s_controller = nil;

@implementation ChqMacAppController

- (void)openSettingsWindow:(nullable id)sender {
    if (!self.settingsWindow) {
        [self buildSettingsWindow];
    }

    const BOOL isFs = [[NSUserDefaults standardUserDefaults] boolForKey:kPrefFullScreenByDefault];
    self.fullScreenCheckbox.state = isFs ? NSControlStateValueOn : NSControlStateValueOff;

    id prefillObj = [[NSUserDefaults standardUserDefaults] objectForKey:kPrefPrefillManualWord];
    const BOOL prefillVal = (prefillObj != nil) ? [prefillObj boolValue] : YES;
    self.prefillManualWordCheckbox.state = prefillVal ? NSControlStateValueOn : NSControlStateValueOff;

    float vol = 1.0f;
    bool muted = false;
    if (self.callbacks.get_volume) vol = self.callbacks.get_volume(self.callbacks.context);
    if (self.callbacks.is_muted) muted = self.callbacks.is_muted(self.callbacks.context);
    [self syncSoundUIWithVolume:vol muted:muted];

    [self.settingsWindow makeKeyAndOrderFront:sender];
    [NSApp activateIgnoringOtherApps:YES];
}

- (void)buildSettingsWindow {
    const NSRect frame = NSMakeRect(0, 0, 460, 295);
    NSWindow* win = [[NSWindow alloc] initWithContentRect:frame
                                                styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable)
                                                  backing:NSBackingStoreBuffered
                                                    defer:NO];
    win.title = @"Settings";
    [win center];
    win.releasedWhenClosed = NO;
    win.delegate = self;

    NSView* content = [[NSView alloc] initWithFrame:frame];
    win.contentView = content;

    // --- Startup & Display Section ---
    NSTextField* displayHeader = [NSTextField labelWithString:@"Startup & Display"];
    displayHeader.font = [NSFont boldSystemFontOfSize:13];
    displayHeader.frame = NSMakeRect(20, 255, 420, 20);
    [content addSubview:displayHeader];

    NSButton* fsCheck = [NSButton checkboxWithTitle:@"Open in Full Screen by default"
                                             target:self
                                             action:@selector(fullScreenCheckboxChanged:)];
    fsCheck.frame = NSMakeRect(30, 227, 400, 24);
    const BOOL fsVal = [[NSUserDefaults standardUserDefaults] boolForKey:kPrefFullScreenByDefault];
    fsCheck.state = fsVal ? NSControlStateValueOn : NSControlStateValueOff;
    [content addSubview:fsCheck];
    self.fullScreenCheckbox = fsCheck;

    NSTextField* fsHint = [NSTextField labelWithString:@"When enabled, Command HQ opens directly into full screen upon launch."];
    fsHint.font = [NSFont systemFontOfSize:11];
    fsHint.textColor = [NSColor secondaryLabelColor];
    fsHint.frame = NSMakeRect(48, 210, 390, 16);
    [content addSubview:fsHint];

    // Separator line 1
    NSBox* separator1 = [[NSBox alloc] initWithFrame:NSMakeRect(20, 197, 420, 1)];
    separator1.boxType = NSBoxSeparator;
    [content addSubview:separator1];

    // --- Copy Protection & Startup Section ---
    NSTextField* gameHeader = [NSTextField labelWithString:@"Manual Verification Prompt"];
    gameHeader.font = [NSFont boldSystemFontOfSize:13];
    gameHeader.frame = NSMakeRect(20, 167, 420, 20);
    [content addSubview:gameHeader];

    NSButton* prefillCheck = [NSButton checkboxWithTitle:@"Auto-fill manual verification word on startup"
                                                  target:self
                                                  action:@selector(prefillManualWordCheckboxChanged:)];
    prefillCheck.frame = NSMakeRect(30, 139, 400, 24);
    id prefillObj = [[NSUserDefaults standardUserDefaults] objectForKey:kPrefPrefillManualWord];
    const BOOL prefillVal = (prefillObj != nil) ? [prefillObj boolValue] : YES;
    prefillCheck.state = prefillVal ? NSControlStateValueOn : NSControlStateValueOff;
    [content addSubview:prefillCheck];
    self.prefillManualWordCheckbox = prefillCheck;

    NSTextField* prefillHint = [NSTextField labelWithString:@"Pre-fills the correct word in the verification prompt so pressing Return continues."];
    prefillHint.font = [NSFont systemFontOfSize:11];
    prefillHint.textColor = [NSColor secondaryLabelColor];
    prefillHint.frame = NSMakeRect(48, 122, 390, 16);
    [content addSubview:prefillHint];

    // Separator line 2
    NSBox* separator2 = [[NSBox alloc] initWithFrame:NSMakeRect(20, 107, 420, 1)];
    separator2.boxType = NSBoxSeparator;
    [content addSubview:separator2];

    // --- Audio Section ---
    NSTextField* audioHeader = [NSTextField labelWithString:@"Game Audio"];
    audioHeader.font = [NSFont boldSystemFontOfSize:13];
    audioHeader.frame = NSMakeRect(20, 77, 420, 20);
    [content addSubview:audioHeader];

    NSButton* muteCheck = [NSButton checkboxWithTitle:@"Mute sound"
                                               target:self
                                               action:@selector(settingsMuteCheckboxChanged:)];
    muteCheck.frame = NSMakeRect(30, 47, 120, 24);
    [content addSubview:muteCheck];
    self.settingsMuteCheckbox = muteCheck;

    NSTextField* volTitle = [NSTextField labelWithString:@"Volume:"];
    volTitle.frame = NSMakeRect(160, 51, 60, 18);
    [content addSubview:volTitle];

    NSSlider* slider = [NSSlider sliderWithValue:1.0
                                        minValue:0.0
                                        maxValue:1.0
                                          target:self
                                          action:@selector(settingsVolumeSliderChanged:)];
    slider.frame = NSMakeRect(220, 49, 140, 20);
    slider.continuous = YES;
    [content addSubview:slider];
    self.settingsVolumeSlider = slider;

    NSTextField* volVal = [NSTextField labelWithString:@"100%"];
    volVal.frame = NSMakeRect(370, 51, 50, 18);
    [content addSubview:volVal];
    self.settingsVolumeLabel = volVal;

    self.settingsWindow = win;
}

- (void)prefillManualWordCheckboxChanged:(NSButton*)sender {
    const BOOL enabled = (sender.state == NSControlStateValueOn);
    [[NSUserDefaults standardUserDefaults] setBool:enabled forKey:kPrefPrefillManualWord];
    [[NSUserDefaults standardUserDefaults] synchronize];
    if (self.callbacks.sync_startup_settings) {
        self.callbacks.sync_startup_settings(self.callbacks.context);
    }
}

- (void)fullScreenCheckboxChanged:(NSButton*)sender {
    const BOOL isFs = (sender.state == NSControlStateValueOn);
    [[NSUserDefaults standardUserDefaults] setBool:isFs forKey:kPrefFullScreenByDefault];
    [[NSUserDefaults standardUserDefaults] synchronize];
}

- (void)settingsMuteCheckboxChanged:(NSButton*)sender {
    const BOOL m = (sender.state == NSControlStateValueOn);
    if (self.callbacks.set_muted) {
        self.callbacks.set_muted(self.callbacks.context, m);
    }
    float vol = 1.0f;
    if (self.callbacks.get_volume) vol = self.callbacks.get_volume(self.callbacks.context);
    [self syncSoundUIWithVolume:vol muted:m];
}

- (void)settingsVolumeSliderChanged:(NSSlider*)sender {
    const float val = [sender floatValue];
    if (self.callbacks.set_volume) {
        self.callbacks.set_volume(self.callbacks.context, val);
    }
    bool muted = false;
    if (self.callbacks.is_muted) muted = self.callbacks.is_muted(self.callbacks.context);
    [self syncSoundUIWithVolume:val muted:muted];
}

- (void)openHelpWindow:(nullable id)sender {
    if (!self.helpWindow) {
        [self buildHelpWindow];
    }
    [self.helpWindow makeKeyAndOrderFront:sender];
    [NSApp activateIgnoringOtherApps:YES];
}

- (void)buildHelpWindow {
    const NSRect frame = NSMakeRect(0, 0, 560, 500);
    NSWindow* win = [[NSWindow alloc] initWithContentRect:frame
                                                styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable)
                                                  backing:NSBackingStoreBuffered
                                                    defer:NO];
    win.title = @"Command HQ Help";
    win.minSize = NSMakeSize(440, 340);
    [win center];
    win.releasedWhenClosed = NO;
    win.delegate = self;

    NSView* content = [[NSView alloc] initWithFrame:frame];
    win.contentView = content;

    NSTextField* titleLabel = [NSTextField labelWithString:@"Command HQ Help"];
    titleLabel.font = [NSFont boldSystemFontOfSize:18];
    titleLabel.frame = NSMakeRect(20, 455, 520, 28);
    titleLabel.autoresizingMask = NSViewMinYMargin | NSViewWidthSizable;
    [content addSubview:titleLabel];

    NSTextField* subtitleLabel = [NSTextField labelWithString:@"Controls, Scenarios & Original Release Notes"];
    subtitleLabel.font = [NSFont systemFontOfSize:12];
    subtitleLabel.textColor = [NSColor secondaryLabelColor];
    subtitleLabel.frame = NSMakeRect(20, 435, 520, 18);
    subtitleLabel.autoresizingMask = NSViewMinYMargin | NSViewWidthSizable;
    [content addSubview:subtitleLabel];

    const NSRect scrollFrame = NSMakeRect(20, 55, 520, 370);
    NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:scrollFrame];
    scroll.hasVerticalScroller = YES;
    scroll.hasHorizontalScroller = NO;
    scroll.borderType = NSBezelBorder;
    scroll.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

    NSTextView* text = [[NSTextView alloc] initWithFrame:scroll.contentView.bounds];
    text.editable = NO;
    text.selectable = YES;
    text.font = [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular];
    text.textContainer.containerSize = NSMakeSize(scroll.contentSize.width, CGFLOAT_MAX);
    text.textContainer.widthTracksTextView = YES;
    text.autoresizingMask = NSViewWidthSizable;

    NSString* helpText =
        @"COMMAND HQ (1990, Ozark Softscape / MicroProse)\n"
        @"Native Port for macOS Apple Silicon\n"
        @"------------------------------------------------------------\n\n"
        @"MOUSE & KEYBOARD CONTROLS\n\n"
        @"Map & Unit Orders:\n"
        @"  Left-click unit:        Select unit and set destination on world map\n"
        @"  Right-click map:        Inspect city/region or cancel selection\n"
        @"  Spacebar / Return:      Confirm dialogs and advance prompts\n"
        @"  Escape:                 Pause / return to options menu\n"
        @"  Function Keys / Menus:  Access Map Mode, Intel, Economic & Replay views\n\n"
        @"Application Shortcuts:\n"
        @"  Cmd+N       Restart Game\n"
        @"  Cmd+S       Save Game\n"
        @"  Cmd+Shift+S Save Game As…\n"
        @"  Cmd+O       Import / Open Saved Game (CHQ-xx.SG)\n"
        @"  Cmd+Shift+O Reveal Saves Folder in Finder\n"
        @"  Cmd+Q       Quit application\n"
        @"  Ctrl+Cmd+F  Toggle Full Screen\n"
        @"  Cmd+,       Open Settings\n"
        @"  Cmd+Shift+M Toggle Sound Mute\n"
        @"  Cmd+=       Increase Sound Volume\n"
        @"  Cmd+-       Decrease Sound Volume\n"
        @"  Option+P    Pause / Unpause Game (Alt+P)\n";

    if (self.callbacks.get_doc_text) {
        const char* doc = self.callbacks.get_doc_text(self.callbacks.context);
        if (doc && doc[0]) {
            NSString* docStr = [NSString stringWithUTF8String:doc];
            helpText = [helpText stringByAppendingFormat:@"\n============================================================\nORIGINAL README\n============================================================\n\n%@", docStr];
        }
    }

    text.string = helpText;
    scroll.documentView = text;
    [content addSubview:scroll];

    NSButton* closeBtn = [NSButton buttonWithTitle:@"Close" target:win action:@selector(performClose:)];
    closeBtn.frame = NSMakeRect(450, 15, 90, 30);
    closeBtn.keyEquivalent = @"\r";
    closeBtn.autoresizingMask = NSViewMaxYMargin | NSViewMinXMargin;
    [content addSubview:closeBtn];

    self.helpWindow = win;
}

- (void)toggleMute:(nullable id)sender {
    (void)sender;
    if (self.callbacks.toggle_mute) {
        self.callbacks.toggle_mute(self.callbacks.context);
    }
    float vol = 1.0f;
    bool muted = false;
    if (self.callbacks.get_volume) vol = self.callbacks.get_volume(self.callbacks.context);
    if (self.callbacks.is_muted) muted = self.callbacks.is_muted(self.callbacks.context);
    [self syncSoundUIWithVolume:vol muted:muted];
}

- (void)increaseVolume:(nullable id)sender {
    (void)sender;
    if (self.callbacks.increase_volume) {
        self.callbacks.increase_volume(self.callbacks.context);
    }
    float vol = 1.0f;
    bool muted = false;
    if (self.callbacks.get_volume) vol = self.callbacks.get_volume(self.callbacks.context);
    if (self.callbacks.is_muted) muted = self.callbacks.is_muted(self.callbacks.context);
    [self syncSoundUIWithVolume:vol muted:muted];
}

- (void)decreaseVolume:(nullable id)sender {
    (void)sender;
    if (self.callbacks.decrease_volume) {
        self.callbacks.decrease_volume(self.callbacks.context);
    }
    float vol = 1.0f;
    bool muted = false;
    if (self.callbacks.get_volume) vol = self.callbacks.get_volume(self.callbacks.context);
    if (self.callbacks.is_muted) muted = self.callbacks.is_muted(self.callbacks.context);
    [self syncSoundUIWithVolume:vol muted:muted];
}

- (void)syncSoundUIWithVolume:(float)vol muted:(BOOL)muted {
    const int percent = (int)std::round(vol * 100.0f);

    if (self.soundMenuMuteItem) {
        self.soundMenuMuteItem.title = muted ? @"Unmute" : @"Mute";
        self.soundMenuMuteItem.state = muted ? NSControlStateValueOn : NSControlStateValueOff;
    }
    if (self.soundMenuVolumeIndicator) {
        self.soundMenuVolumeIndicator.title = [NSString stringWithFormat:@"Volume: %d%%", percent];
    }

    if (self.settingsMuteCheckbox) {
        self.settingsMuteCheckbox.state = muted ? NSControlStateValueOn : NSControlStateValueOff;
    }
    if (self.settingsVolumeSlider) {
        self.settingsVolumeSlider.floatValue = vol;
    }
    if (self.settingsVolumeLabel) {
        self.settingsVolumeLabel.stringValue = [NSString stringWithFormat:@"%d%%", percent];
    }
}

- (void)startMenuTrackingTimer {
    [self stopMenuTrackingTimer];
    self.trackingTimer = [NSTimer timerWithTimeInterval:(1.0 / 60.0)
                                                 target:self
                                               selector:@selector(onMenuTrackingTick:)
                                               userInfo:nil
                                                repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:self.trackingTimer forMode:NSRunLoopCommonModes];
}

- (void)stopMenuTrackingTimer {
    if (self.trackingTimer) {
        [self.trackingTimer invalidate];
        self.trackingTimer = nil;
    }
}

- (void)onMenuTrackingTick:(NSTimer*)timer {
    (void)timer;
    if (self.callbacks.step_frame) {
        self.callbacks.step_frame(self.callbacks.context);
    }
}

- (void)newGame:(nullable id)sender {
    (void)sender;
    if (self.callbacks.new_game) {
        self.callbacks.new_game(self.callbacks.context);
    }
}

- (void)openGame:(nullable id)sender {
    (void)sender;
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.title = @"Import Saved Game";
    panel.message = @"Select a Command HQ saved game file (CHQ-xx.SG) to import into your Saves folder:";
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    panel.allowedFileTypes = @[ @"sg", @"SG" ];
    panel.allowsOtherFileTypes = YES;

    if (self.callbacks.get_default_save_dir) {
        const char* dir = self.callbacks.get_default_save_dir(self.callbacks.context);
        if (dir && dir[0]) {
            panel.directoryURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:dir]];
        }
    }

    if ([panel runModal] == NSModalResponseOK) {
        NSString* path = panel.URL.path;
        if (path.length > 0) {
            const char* cPath = [path UTF8String];
            if (!chq::validate_save_file(cPath)) {
                NSAlert* alert = [[NSAlert alloc] init];
                alert.messageText = @"Invalid Save File";
                alert.informativeText = @"The selected file is not a valid Command HQ saved game (CHQ-xx.SG).";
                alert.alertStyle = NSAlertStyleWarning;
                [alert addButtonWithTitle:@"OK"];
                [alert runModal];
                return;
            }
            if (self.callbacks.open_game) {
                self.callbacks.open_game(self.callbacks.context, cPath);
            }
        }
    }
}

- (void)saveGame:(nullable id)sender {
    (void)sender;
    if (self.callbacks.save_game) {
        self.callbacks.save_game(self.callbacks.context);
    }
}

- (void)saveGameAs:(nullable id)sender {
    (void)sender;
    char chosen[1024] = {0};
    const char* def_dir = self.callbacks.get_default_save_dir ? self.callbacks.get_default_save_dir(self.callbacks.context) : nullptr;
    const char* active = self.callbacks.get_active_save_path ? self.callbacks.get_active_save_path(self.callbacks.context) : nullptr;
    std::string def_name = active ? std::filesystem::path(active).filename().string() : "CHQ-00.SG";
    if (chq::macos::prompt_save_as_panel(chosen, sizeof(chosen), def_dir, def_name.c_str())) {
        if (self.callbacks.save_game_as) {
            self.callbacks.save_game_as(self.callbacks.context, chosen);
        }
    }
}

- (BOOL)validateMenuItem:(NSMenuItem*)item {
    if (item.action == @selector(saveGame:) || item.action == @selector(saveGameAs:)) {
        if (self.callbacks.can_save) {
            return self.callbacks.can_save(self.callbacks.context) ? YES : NO;
        }
        return NO;
    }
    if (item.action == @selector(disconnectMultiplayer:)) {
        return chq::net::MultiplayerManager::instance().state() != chq::net::MultiplayerState::Disconnected;
    }
    if (self.mpStatusItem) {
        auto& mgr = chq::net::MultiplayerManager::instance();
        switch (mgr.state()) {
            case chq::net::MultiplayerState::Disconnected:
                self.mpStatusItem.title = @"Status: Inactive";
                break;
            case chq::net::MultiplayerState::Hosting: {
                std::string rc = mgr.room_code();
                if (rc.empty()) {
                    self.mpStatusItem.title = @"Status: Hosting (Connecting...)";
                } else {
                    self.mpStatusItem.title = [NSString stringWithFormat:@"Status: Hosting (Room %@)",
                        [NSString stringWithUTF8String:rc.c_str()]];
                }
                break;
            }
            case chq::net::MultiplayerState::Connecting:
                self.mpStatusItem.title = @"Status: Connecting...";
                break;
            case chq::net::MultiplayerState::Connected: {
                double rtt = mgr.round_trip_time_ms();
                std::string rc = mgr.room_code();
                if (rtt > 0.0) {
                    self.mpStatusItem.title = [NSString stringWithFormat:@"Status: Connected (%@ | %.0f ms)",
                        [NSString stringWithUTF8String:rc.c_str()], rtt];
                } else {
                    self.mpStatusItem.title = [NSString stringWithFormat:@"Status: Connected (%@)",
                        [NSString stringWithUTF8String:rc.c_str()]];
                }
                break;
            }
            case chq::net::MultiplayerState::Faulted:
                self.mpStatusItem.title = @"Status: Connection Faulted";
                break;
        }
    }
    return YES;
}

- (void)hostInternetMatch:(nullable id)sender {
    (void)sender;
    auto& mgr = chq::net::MultiplayerManager::instance();
    if (mgr.state() != chq::net::MultiplayerState::Disconnected) {
        NSAlert* confirm = [[NSAlert alloc] init];
        confirm.messageText = @"Active Multiplayer Session";
        confirm.informativeText = @"A multiplayer session is already active. Disconnect and start a new host session?";
        confirm.alertStyle = NSAlertStyleWarning;
        [confirm addButtonWithTitle:@"Disconnect & Host"];
        [confirm addButtonWithTitle:@"Cancel"];
        if ([confirm runModal] != NSAlertFirstButtonReturn) {
            return;
        }
        mgr.disconnect();
    }

    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = @"Host Internet Match";
    alert.informativeText = @"Host a multiplayer game across the internet. An outbound connection will connect to the relay broker and assign a Room Code.";
    alert.alertStyle = NSAlertStyleInformational;
    [alert addButtonWithTitle:@"Host Match"];
    [alert addButtonWithTitle:@"Cancel"];

    NSView* container = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 320, 85)];

    NSTextField* relayLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 58, 320, 18)];
    relayLabel.stringValue = @"Relay Server (host:port or localhost):";
    relayLabel.editable = NO;
    relayLabel.bordered = NO;
    relayLabel.backgroundColor = [NSColor clearColor];
    [container addSubview:relayLabel];

    const char* env_relay = std::getenv("CHQ_RELAY_SERVER");
    NSString* defRelay = (env_relay && env_relay[0]) ? [NSString stringWithUTF8String:env_relay] : @"localhost:7190";
    NSTextField* relayField = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 36, 320, 22)];
    relayField.stringValue = defRelay;
    [container addSubview:relayField];

    NSTextField* codeLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 18, 320, 16)];
    codeLabel.stringValue = @"Custom Room Code (leave blank for random):";
    codeLabel.font = [NSFont systemFontOfSize:11];
    codeLabel.editable = NO;
    codeLabel.bordered = NO;
    codeLabel.backgroundColor = [NSColor clearColor];
    [container addSubview:codeLabel];

    NSTextField* codeField = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 160, 20)];
    codeField.placeholderString = @"e.g. WAR-1234";
    [container addSubview:codeField];

    alert.accessoryView = container;

    if ([alert runModal] == NSAlertFirstButtonReturn) {
        std::string relay_str = [relayField.stringValue UTF8String];
        std::string custom_code = [codeField.stringValue UTF8String];

        std::string host = "localhost";
        int port = 7190;
        auto colon = relay_str.find(':');
        if (colon != std::string::npos) {
            host = relay_str.substr(0, colon);
            try {
                port = std::stoi(relay_str.substr(colon + 1));
            } catch (...) {
                port = 7190;
            }
        } else if (!relay_str.empty()) {
            host = relay_str;
        }

        if (!mgr.host_internet_game(host, port, custom_code)) {
            NSAlert* err = [[NSAlert alloc] init];
            err.messageText = @"Unable to Host Match";
            err.informativeText = @"Could not establish connection to the relay server.";
            err.alertStyle = NSAlertStyleCritical;
            [err addButtonWithTitle:@"OK"];
            [err runModal];
            return;
        }

        // Wait up to 1 second for room code confirmation
        for (int i = 0; i < 50 && mgr.room_code().empty(); ++i) {
            [NSThread sleepForTimeInterval:0.02];
        }

        NSString* assignedCode = [NSString stringWithUTF8String:mgr.room_code().c_str()];
        NSAlert* info = [[NSAlert alloc] init];
        info.messageText = @"Match Hosted Successfully";
        info.informativeText = [NSString stringWithFormat:
            @"Room Code: %@\n\n"
            @"1. Share this Room Code with your opponent.\n"
            @"2. In Command HQ, select: Multiplayer -> MODEM -> Answer.",
            assignedCode];
        info.alertStyle = NSAlertStyleInformational;
        [info addButtonWithTitle:@"Copy Room Code"];
        [info addButtonWithTitle:@"OK"];

        if ([info runModal] == NSAlertFirstButtonReturn) {
            NSPasteboard* pb = [NSPasteboard generalPasteboard];
            [pb clearContents];
            [pb setString:assignedCode forType:NSPasteboardTypeString];
        }
    }
}

- (void)joinInternetMatch:(nullable id)sender {
    (void)sender;
    auto& mgr = chq::net::MultiplayerManager::instance();
    if (mgr.state() != chq::net::MultiplayerState::Disconnected) {
        NSAlert* confirm = [[NSAlert alloc] init];
        confirm.messageText = @"Active Multiplayer Session";
        confirm.informativeText = @"A multiplayer session is already active. Disconnect and join a new match?";
        confirm.alertStyle = NSAlertStyleWarning;
        [confirm addButtonWithTitle:@"Disconnect & Join"];
        [confirm addButtonWithTitle:@"Cancel"];
        if ([confirm runModal] != NSAlertFirstButtonReturn) {
            return;
        }
        mgr.disconnect();
    }

    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = @"Join Internet Match";
    alert.informativeText = @"Enter the Room Code provided by the host:";
    alert.alertStyle = NSAlertStyleInformational;
    [alert addButtonWithTitle:@"Join Match"];
    [alert addButtonWithTitle:@"Cancel"];

    NSView* container = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 320, 85)];

    NSTextField* codeLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 60, 320, 18)];
    codeLabel.stringValue = @"Room Code:";
    codeLabel.editable = NO;
    codeLabel.bordered = NO;
    codeLabel.backgroundColor = [NSColor clearColor];
    [container addSubview:codeLabel];

    NSTextField* codeField = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 38, 320, 22)];
    codeField.placeholderString = @"e.g. WAR-ABCD";
    [container addSubview:codeField];

    NSTextField* relayLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 18, 320, 16)];
    relayLabel.stringValue = @"Relay Server (host:port or localhost):";
    relayLabel.font = [NSFont systemFontOfSize:11];
    relayLabel.editable = NO;
    relayLabel.bordered = NO;
    relayLabel.backgroundColor = [NSColor clearColor];
    [container addSubview:relayLabel];

    const char* env_relay = std::getenv("CHQ_RELAY_SERVER");
    NSString* defRelay = (env_relay && env_relay[0]) ? [NSString stringWithUTF8String:env_relay] : @"localhost:7190";
    NSTextField* relayField = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 320, 20)];
    relayField.stringValue = defRelay;
    [container addSubview:relayField];

    alert.accessoryView = container;

    if ([alert runModal] == NSAlertFirstButtonReturn) {
        std::string room_code = [codeField.stringValue UTF8String];
        std::string relay_str = [relayField.stringValue UTF8String];

        if (room_code.empty()) {
            NSAlert* err = [[NSAlert alloc] init];
            err.messageText = @"Missing Room Code";
            err.informativeText = @"Please enter a valid Room Code to join.";
            err.alertStyle = NSAlertStyleWarning;
            [err addButtonWithTitle:@"OK"];
            [err runModal];
            return;
        }

        std::string host = "localhost";
        int port = 7190;
        auto colon = relay_str.find(':');
        if (colon != std::string::npos) {
            host = relay_str.substr(0, colon);
            try {
                port = std::stoi(relay_str.substr(colon + 1));
            } catch (...) {
                port = 7190;
            }
        } else if (!relay_str.empty()) {
            host = relay_str;
        }

        if (!mgr.join_internet_game(host, port, room_code)) {
            NSAlert* err = [[NSAlert alloc] init];
            err.messageText = @"Connection Failed";
            err.informativeText = @"Unable to connect to the relay server or room.";
            err.alertStyle = NSAlertStyleCritical;
            [err addButtonWithTitle:@"OK"];
            [err runModal];
            return;
        }

        NSAlert* info = [[NSAlert alloc] init];
        info.messageText = @"Connecting to Room";
        info.informativeText = [NSString stringWithFormat:
            @"Connecting to room %@...\n\n"
            @"In Command HQ, select: Multiplayer -> MODEM -> Call (Dial).",
            [NSString stringWithUTF8String:room_code.c_str()]];
        info.alertStyle = NSAlertStyleInformational;
        [info addButtonWithTitle:@"OK"];
        [info runModal];
    }
}

- (void)directMatch:(nullable id)sender {
    (void)sender;
    auto& mgr = chq::net::MultiplayerManager::instance();
    if (mgr.state() != chq::net::MultiplayerState::Disconnected) {
        NSAlert* confirm = [[NSAlert alloc] init];
        confirm.messageText = @"Active Multiplayer Session";
        confirm.informativeText = @"A multiplayer session is already active. Disconnect?";
        confirm.alertStyle = NSAlertStyleWarning;
        [confirm addButtonWithTitle:@"Disconnect"];
        [confirm addButtonWithTitle:@"Cancel"];
        if ([confirm runModal] != NSAlertFirstButtonReturn) return;
        mgr.disconnect();
    }

    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = @"Direct LAN / IP Match";
    alert.informativeText = @"Connect directly over local network (LAN) or direct IP address:";
    alert.alertStyle = NSAlertStyleInformational;
    [alert addButtonWithTitle:@"Host (Listen)"];
    [alert addButtonWithTitle:@"Join (Connect)"];
    [alert addButtonWithTitle:@"Cancel"];

    NSView* container = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 320, 60)];
    NSTextField* hostLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 35, 320, 18)];
    hostLabel.stringValue = @"Remote Host (IP or hostname, for Join):";
    hostLabel.editable = NO;
    hostLabel.bordered = NO;
    hostLabel.backgroundColor = [NSColor clearColor];
    [container addSubview:hostLabel];

    NSTextField* hostField = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 15, 200, 20)];
    hostField.stringValue = @"127.0.0.1";
    [container addSubview:hostField];

    NSTextField* portField = [[NSTextField alloc] initWithFrame:NSMakeRect(210, 15, 100, 20)];
    portField.stringValue = @"7190";
    [container addSubview:portField];

    alert.accessoryView = container;

    NSModalResponse resp = [alert runModal];
    int port = 7190;
    try {
        port = std::stoi([portField.stringValue UTF8String]);
    } catch (...) {}

    if (resp == NSAlertFirstButtonReturn) {
        // Host (Listen)
        if (mgr.host_lan_game(port)) {
            NSAlert* info = [[NSAlert alloc] init];
            info.messageText = @"Listening for Direct Connection";
            info.informativeText = [NSString stringWithFormat:
                @"Listening on port %d.\nIn Command HQ, select: Multiplayer -> DIRECT or MODEM -> Answer.", port];
            [info addButtonWithTitle:@"OK"];
            [info runModal];
        }
    } else if (resp == NSAlertSecondButtonReturn) {
        // Join (Connect)
        std::string host = [hostField.stringValue UTF8String];
        if (mgr.join_lan_game(host, port)) {
            NSAlert* info = [[NSAlert alloc] init];
            info.messageText = @"Connecting to Direct Host";
            info.informativeText = [NSString stringWithFormat:
                @"Connecting to %s:%d...\nIn Command HQ, select: Multiplayer -> DIRECT or MODEM -> Call.", host.c_str(), port];
            [info addButtonWithTitle:@"OK"];
            [info runModal];
        }
    }
}

- (void)disconnectMultiplayer:(nullable id)sender {
    (void)sender;
    chq::net::MultiplayerManager::instance().disconnect();
}

- (void)revealSavesFolder:(nullable id)sender {
    (void)sender;
    if (self.callbacks.get_default_save_dir) {
        const char* dir = self.callbacks.get_default_save_dir(self.callbacks.context);
        if (dir && dir[0]) {
            NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:dir]];
            [[NSWorkspace sharedWorkspace] openURL:url];
        }
    }
}

- (void)dealloc {
    [self stopMenuTrackingTimer];
}

@end

namespace chq::macos {

bool is_fullscreen_by_default() {
    return [[NSUserDefaults standardUserDefaults] boolForKey:kPrefFullScreenByDefault];
}

void set_fullscreen_by_default(bool enabled) {
    [[NSUserDefaults standardUserDefaults] setBool:enabled forKey:kPrefFullScreenByDefault];
    [[NSUserDefaults standardUserDefaults] synchronize];
}

float get_saved_sound_volume() {
    id val = [[NSUserDefaults standardUserDefaults] objectForKey:kPrefSoundVolume];
    if (val == nil) return 1.0f;
    return [val floatValue];
}

bool get_saved_sound_muted() {
    return [[NSUserDefaults standardUserDefaults] boolForKey:kPrefSoundMuted];
}

void save_sound_preferences(float volume, bool muted) {
    [[NSUserDefaults standardUserDefaults] setFloat:volume forKey:kPrefSoundVolume];
    [[NSUserDefaults standardUserDefaults] setBool:muted forKey:kPrefSoundMuted];
    [[NSUserDefaults standardUserDefaults] synchronize];
}

bool should_prefill_manual_word() {
    id obj = [[NSUserDefaults standardUserDefaults] objectForKey:kPrefPrefillManualWord];
    return (obj != nil) ? [obj boolValue] : true;
}

void set_prefill_manual_word(bool enabled) {
    [[NSUserDefaults standardUserDefaults] setBool:enabled forKey:kPrefPrefillManualWord];
    [[NSUserDefaults standardUserDefaults] synchronize];
}

bool should_auto_submit_manual_word() {
    return [[NSUserDefaults standardUserDefaults] boolForKey:kPrefAutoSubmitManualWord];
}

void set_auto_submit_manual_word(bool enabled) {
    [[NSUserDefaults standardUserDefaults] setBool:enabled forKey:kPrefAutoSubmitManualWord];
    [[NSUserDefaults standardUserDefaults] synchronize];
}

void setup_macos_app(SDL_Window* window, const SoundBridgeCallbacks& callbacks) {
    (void)window;
    @autoreleasepool {
        NSDictionary* defaults = @{
            kPrefFullScreenByDefault: @NO,
            kPrefSoundMuted: @NO,
            kPrefSoundVolume: @1.0f,
            kPrefPrefillManualWord: @YES,
            kPrefAutoSubmitManualWord: @NO
        };
        [[NSUserDefaults standardUserDefaults] registerDefaults:defaults];

        if (!s_controller) {
            s_controller = [[ChqMacAppController alloc] init];
        }
        s_controller.callbacks = callbacks;

        NSMenu* mainMenu = [NSApp mainMenu];
        if (!mainMenu) return;

        // 1. Hook up the Application Menu's "Settings…" / "Preferences…" item
        if (mainMenu.numberOfItems > 0) {
            NSMenuItem* appMenuItem = [mainMenu itemAtIndex:0];
            if (appMenuItem.submenu) {
                for (NSMenuItem* item in appMenuItem.submenu.itemArray) {
                    if ([item.keyEquivalent isEqualToString:@","] ||
                        [item.title containsString:@"Settings"] ||
                        [item.title containsString:@"Preferences"]) {
                        item.target = s_controller;
                        item.action = @selector(openSettingsWindow:);
                    }
                }
            }
        }

        // 2. Add File Menu
        NSInteger fileIdx = [mainMenu indexOfItemWithTitle:@"File"];
        if (fileIdx < 0) {
            NSMenu* fileMenu = [[NSMenu alloc] initWithTitle:@"File"];

            NSMenuItem* saveItem = [[NSMenuItem alloc] initWithTitle:@"Save Game"
                                                              action:@selector(saveGame:)
                                                       keyEquivalent:@"s"];
            saveItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
            saveItem.target = s_controller;
            [fileMenu addItem:saveItem];

            NSMenuItem* saveAsItem = [[NSMenuItem alloc] initWithTitle:@"Save Game As…"
                                                                action:@selector(saveGameAs:)
                                                         keyEquivalent:@"S"];
            saveAsItem.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
            saveAsItem.target = s_controller;
            [fileMenu addItem:saveAsItem];

            [fileMenu addItem:[NSMenuItem separatorItem]];

            NSMenuItem* newItem = [[NSMenuItem alloc] initWithTitle:@"Restart Game"
                                                             action:@selector(newGame:)
                                                      keyEquivalent:@"n"];
            newItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
            newItem.target = s_controller;
            [fileMenu addItem:newItem];

            NSMenuItem* openItem = [[NSMenuItem alloc] initWithTitle:@"Import Saved Game…"
                                                              action:@selector(openGame:)
                                                       keyEquivalent:@"o"];
            openItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
            openItem.target = s_controller;
            [fileMenu addItem:openItem];

            NSMenuItem* revealItem = [[NSMenuItem alloc] initWithTitle:@"Reveal Saves Folder in Finder"
                                                                action:@selector(revealSavesFolder:)
                                                         keyEquivalent:@"O"];
            revealItem.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
            revealItem.target = s_controller;
            [fileMenu addItem:revealItem];

            NSMenuItem* fileTopItem = [[NSMenuItem alloc] initWithTitle:@"File" action:nil keyEquivalent:@""];
            fileTopItem.submenu = fileMenu;

            if (mainMenu.numberOfItems > 1) {
                [mainMenu insertItem:fileTopItem atIndex:1];
            } else {
                [mainMenu addItem:fileTopItem];
            }
        }

        // 3. Add Sound Menu
        NSInteger soundIdx = [mainMenu indexOfItemWithTitle:@"Sound"];
        if (soundIdx < 0) {
            NSMenu* soundMenu = [[NSMenu alloc] initWithTitle:@"Sound"];

            NSMenuItem* muteItem = [[NSMenuItem alloc] initWithTitle:@"Mute"
                                                              action:@selector(toggleMute:)
                                                       keyEquivalent:@"m"];
            muteItem.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
            muteItem.target = s_controller;
            [soundMenu addItem:muteItem];
            s_controller.soundMenuMuteItem = muteItem;

            [soundMenu addItem:[NSMenuItem separatorItem]];

            NSMenuItem* incItem = [[NSMenuItem alloc] initWithTitle:@"Increase Volume"
                                                             action:@selector(increaseVolume:)
                                                      keyEquivalent:@"="];
            incItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
            incItem.target = s_controller;
            [soundMenu addItem:incItem];
            s_controller.soundMenuIncreaseItem = incItem;

            NSMenuItem* decItem = [[NSMenuItem alloc] initWithTitle:@"Decrease Volume"
                                                             action:@selector(decreaseVolume:)
                                                      keyEquivalent:@"-"];
            decItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
            decItem.target = s_controller;
            [soundMenu addItem:decItem];
            s_controller.soundMenuDecreaseItem = decItem;

            [soundMenu addItem:[NSMenuItem separatorItem]];

            NSMenuItem* volIndicator = [[NSMenuItem alloc] initWithTitle:@"Volume: 100%"
                                                                  action:nil
                                                           keyEquivalent:@""];
            volIndicator.enabled = NO;
            [soundMenu addItem:volIndicator];
            s_controller.soundMenuVolumeIndicator = volIndicator;

            NSMenuItem* soundTopItem = [[NSMenuItem alloc] initWithTitle:@"Sound" action:nil keyEquivalent:@""];
            soundTopItem.submenu = soundMenu;

            NSInteger windowIdx = [mainMenu indexOfItemWithTitle:@"Window"];
            if (windowIdx >= 0) {
                [mainMenu insertItem:soundTopItem atIndex:windowIdx];
            } else {
                [mainMenu addItem:soundTopItem];
            }
        }

        // 4. Add Multiplayer Menu
        NSInteger mpIdx = [mainMenu indexOfItemWithTitle:@"Multiplayer"];
        if (mpIdx < 0) {
            NSMenu* mpMenu = [[NSMenu alloc] initWithTitle:@"Multiplayer"];

            NSMenuItem* hostItem = [[NSMenuItem alloc] initWithTitle:@"Host Internet Match…"
                                                              action:@selector(hostInternetMatch:)
                                                       keyEquivalent:@""];
            hostItem.target = s_controller;
            [mpMenu addItem:hostItem];
            s_controller.mpHostItem = hostItem;

            NSMenuItem* joinItem = [[NSMenuItem alloc] initWithTitle:@"Join Internet Match…"
                                                              action:@selector(joinInternetMatch:)
                                                       keyEquivalent:@""];
            joinItem.target = s_controller;
            [mpMenu addItem:joinItem];
            s_controller.mpJoinItem = joinItem;

            [mpMenu addItem:[NSMenuItem separatorItem]];

            NSMenuItem* directItem = [[NSMenuItem alloc] initWithTitle:@"Direct LAN / IP Match…"
                                                                action:@selector(directMatch:)
                                                         keyEquivalent:@""];
            directItem.target = s_controller;
            [mpMenu addItem:directItem];
            s_controller.mpDirectItem = directItem;

            [mpMenu addItem:[NSMenuItem separatorItem]];

            NSMenuItem* discItem = [[NSMenuItem alloc] initWithTitle:@"Disconnect"
                                                              action:@selector(disconnectMultiplayer:)
                                                       keyEquivalent:@""];
            discItem.target = s_controller;
            [mpMenu addItem:discItem];
            s_controller.mpDisconnectItem = discItem;

            [mpMenu addItem:[NSMenuItem separatorItem]];

            NSMenuItem* statusItem = [[NSMenuItem alloc] initWithTitle:@"Status: Inactive"
                                                                action:nil
                                                         keyEquivalent:@""];
            statusItem.enabled = NO;
            [mpMenu addItem:statusItem];
            s_controller.mpStatusItem = statusItem;

            NSMenuItem* mpTopItem = [[NSMenuItem alloc] initWithTitle:@"Multiplayer" action:nil keyEquivalent:@""];
            mpTopItem.submenu = mpMenu;

            NSInteger windowIdx = [mainMenu indexOfItemWithTitle:@"Window"];
            if (windowIdx >= 0) {
                [mainMenu insertItem:mpTopItem atIndex:windowIdx];
            } else {
                NSInteger helpPos = [mainMenu indexOfItemWithTitle:@"Help"];
                if (helpPos >= 0) {
                    [mainMenu insertItem:mpTopItem atIndex:helpPos];
                } else {
                    [mainMenu addItem:mpTopItem];
                }
            }
        }

        // 5. Add Help Menu
        NSInteger helpIdx = [mainMenu indexOfItemWithTitle:@"Help"];
        if (helpIdx < 0) {
            NSMenu* helpMenu = [[NSMenu alloc] initWithTitle:@"Help"];

            NSMenuItem* helpItem = [[NSMenuItem alloc] initWithTitle:@"Command HQ Help"
                                                              action:@selector(openHelpWindow:)
                                                       keyEquivalent:@"?"];
            helpItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
            helpItem.target = s_controller;
            [helpMenu addItem:helpItem];

            NSMenuItem* helpTopItem = [[NSMenuItem alloc] initWithTitle:@"Help" action:nil keyEquivalent:@""];
            helpTopItem.submenu = helpMenu;
            [mainMenu addItem:helpTopItem];

            [NSApp setHelpMenu:helpMenu];
        }

        // 5. Ensure menu bar and window controls appear when mouse moves to the top edge in full screen
        [[NSNotificationCenter defaultCenter] addObserverForName:NSWindowWillEnterFullScreenNotification
                                                          object:nil
                                                           queue:[NSOperationQueue mainQueue]
                                                      usingBlock:^(NSNotification * _Nonnull note) {
            (void)note;
            [NSMenu setMenuBarVisible:YES];
        }];

        [[NSNotificationCenter defaultCenter] addObserverForName:NSWindowDidEnterFullScreenNotification
                                                          object:nil
                                                           queue:[NSOperationQueue mainQueue]
                                                      usingBlock:^(NSNotification * _Nonnull note) {
            (void)note;
            [NSMenu setMenuBarVisible:YES];
        }];

        // 6. Run a 60 Hz timer during menu tracking so audio and emulation continue smoothly
        [[NSNotificationCenter defaultCenter] addObserverForName:NSMenuDidBeginTrackingNotification
                                                          object:nil
                                                           queue:[NSOperationQueue mainQueue]
                                                      usingBlock:^(NSNotification * _Nonnull note) {
            (void)note;
            [s_controller startMenuTrackingTimer];
        }];

        [[NSNotificationCenter defaultCenter] addObserverForName:NSMenuDidEndTrackingNotification
                                                          object:nil
                                                           queue:[NSOperationQueue mainQueue]
                                                      usingBlock:^(NSNotification * _Nonnull note) {
            (void)note;
            [s_controller stopMenuTrackingTimer];
        }];

        float vol = 1.0f;
        bool muted = false;
        if (callbacks.get_volume) vol = callbacks.get_volume(callbacks.context);
        if (callbacks.is_muted) muted = callbacks.is_muted(callbacks.context);
        [s_controller syncSoundUIWithVolume:vol muted:muted];
    }
}

void sync_macos_sound_ui(float volume, bool muted) {
    if (s_controller) {
        [s_controller syncSoundUIWithVolume:volume muted:muted];
    }
}

void show_settings_window() {
    if (s_controller) {
        [s_controller openSettingsWindow:nil];
    }
}

void show_help_window() {
    if (s_controller) {
        [s_controller openHelpWindow:nil];
    }
}

bool prompt_open_save_panel(char* out_path, std::size_t max_len, const char* default_dir) {
    if (!out_path || max_len == 0) return false;
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.title = @"Resume Saved War";
        panel.message = @"Select a Command HQ saved game file (CHQ-xx.SG) to resume:";
        panel.canChooseFiles = YES;
        panel.canChooseDirectories = NO;
        panel.allowsMultipleSelection = NO;
        panel.allowedFileTypes = @[ @"sg", @"SG" ];
        panel.allowsOtherFileTypes = YES;

        if (default_dir && default_dir[0]) {
            panel.directoryURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:default_dir]];
        }

        if ([panel runModal] == NSModalResponseOK) {
            NSString* path = panel.URL.path;
            if (path.length > 0 && path.length < max_len) {
                const char* cPath = [path UTF8String];
                if (!chq::validate_save_file(cPath)) {
                    NSAlert* alert = [[NSAlert alloc] init];
                    alert.messageText = @"Invalid Save File";
                    alert.informativeText = @"The selected file is not a valid Command HQ saved game (CHQ-xx.SG).";
                    alert.alertStyle = NSAlertStyleWarning;
                    [alert addButtonWithTitle:@"OK"];
                    [alert runModal];
                    return false;
                }
                std::strncpy(out_path, cPath, max_len - 1);
                out_path[max_len - 1] = '\0';
                return true;
            }
        }
        return false;
    }
}

bool prompt_save_as_panel(char* out_path, std::size_t max_len, const char* default_dir, const char* default_name) {
    if (!out_path || max_len == 0) return false;
    @autoreleasepool {
        NSSavePanel* panel = [NSSavePanel savePanel];
        panel.title = @"Save Game";
        panel.message = @"Choose a name and location to save your game:";
        panel.allowedFileTypes = @[ @"sg", @"SG" ];
        panel.allowsOtherFileTypes = YES;
        panel.canCreateDirectories = YES;

        if (default_dir && default_dir[0]) {
            panel.directoryURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:default_dir]];
        }
        if (default_name && default_name[0]) {
            panel.nameFieldStringValue = [NSString stringWithUTF8String:default_name];
        } else {
            panel.nameFieldStringValue = @"CHQ-00.SG";
        }

        if ([panel runModal] == NSModalResponseOK) {
            NSString* path = panel.URL.path;
            if (path.length > 0) {
                std::strncpy(out_path, [path UTF8String], max_len - 1);
                out_path[max_len - 1] = '\0';
                return true;
            }
        }
        return false;
    }
}

UnsavedPromptResult prompt_unsaved_changes_alert(const char* action_message) {
    @autoreleasepool {
        NSAlert* alert = [[NSAlert alloc] init];
        if (action_message && action_message[0]) {
            alert.messageText = [NSString stringWithUTF8String:action_message];
        } else {
            alert.messageText = @"Do you want to save the changes you made to this game?";
        }
        alert.informativeText = @"Your progress will be lost if you don't save.";
        alert.alertStyle = NSAlertStyleWarning;

        [alert addButtonWithTitle:@"Save"];       // First button -> NSAlertFirstButtonReturn
        [alert addButtonWithTitle:@"Cancel"];     // Second button -> NSAlertSecondButtonReturn
        [alert addButtonWithTitle:@"Don't Save"]; // Third button -> NSAlertThirdButtonReturn

        if (alert.buttons.count >= 3) {
            [alert.buttons[1] setKeyEquivalent:@"\033"];
            [alert.buttons[2] setKeyEquivalent:@"d"];
            [alert.buttons[2] setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
        }

        NSModalResponse res = [alert runModal];
        if (res == NSAlertFirstButtonReturn) return UnsavedPromptResult::Save;
        if (res == NSAlertThirdButtonReturn) return UnsavedPromptResult::DontSave;
        return UnsavedPromptResult::Cancel;
    }
}

void show_alert(const char* title, const char* message) {
    @autoreleasepool {
        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = [NSString stringWithUTF8String:title ? title : "Notice"];
        alert.informativeText = [NSString stringWithUTF8String:message ? message : ""];
        alert.alertStyle = NSAlertStyleInformational;
        [alert addButtonWithTitle:@"OK"];
        [alert runModal];
    }
}

std::optional<std::string> prompt_for_data_folder() {
    @autoreleasepool {
        [NSApp activateIgnoringOtherApps:YES];
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.title = @"Locate Command HQ Game Files";
        panel.message = @"Select the folder containing Command HQ game files (including CHQ.EXE and FONTS.CHQ):";
        panel.prompt = @"Select";
        panel.canChooseFiles = NO;
        panel.canChooseDirectories = YES;
        panel.allowsMultipleSelection = NO;
        panel.canCreateDirectories = NO;

        if ([panel runModal] == NSModalResponseOK) {
            NSString* path = panel.URL.path;
            if (path.length > 0) {
                return std::string([path UTF8String]);
            }
        }
        return std::nullopt;
    }
}

}  // namespace chq::macos
