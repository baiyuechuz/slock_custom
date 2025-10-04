/* user and group to drop privileges to */
static const char *user  = "nobody";
static const char *group = "nobody";

static const char *colorname[NUMCOLS] = {
	[INIT]		=	"#ff111827", /* after initialization */
	[INPUT]		=	"#ff1f2937", /* during input */
	[FAILED]	=	"#ff991b1b", /* wrong password */
	[CAPS]		=	"#fff59e0b", /* CapsLock on */
};

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
