/* Taken from https://github.com/djpohly/dwl/issues/466 */
#include <xkbcommon/xkbcommon-keysyms.h>
#define COLOR(hex)    { ((hex >> 24) & 0xFF) / 255.0f, \
                        ((hex >> 16) & 0xFF) / 255.0f, \
                        ((hex >> 8) & 0xFF) / 255.0f, \
                        (hex & 0xFF) / 255.0f }
/* appearance */
static const int sloppyfocus               = 1;  /* focus follows mouse */
static const int bypass_surface_visibility = 0;  /* 1 means idle inhibitors will disable idle tracking even if it's surface isn't visible  */
static const int smartgaps                 = 0;  /* 1 means no outer gap when there is only one window */
static int gaps                            = 1;  /* 1 means gaps between windows are added */
static const unsigned int gappx            = 10; /* gap pixel between windows */
static const unsigned int borderpx         = 2;  /* border pixel of windows */
static const float rootcolor[]             = COLOR(0x222222ff);
static const float bordercolor[]           = COLOR(0x444444ff);
static const float focuscolor[]            = COLOR(0x9ee9eaff);
static const float urgentcolor[]           = COLOR(0xa3dfafff);
static const float resize_factor           = 0.0002f; /* Resize multiplier for mouse resizing, depends on mouse sensivity. */
static const uint32_t resize_interval_ms   = 16; /* Resize interval depends on framerate and screen refresh rate. */
/* This conforms to the xdg-protocol. Set the alpha to zero to restore the old behavior */
static const float fullscreen_bg[]         = {0.1f, 0.1f, 0.1f, 1.0f}; /* You can also use glsl colors */

enum Direction { DIR_LEFT, DIR_RIGHT, DIR_UP, DIR_DOWN };
enum {
    VIEW_L = -1,
    VIEW_R = 1,
    SHIFT_L = -2,
    SHIFT_R = 2,
} RotateTags;

/* tagging - TAGCOUNT must be no greater than 31 */
#define TAGCOUNT (17)

/* logging */
static int log_level = WLR_ERROR;

/* NOTE: ALWAYS keep a rule declared even if you don't use rules (e.g leave at least one example) */
static const Rule rules[] = {
	/* app_id             title       tags mask     isfloating   monitor */
	/* examples: */
	{ "pavucontrol",                     NULL,       0,            1,           -1 }, /* Start on currently visible tags floating, not tiled */
	{ "qalculate-gtk",                   NULL,       0,            1,           -1 }, /* Start on currently visible tags floating, not tiled */
	{ "org.gnome.FileRoller",            NULL,       0,            1,           -1 }, /* Start on currently visible tags floating, not tiled */
	{ "gthumb",                          NULL,       0,            1,           -1 }, /* Start on currently visible tags floating, not tiled */
	{ "steam",                           NULL,       0,            1,           -1 }, /* Start on currently visible tags floating, not tiled */
	{ "mpv",							 NULL,       0,            1,           -1 }, /* Start on currently visible tags floating, not tiled */
	{ "Gimp_EXAMPLE",                    NULL,       0,            1,           -1 }, /* Start on currently visible tags floating, not tiled */
	{ "firefox_EXAMPLE",                 NULL,       1 << 8,       0,           -1 }, /* Start on ONLY tag "9" */
};

/* layout(s) */
static const Layout layouts[] = {
	/* symbol     arrange function */
	{ "|w|",      btrtile },
	{ "[]=",      tile },
	{ "><>",      NULL },    /* no layout function means floating behavior */
	{ "[M]",      monocle },
};

/* monitors */
/* (x=-1, y=-1) is reserved as an "autoconfigure" monitor position indicator
 * WARNING: negative values other than (-1, -1) cause problems with Xwayland clients
 * https://gitlab.freedesktop.org/xorg/xserver/-/issues/899
*/
/* NOTE: ALWAYS add a fallback rule, even if you are completely sure it won't be used */
static const MonitorRule monrules[] = {
	/* name       mfact  nmaster scale layout       rotate/reflect                x    y */
	/* example of a HiDPI laptop monitor:
	{ "eDP-1",    0.5f,  1,      2,    &layouts[0], WL_OUTPUT_TRANSFORM_NORMAL,   -1,  -1 },
	*/
	/* defaults */
	{ "DP-1",     0.50f, 1,      1,    &layouts[0], WL_OUTPUT_TRANSFORM_NORMAL,   -1,  -1 },
	{ NULL,       0.55f, 1,      1,    &layouts[0], WL_OUTPUT_TRANSFORM_NORMAL,   -1,  -1 },
};

/* keyboard */
static const struct xkb_rule_names xkb_rules = {
		/* can specify fields: rules, model, layout, variant, options */
		.layout = "us",
		.options = NULL,
};

static const int repeat_rate				= 25;
static const int repeat_delay				= 600;
/* Trackpad */
static const int tap_to_click				= 1;
static const int tap_and_drag				= 1;
static const int drag_lock					= 1;
static const int natural_scrolling			= 0;
static const int disable_while_typing		= 1;
static const int left_handed				= 0;
static const int middle_button_emulation 	= 0;
/* You can choose between:
LIBINPUT_CONFIG_SCROLL_NO_SCROLL
LIBINPUT_CONFIG_SCROLL_2FG
LIBINPUT_CONFIG_SCROLL_EDGE
LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN
*/
static const enum libinput_config_scroll_method scroll_method = LIBINPUT_CONFIG_SCROLL_2FG;

/* You can choose between:
LIBINPUT_CONFIG_CLICK_METHOD_NONE
LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS
LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER
*/
static const enum libinput_config_click_method click_method = LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;

/* You can choose between:
LIBINPUT_CONFIG_SEND_EVENTS_ENABLED
LIBINPUT_CONFIG_SEND_EVENTS_DISABLED
LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE
*/
static const uint32_t send_events_mode = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;

/* You can choose between:
LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT
LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE
*/
static const enum libinput_config_accel_profile accel_profile = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
static const double accel_speed = 0.0;

/* You can choose between:
LIBINPUT_CONFIG_TAP_MAP_LRM -- 1/2/3 finger tap maps to left/right/middle
LIBINPUT_CONFIG_TAP_MAP_LMR -- 1/2/3 finger tap maps to left/middle/right
*/
static const enum libinput_config_tap_button_map button_map = LIBINPUT_CONFIG_TAP_MAP_LRM;

/* If you want to use the windows key for MODKEY, use WLR_MODIFIER_LOGO */
#define MODKEY WLR_MODIFIER_LOGO

#define TAGKEYS(KEY,SKEY,TAG) \
	{ MODKEY,										KEY,    view,		{.ui = 1 << TAG} }, \
	{ MODKEY|WLR_MODIFIER_CTRL,						KEY,    toggleview,	{.ui = 1 << TAG} }, \
	{ MODKEY|WLR_MODIFIER_SHIFT,					SKEY,   tag,		{.ui = 1 << TAG} }, \
	{ MODKEY|WLR_MODIFIER_CTRL|WLR_MODIFIER_SHIFT,	SKEY,	toggletag,	{.ui = 1 << TAG} }

/* helper for spawning shell commands in the pre dwm-5.0 fashion */
#define SHCMD(cmd) { .v = (const char*[]){ "/bin/sh", "-c", cmd, NULL } }

/* commands */
static const char *termfoott[]			= { "foot", NULL };

static const char *termfoot[]			= { "footclient", NULL };
static const char *termst[]				= { "st", NULL };
static const char *menucmd[]			= { "wmenu", NULL };
static const char *files[]				= { "thunar", NULL };
static const char *editor[]				= { "geany", NULL };
static const char *steam[]				= { "/home/juftuf/.local/bin/launchsteam.sh", NULL };
static const char *lutris[]				= { "lutris", NULL };
static const char *discord[]			= { "discord", NULL };
static const char *pavucontrol[]		= { "pavucontrol", NULL };
static const char *virtualbox[]			= { "virtualbox", NULL };
static const char *whatsapp[]			= { "/home/juftuf/.local/bin/whatsapp.sh", NULL };
static const char *telegram[]			= { "telegram-desktop", NULL };
static const char *spotify[]			= { "spotify", "--enable-features=UseOzonePlatform", 
											"--ozone-platform=wayland", NULL };
static const char *killwine[]			= { "/home/juftuf/.local/bin/killwine.sh", NULL };
static const char *killsteam[]			= { "/home/juftuf/.local/bin/killsteam.sh", NULL };
static const char *alsaheadphones[]		= { "/home/juftuf/.local/bin/alsaheadphones.sh", NULL };
static const char *alsaspeakers[]		= { "/home/juftuf/.local/bin/alsaspeakers.sh", NULL };
static const char *vrron[]				= { "wlr-randr", "--output",
											"DP-1", "--on", "--mode", "5120x1440@120.000000Hz", "--adaptive-sync", "enabled", NULL };
static const char *vrroff[]				= { "wlr-randr", "--output",
											"DP-1", "--on", "--mode", "5120x1440@120.000000Hz", "--adaptive-sync", "disabled", NULL };
static const char *qalc[]				= { "qalculate-gtk", NULL };
static const char *obs[]				= { "/home/juftuf/.local/bin/obs.sh", NULL };
static const char *colorpicker[]		= { "/home/juftuf/.config/dwl/scripts/colorpicker", NULL };
static const char *browser[]			= { "firefox-bin", NULL };
static const char *lockscreen[]			= { "loginctl", "lock-session", NULL };
static const char *suspend[]			= { "loginctl", "suspend", NULL };
static const char *hibernate[]			= { "loginctl", "hibernate", NULL };
static const char *brightnessup[]		= { "brightnessctl", "s", "10%+", NULL };
static const char *brightnessdown[]	  	= { "brightnessctl", "s", "10%-", NULL };
static const char *volup[]				= { "/home/juftuf/.config/dwl/scripts/volume", "--inc", NULL };
static const char *voldown[]			= { "/home/juftuf/.config/dwl/scripts/volume", "--dec", NULL };
static const char *mute[]				= { "/home/juftuf/.config/dwl/scripts/volume", "--toggle", NULL };
static const char *mutemic[]			= { "/home/juftuf/.config/dwl/scripts/volume", "--toggle-mic", NULL };
static const char *spotifynext[]		= { "dbus-send", "--print-reply", "--dest=org.mpris.MediaPlayer2.spotify",
											"/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player.Next", NULL };
static const char *spotifyprev[]		= { "dbus-send", "--print-reply", "--dest=org.mpris.MediaPlayer2.spotify",
											"/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player.Previous", NULL };
static const char *spotifypause[]		= { "dbus-send", "--print-reply", "--dest=org.mpris.MediaPlayer2.spotify",
											"/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player.PlayPause", NULL };
static const char *spotifystop[]		= { "dbus-send", "--print-reply", "--dest=org.mpris.MediaPlayer2.spotify",
											"/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player.Stop", NULL };

static const Key keys[] = {
	/* Note that Shift changes certain key codes: c -> C, 2 -> at, etc. */
	/* modifier									key						function			argument */
	{ MODKEY,									XKB_KEY_r,				spawn,				{.v = menucmd} },
	{ MODKEY,									XKB_KEY_t,				spawn,				{.v = termfoot} },
	{ MODKEY|WLR_MODIFIER_CTRL,					XKB_KEY_t,				spawn,				{.v = termst} },
	{ MODKEY,									XKB_KEY_f,				spawn,				{.v = files} },
	{ MODKEY,									XKB_KEY_w,				spawn,				{.v = browser} },
	{ MODKEY,									XKB_KEY_e,				spawn,				{.v = editor} },
	{ MODKEY,									XKB_KEY_s,				spawn,				{.v = steam} },
	{ MODKEY,									XKB_KEY_g,				spawn,				{.v = lutris} },
	{ MODKEY,									XKB_KEY_d,				spawn,				{.v = discord} },
	{ MODKEY,									XKB_KEY_v,				spawn,				{.v = pavucontrol} },
	{ MODKEY,									XKB_KEY_b,				spawn,				{.v = virtualbox} },
	{ MODKEY,									XKB_KEY_a,				spawn,				{.v = whatsapp} },
	{ MODKEY,									XKB_KEY_m,				spawn,				{.v = spotify} },
	{ MODKEY,									XKB_KEY_F7,				spawn,				{.v = vrron} },
	{ MODKEY,									XKB_KEY_F8, 			spawn,				{.v = vrroff} },
	{ MODKEY,									XKB_KEY_F9, 			spawn,				{.v = alsaheadphones} },
	{ MODKEY,									XKB_KEY_F10,			spawn,				{.v = alsaspeakers} },
	{ MODKEY,									XKB_KEY_F11,			spawn,				{.v = killwine} },
	{ MODKEY,									XKB_KEY_F12,			spawn,				{.v = killsteam} },
	{ 0,	  									XKB_KEY_XF86Calculator,	spawn,				{.v = qalc} },
	{ MODKEY|WLR_MODIFIER_ALT,					XKB_KEY_a,				spawn,				{.v = telegram} },
	{ MODKEY|WLR_MODIFIER_ALT,					XKB_KEY_o,				spawn,				{.v = obs} },
	{ MODKEY,									XKB_KEY_p,				spawn,				{.v = colorpicker} },
	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT,		XKB_KEY_x,				spawn,				{.v = lockscreen} },
	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT,		XKB_KEY_p,				spawn,				{.v = suspend} },
	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT,		XKB_KEY_i,				spawn,				{.v = hibernate} },
	{ 0,XKB_KEY_XF86AudioRaiseVolume,									spawn,				{.v = volup} },
	{ 0,XKB_KEY_XF86AudioLowerVolume,									spawn,				{.v = voldown} },
	{ 0,XKB_KEY_XF86AudioMute,											spawn,				{.v = mute} },
	{ 0,XKB_KEY_XF86AudioMicMute,										spawn,				{.v = mutemic} },
	{ 0,XKB_KEY_XF86AudioNext,											spawn,				{.v = spotifynext} },
	{ 0,XKB_KEY_XF86AudioPrev,											spawn,				{.v = spotifyprev} },
	{ 0,XKB_KEY_XF86AudioPlay,											spawn,				{.v = spotifypause} },
	{ 0,XKB_KEY_XF86AudioStop,											spawn,				{.v = spotifystop} },
	{ 0,XKB_KEY_XF86MonBrightnessUp, 									spawn,				{.v = brightnessup} },
	{ 0,XKB_KEY_XF86MonBrightnessDown,									spawn,				{.v = brightnessdown} },

	{ MODKEY,									XKB_KEY_c,				killclient,			{0} },

	{ MODKEY|WLR_MODIFIER_SHIFT,				XKB_KEY_Up,				swapclients,		{.i = DIR_UP} },
	{ MODKEY|WLR_MODIFIER_SHIFT,				XKB_KEY_Down,			swapclients,		{.i = DIR_DOWN} },
	{ MODKEY|WLR_MODIFIER_SHIFT,				XKB_KEY_Right,			swapclients,		{.i = DIR_RIGHT} },
	{ MODKEY|WLR_MODIFIER_SHIFT,				XKB_KEY_Left,			swapclients,		{.i = DIR_LEFT} },
	{ MODKEY|WLR_MODIFIER_SHIFT,				XKB_KEY_K,				swapclients,		{.i = DIR_UP} },
	{ MODKEY|WLR_MODIFIER_SHIFT,				XKB_KEY_J,				swapclients,		{.i = DIR_DOWN} },
	{ MODKEY|WLR_MODIFIER_SHIFT,				XKB_KEY_L,				swapclients,		{.i = DIR_RIGHT} },
	{ MODKEY|WLR_MODIFIER_SHIFT,				XKB_KEY_H,				swapclients,		{.i = DIR_LEFT} },
	
	{ MODKEY|WLR_MODIFIER_CTRL,					XKB_KEY_Right,			setratio_h,			{.f = +0.025f} },
	{ MODKEY|WLR_MODIFIER_CTRL,					XKB_KEY_Left,			setratio_h,			{.f = -0.025f} },
	{ MODKEY|WLR_MODIFIER_CTRL,					XKB_KEY_Up,				setratio_v,			{.f = -0.025f} },
	{ MODKEY|WLR_MODIFIER_CTRL,					XKB_KEY_Down,	  		setratio_v,			{.f = +0.025f} },
	{ MODKEY|WLR_MODIFIER_CTRL,					XKB_KEY_l,				setratio_h,			{.f = +0.025f} },
	{ MODKEY|WLR_MODIFIER_CTRL,					XKB_KEY_h,				setratio_h,			{.f = -0.025f} },
	{ MODKEY|WLR_MODIFIER_CTRL,					XKB_KEY_k,				setratio_v,			{.f = -0.025f} },
	{ MODKEY|WLR_MODIFIER_CTRL,					XKB_KEY_j,				setratio_v,			{.f = +0.025f} },

	{ MODKEY,									XKB_KEY_Left,			focusdir,			{.ui = 0} },
	{ MODKEY,									XKB_KEY_Right,			focusdir,			{.ui = 1} },
	{ MODKEY,									XKB_KEY_Up,	  			focusdir,			{.ui = 2} },
	{ MODKEY,									XKB_KEY_Down,			focusdir,			{.ui = 3} },
	{ MODKEY,									XKB_KEY_h,	 			focusdir,			{.ui = 0} },
	{ MODKEY,									XKB_KEY_l,				focusdir,			{.ui = 1} },
	{ MODKEY,									XKB_KEY_k,				focusdir,			{.ui = 2} },
	{ MODKEY,									XKB_KEY_j,				focusdir,			{.ui = 3} },
	
	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT,		XKB_KEY_Left,			rotatetags,			{.i = VIEW_L} },
	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT,		XKB_KEY_Right,			rotatetags,			{.i = VIEW_R} },
	{ WLR_MODIFIER_ALT|WLR_MODIFIER_SHIFT,		XKB_KEY_Left,			rotatetags,			{.i = SHIFT_L} },
	{ WLR_MODIFIER_ALT|WLR_MODIFIER_SHIFT,		XKB_KEY_Right,			rotatetags,			{.i = SHIFT_R} },

	{ WLR_MODIFIER_ALT|WLR_MODIFIER_SHIFT,		XKB_KEY_exclam,			setlayout,			{.v = &layouts[0]} },
	{ WLR_MODIFIER_ALT|WLR_MODIFIER_SHIFT,		XKB_KEY_at,				setlayout,			{.v = &layouts[1]} },
	{ WLR_MODIFIER_ALT|WLR_MODIFIER_SHIFT,		XKB_KEY_numbersign,		setlayout,			{.v = &layouts[2]} },
	{ WLR_MODIFIER_ALT|WLR_MODIFIER_SHIFT,		XKB_KEY_dollar,			setlayout,			{.v = &layouts[3]} },

	{ MODKEY,									XKB_KEY_space,			togglefloating,		{0} },
	{ MODKEY,									XKB_KEY_Return,			togglefullscreen,	{0} },
	{ MODKEY,									XKB_KEY_0,				view,				{.ui = ~0} },
	{ MODKEY|WLR_MODIFIER_SHIFT,				XKB_KEY_parenright,		tag,				{.ui = ~0} },

	{ MODKEY,									XKB_KEY_b,				togglebar,			{0} },
	{ MODKEY,									XKB_KEY_i,				incnmaster,			{.i = +1} },
	{ MODKEY|WLR_MODIFIER_SHIFT,				XKB_KEY_i,				incnmaster,			{.i = -1} },
	{ MODKEY,									XKB_KEY_Return,			zoom,				{0} },
	{ MODKEY,									XKB_KEY_Tab,			view,				{0} },
	{ MODKEY,									XKB_KEY_g,				togglegaps,			{0} },
	{ MODKEY,									XKB_KEY_j,				focusstack,			{.i = +1} },
	{ MODKEY,									XKB_KEY_k,				focusstack,			{.i = -1} },
	{ MODKEY,									XKB_KEY_h,				setmfact,			{.f = -0.05f} },
	{ MODKEY,									XKB_KEY_l,				setmfact,			{.f = +0.05f} },
	{ MODKEY,									XKB_KEY_comma, 			focusmon,			{.i = WLR_DIRECTION_LEFT} },
	{ MODKEY,									XKB_KEY_period,			focusmon,			{.i = WLR_DIRECTION_RIGHT} },
	{ MODKEY|WLR_MODIFIER_SHIFT,				XKB_KEY_less,			tagmon,				{.i = WLR_DIRECTION_LEFT} },
	{ MODKEY|WLR_MODIFIER_SHIFT,				XKB_KEY_greater,		tagmon,				{.i = WLR_DIRECTION_RIGHT} },
	TAGKEYS(									XKB_KEY_1,				XKB_KEY_exclam,		0),
	TAGKEYS(									XKB_KEY_2,				XKB_KEY_at,			1),
	TAGKEYS(									XKB_KEY_3,				XKB_KEY_numbersign,	2),
	TAGKEYS(									XKB_KEY_4,				XKB_KEY_dollar,		3),
	TAGKEYS(									XKB_KEY_5,				XKB_KEY_percent,	4),
	TAGKEYS(									XKB_KEY_6,				XKB_KEY_asciicircum,5),
	TAGKEYS(									XKB_KEY_7,				XKB_KEY_ampersand,	6),
	TAGKEYS(									XKB_KEY_8,				XKB_KEY_asterisk,	7),
	TAGKEYS(									XKB_KEY_9,				XKB_KEY_parenleft,	8),
	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT,		XKB_KEY_Delete,			quit,				{0} },

	/* Ctrl-Alt-Backspace and Ctrl-Alt-Fx used to be handled by X server */
	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT,		XKB_KEY_Terminate_Server, quit,				{0} },
	/* Ctrl-Alt-Fx is used to switch to another VT, if you don't know what a VT is
	 * do not remove them.
	 */
#define CHVT(n) { WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT,XKB_KEY_XF86Switch_VT_##n, chvt, {.ui = (n)} }
	CHVT(1), CHVT(2), CHVT(3), CHVT(4), CHVT(5), CHVT(6),
	CHVT(7), CHVT(8), CHVT(9), CHVT(10), CHVT(11), CHVT(12),
};

static const Button buttons[] = {
	{ MODKEY,			BTN_LEFT, 	  moveresize,		{.ui = CurMove} },
	{ MODKEY,			BTN_MIDDLE,	  togglefloating,	{0} },
	{ MODKEY,			BTN_RIGHT,	  moveresize,		{.ui = CurResize} },
};
