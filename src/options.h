#ifndef OPTIONS_H_
#define OPTIONS_H_

#include <stdint.h>

#define GB_B_BLANK 0.8
#define GB_B_BEGIN 0.0
#define GB_C_COLUMN 4
#define GB_C_CUT -1
#define GB_D_DEPTH -1
#define GB_D_EDGE 12
#define GB_E_END 0.0

#ifndef GB_F_FONTNAME
#ifdef __APPLE__
#	define GB_F_FONTNAME "Tahoma Bold.ttf"
#else
#ifdef _WIN32
#	define GB_F_FONTNAME "tahomabd.ttf"
#else
#	define GB_F_FONTNAME "DejaVuSans.ttf"
#endif
#endif
#endif

#define GB_G_GAP 0
#define GB_H_HEIGHT 150
#define GB_I_INFO 1
#define GB_I_INDIVIDUAL 0
#define GB_J_QUALITY 90
#define GB_K_BCOLOR 0xFFFFFF
#define GB_L_INFO_LOCATION 4
#define GB_L_TIME_LOCATION 1
#define GB_N_NORMAL_PRIO 0
#define GB_X_FILENAME_USE_FULL 0
#define GB_O_SUFFIX ".jpg"
#define GB_P_PAUSE 0
#define GB_P_DONTPAUSE 0
#define GB_Q_QUIET 0
#define GB_R_ROW 4
#define GB_S_STEP 120
#define GB_S_SELECT_VIDEO_STREAM 0
#define GB_T_TIME 1
#define GB_V_VERBOSE 0
#define GB_W_WIDTH 1024
#define GB_W_OVERWRITE 1
#define GB_Z_SEEK 0
#define GB_Z_NONSEEK 0

#define COLOR_INFO  0x555555
#define COLOR_WHITE 0xFFFFFF
#define COLOR_BLACK 0x000000
#define COLOR_GREY  0x808080

typedef struct AVDictionary AVDictionary;

struct options
{
    int a_ratio_num;
    int a_ratio_den;
    double b_blank;
    double B_begin; // skip this seconds from the beginning
    int c_column;
    double C_cut;
    int d_depth; // directory depth
    int D_edge; // edge detection; 0 off; >0 on
    double E_end; // skip this seconds at the end
    const char *f_fontname;
    uint32_t F_info_color; // info color
    double F_info_font_size; // info font size
    const char *F_ts_fontname; // time stamp fontname
    uint32_t F_ts_color; // time stamp color
    uint32_t F_ts_shadow; // time stamp shadow color
    double F_ts_font_size; // time stamp font size
    int g_gap;
    int h_height; // mininum height of each shot; will reduce # of column to meet this height
    int H_human_filesize; // filesize only in human readable size (KiB, MiB, GiB)
    int i_info; // 1 on; 0 off
    int I_individual;  // 1 on; 0 off
    int I_individual_thumbnail;   // 1 on; 0 off
    int I_individual_original;    // 1 on; 0 off
    int I_individual_ignore_grid; // 1 on; 0 off
    int j_quality;
    uint32_t k_bcolor; // background color
    int L_info_location;
    int L_time_location;
    int n_normal; // normal priority; 1 normal; 0 lower
    const char *N_suffix; // info text file suffix
    int X_filename_use_full; // use full input filename (include extension)
    const char *o_suffix;
    const char *O_outdir;
    int p_pause; // pause before exiting; 1 pause; 0 don't pause
    int P_dontpause; // don't pause; override p_pause
    int q_quiet; // 1 on; 0 off
    int r_row; // 0 = as many rows as needed
    int s_step; // less than 0 = every frame; 0 = step evenly to get column x row
    int S_select_video_stream;
    int t_timestamp; // 1 on; 0 off
    const char *T_text;
    int v_verbose; // 1 on; 0 off
    int V; // 1 on; 0 off
    int w_width; // 0 = column * movie width
    int W_overwrite; // 1 = overwrite; 0 = don't overwrite
    int z_seek; // always use seek mode; 1 on; 0 off
    int Z_nonseek; // always use non-seek mode; 1 on; 0 off

    // long command line options
    int shadow; // -1 off, 0 auto, >0 manual
    int transparent_bg; //  0 off, 1 on
    int cover; //  album art (cover image)
    int webvtt;
    const char *cover_suffix;
    const char *webvtt_prefix;
    AVDictionary *dict;
};

char* mtn_identification();
void init_options(struct options *o);
void free_options(struct options *o);
int parse_options(struct options *o, int argc, char *argv[], int *start_index);

#define RGB_R(c) (((c) >> 16) & 0xFF)
#define RGB_G(c) (((c) >> 8) & 0xFF)
#define RGB_B(c) ((c) & 0xFF)

#endif // OPTIONS_H_
