/* user and group to drop privileges to */
static const char *user  = "nobody";
static const char *group = "nobody";

static const char *colorname[NUMCOLS] = {
	[INIT]		=	"#cc0b1221", /* after initialization */
	[INPUT]		=	"#cc004466", /* during input */
	[FAILED]	=	"#cc660000", /* wrong password */
	[CAPS]		=	"#ccCC6600", /* CapsLock on */
};

/* alpha value for background (00 = fully transparent, ff = fully opaque) */
static const unsigned int alpha = 0xcc;

/* treat a cleared input like a wrong password (color) */
static const int failonclear = 1;

/* default message */
static const char * message = "BAIYUECHU\n baiyuechuz - 󰖟 baiyuechu.dev\n\nEnter password to unlock the screen";

/* text color */
static const char * text_color = "#E5E6E5";

/* text size (must be a valid size) */
static const char * font_name = "JetBrainsMono Nerd Font:size=18:antialias=true:hinting=true:rgba=true";

/* larger font for first line */
static const char * font_name_large = "JetBrainsMono Nerd Font:size=32:antialias=true:hinting=true:rgba=true";
