#include "options.h"
#include <string.h>
#include <stdlib.h>
#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <gd.h>
#include <getopt.h>

extern const char *gb_argv0;
extern const char *gb_version;

void init_options(struct options *o)
{
    o->a_ratio_num = 0;
    o->a_ratio_den = 1;
    o->b_blank = GB_B_BLANK;
    o->B_begin = GB_B_BEGIN;
    o->c_column = GB_C_COLUMN;
    o->C_cut = GB_C_CUT;
    o->d_depth = GB_D_DEPTH;
    o->D_edge = GB_D_EDGE;
    o->E_end = GB_E_END;
    o->f_fontname = strdup(GB_F_FONTNAME);
    o->F_info_color = COLOR_INFO;
    o->F_info_font_size = 9;
    o->F_ts_fontname = strdup(GB_F_FONTNAME);
    o->F_ts_color = COLOR_WHITE;
    o->F_ts_shadow = COLOR_BLACK;
    o->F_ts_font_size = 8;
    o->g_gap = GB_G_GAP;
    o->h_height = GB_H_HEIGHT;
    o->H_human_filesize = 0;
    o->i_info = GB_I_INFO;
    o->I_individual = GB_I_INDIVIDUAL;
    o->I_individual_thumbnail = 0;
    o->I_individual_original = 0;
    o->I_individual_ignore_grid = 0;
    o->j_quality = GB_J_QUALITY;
    o->k_bcolor = GB_K_BCOLOR;
    o->L_info_location = GB_L_INFO_LOCATION;
    o->L_time_location = GB_L_TIME_LOCATION;
    o->n_normal = GB_N_NORMAL_PRIO;
    o->N_suffix = NULL;
    o->X_filename_use_full = GB_X_FILENAME_USE_FULL;
    o->o_suffix = strdup(GB_O_SUFFIX);
    o->O_outdir = NULL;
    o->p_pause = GB_P_PAUSE;
    o->P_dontpause = GB_P_DONTPAUSE;
    o->q_quiet = GB_Q_QUIET;
    o->r_row = GB_R_ROW;
    o->s_step = GB_S_STEP;
    o->S_select_video_stream = GB_S_SELECT_VIDEO_STREAM;
    o->t_timestamp = GB_T_TIME;
    o->T_text = NULL;
    o->v_verbose = GB_V_VERBOSE;
    o->V = GB_V_VERBOSE;
    o->w_width = GB_W_WIDTH;
    o->W_overwrite = GB_W_OVERWRITE;
    o->z_seek = GB_Z_SEEK;
    o->Z_nonseek = GB_Z_NONSEEK;
    o->shadow = -1;
    o->transparent_bg = 0;
    o->cover = 0;
    o->webvtt = 0;
    o->cover_suffix = strdup("_cover.jpg");
    o->webvtt_prefix = strdup("");
    o->dict = NULL;
}

static int get_location_opt(struct options *o, char c, char *optarg)
{
    int ret = 1;
    char *bak = strdup(optarg); // backup for displaying error
    if (!bak)
    {
        av_log(NULL, AV_LOG_ERROR, "%s: strdup failed\n", gb_argv0);
        return ret;
    }

    const char *delim = ":";
    char *tailptr;

    // info text location
    char *token = strtok(optarg, delim);
    if (!token)
        goto cleanup;

    o->L_info_location = strtol(token, &tailptr, 10);
    if (*tailptr)
        goto cleanup; // error

    // time stamp location
    token = strtok(NULL, delim);
    if (!token)
    {
        ret = 0; // time stamp format is optional
        goto cleanup;
    }
    o->L_time_location = strtol(token, &tailptr, 10);
    if (*tailptr)
        goto cleanup; // error

    ret = 0;

  cleanup:
    if (ret)
        av_log(NULL, AV_LOG_ERROR, "%s: argument for option -%c is invalid at '%s'\n", gb_argv0, c, bak);
    free(bak);
    return ret;
}

/*
check and convert string to RGB color, must be in the correct format RRGGBB (in hex)
return -1 if error
*/
static int parse_color(uint32_t *rgb, const char *str)
{
    if (!str || strlen(str) < 6)
        return -1;

    *rgb = 0;
    unsigned i;
    for (i = 0; i < 6; i++)
    {
        unsigned digit;
        if (str[i] >= '0' && str[i] <= '9')
            digit = str[i] - '0';
        else if (str[i] >= 'a' && str[i] <= 'f')
            digit = str[i] - 'a' + 10;
        else if (str[i] >= 'A' && str[i] <= 'F')
            digit = str[i] - 'A' + 10;
        else
            return -1;
        *rgb = *rgb << 4 | digit;
    }
    return 0;
}

static int get_color_opt(uint32_t *color, char c, char *optarg)
{
    if (parse_color(color, optarg) == -1)
    {
        av_log(NULL, AV_LOG_ERROR, "%s: argument for option -%c is invalid at '%s' -- must be RRGGBB in hex\n", gb_argv0, c, optarg);
        return 1;
    }
    return 0;
}

static int get_format_opt(struct options *o, char c, char *optarg)
{
    int ret = 1;
    char *bak = strdup(optarg); // backup for displaying error
    if (!bak)
    {
        av_log(NULL, AV_LOG_ERROR, "%s: strdup failed\n", gb_argv0);
        return ret;
    }

    const char *delim = ":";

    // info text font color
    char *token = strtok(optarg, delim);
    if (!token || parse_color(&o->F_info_color, token) == -1)
        goto cleanup;

    // info text font size
    token = strtok(NULL, delim);
    if (!token)
        goto cleanup;

    char *tailptr;
    o->F_info_font_size = strtol(token, &tailptr, 10);
    if (*tailptr)
        goto cleanup; // error

    // time stamp font
    token = strtok(NULL, delim);
    if (!token)
    {
        ret = 0; // time stamp format is optional
        free((char *) o->F_ts_fontname);
        o->F_ts_fontname = strdup(o->f_fontname);
        o->F_ts_font_size = o->F_info_font_size - 1;
        goto cleanup;
    }
    free((char *) o->F_ts_fontname);
    o->F_ts_fontname = strdup(token);
    // time stamp font color
    token = strtok(NULL, delim);
    if (!token || parse_color(&o->F_ts_color, token) == -1)
        goto cleanup;

    // time stamp shadow color
    token = strtok(NULL, delim);
    if (!token || parse_color(&o->F_ts_shadow, token) == -1)
        goto cleanup;

    // time stamp font size
    token = strtok(NULL, delim);
    if (!token)
        goto cleanup;

    o->F_ts_font_size = strtol(token, &tailptr, 10);
    if (*tailptr)
        goto cleanup; // error

    ret = 0;

  cleanup:
    if (ret)
    {
        av_log(NULL, AV_LOG_ERROR, "%s: argument for option -%c is invalid at '%s'\n", gb_argv0, c, bak);
        av_log(NULL, AV_LOG_ERROR, "examples:\n");
        av_log(NULL, AV_LOG_ERROR, "info text blue color size 10:\n  -%c 0000FF:10\n", c);
        av_log(NULL, AV_LOG_ERROR, "info text green color size 12; time stamp font comicbd.ttf yellow color black shadow size 8 :\n  -%c 00FF00:12:comicbd.ttf:ffff00:000000:8\n", c);
    }
    free(bak);
    return ret;
}

static int get_opt_for_I_arg(struct options *o, const char *optarg)
{
    if (strchr(optarg, '-'))
    {
        av_log(NULL, AV_LOG_ERROR, "Missing argument for -I option!");
        return 1;
    }

    if (strchr(optarg, 't') || strchr(optarg, 'T'))
        o->I_individual_thumbnail = 1;

    if (strchr(optarg, 'o') || strchr(optarg, 'O'))
        o->I_individual_original  = 1;

    if (strchr(optarg, 'i') || strchr(optarg, 'I'))
        o->I_individual_ignore_grid = 1;

    if (o->I_individual_thumbnail + o->I_individual_original + o->I_individual_ignore_grid == 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Unknown argument \"%s\" for -I option!", optarg);
        return 1;
    }

    return 0;
}

static int get_int_opt(char *optname, int *opt, char *optarg, int sign)
{
    char *tailptr;
    int ret = strtol(optarg, &tailptr, 10);
    if (*tailptr)
    {
        av_log(NULL, AV_LOG_ERROR, "%s: argument for option -%s is invalid at '%s'\n", gb_argv0, optname, tailptr);
        return 1;
    }
    if (sign > 0 && ret <= 0)
    {
        av_log(NULL, AV_LOG_ERROR, "%s: argument for option -%s must be > 0\n", gb_argv0, optname);
        return 1;
    }
    if (sign == 0 && ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "%s: argument for option -%s must be >= 0\n", gb_argv0, optname);
        return 1;
    }
    *opt = ret;
    return 0;
}

static int get_double_opt(char c, double *opt, char *optarg, double sign)
{
    char *tailptr;
    double ret = strtod(optarg, &tailptr);
    if (*tailptr)
    {
        av_log(NULL, AV_LOG_ERROR, "%s: argument for option -%c is invalid at '%s'\n", gb_argv0, c, tailptr);
        return 1;
    }
    if (sign > 0 && ret <= 0)
    {
        av_log(NULL, AV_LOG_ERROR, "%s: argument for option -%c must be > 0\n", gb_argv0, c);
        return 1;
    }
    if (sign == 0.0 && ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "%s: argument for option -%c must be >= 0\n", gb_argv0, c);
        return 1;
    }
    *opt = ret;
    return 0;
}

char* mtn_identification()
{
    const char txt[] = "Movie Thumbnailer (mtn) %s\nCompiled%s with: %s %s %s %s GD:%s";
    const char GD_VER[] =
       #ifdef GD_VERSION_STRING
           GD_VERSION_STRING
       #else
           GD2_ID
        #endif
    ;
    const char STATIC_MSG[] =
        #ifdef MTN_STATIC
            " statically"
        #else
            ""
        #endif
            ;
    size_t s = snprintf(NULL, 0, txt, gb_version, STATIC_MSG, LIBAVCODEC_IDENT, LIBAVFORMAT_IDENT, LIBAVUTIL_IDENT, LIBSWSCALE_IDENT, GD_VER) +1;
    char* msg = malloc(s);
               snprintf( msg, s, txt, gb_version, STATIC_MSG, LIBAVCODEC_IDENT, LIBAVFORMAT_IDENT, LIBAVUTIL_IDENT, LIBSWSCALE_IDENT, GD_VER);
    return msg;
}

void usage()
{
    char *ident = mtn_identification();
    av_log(NULL, AV_LOG_INFO, "\n%s\n\n", ident);
#ifndef DEBUG
    av_log(NULL, AV_LOG_INFO, "Mtn saves thumbnails of specified movie files or directories to image files.\n");
    av_log(NULL, AV_LOG_INFO, "For directories, it will recursively search inside for movie files.\n\n");
    av_log(NULL, AV_LOG_INFO, "Usage:\n  %s [options] file_or_dir1 [file_or_dir2] ... [file_or_dirn]\n", gb_argv0);
    av_log(NULL, AV_LOG_INFO, "Options: (and default values)\n");
    av_log(NULL, AV_LOG_INFO, "  -a aspect_ratio : override input file's display aspect ratio\n");
    av_log(NULL, AV_LOG_INFO, "  -b %.2f : skip if %% blank is higher; 0:skip all 1:skip really blank >1:off\n", GB_B_BLANK);
    av_log(NULL, AV_LOG_INFO, "  -B %.1f : omit this seconds from the beginning\n", GB_B_BEGIN);
    av_log(NULL, AV_LOG_INFO, "  -c %d : # of column\n", GB_C_COLUMN);
    av_log(NULL, AV_LOG_INFO, "  -C %d : cut movie and thumbnails not more than the specified seconds; <=0:off\n", GB_C_CUT);
    av_log(NULL, AV_LOG_INFO, "  -d #: recursion depth; 0:immediate children files only\n");
    av_log(NULL, AV_LOG_INFO, "  -D %d : edge detection; 0:off >0:on; higher detects more; try -D4 -D6 or -D8\n", GB_D_EDGE);
    //av_log(NULL, AV_LOG_ERROR, "  -e : to be done\n"); // extension of movie files
    av_log(NULL, AV_LOG_INFO, "  -E %.1f : omit this seconds at the end\n", GB_E_END);
    av_log(NULL, AV_LOG_INFO, "  -f %s : font file; use absolute path if not in usual places\n", GB_F_FONTNAME);
    av_log(NULL, AV_LOG_INFO, "  -F RRGGBB:size[:font:RRGGBB:RRGGBB:size] : font format [time is optional]\n     info_color:info_size[:time_font:time_color:time_shadow:time_size]\n");
    av_log(NULL, AV_LOG_INFO, "  -g %d : gap between each shot\n", GB_G_GAP);
    av_log(NULL, AV_LOG_INFO, "  -h %d : minimum height of each shot; will reduce # of column to fit\n", GB_H_HEIGHT);
    av_log(NULL, AV_LOG_INFO, "  -H : filesize only in human readable format (MiB, GiB). Default shows size in bytes too\n");
    av_log(NULL, AV_LOG_INFO, "  -i : info text off\n");
    av_log(NULL, AV_LOG_INFO, "  -I {toi}: save individual shots; t - thumbnail size, o - original size, i - ignore creating thumbnail grid\n");
    av_log(NULL, AV_LOG_INFO, "  -j %d : jpeg quality\n", GB_J_QUALITY);
    av_log(NULL, AV_LOG_INFO, "  -k RRGGBB : background color (in hex)\n"); // backgroud color
    av_log(NULL, AV_LOG_INFO, "  -L info_location[:time_location] : location of text\n     1=lower left, 2=lower right, 3=upper right, 4=upper left\n");
    av_log(NULL, AV_LOG_INFO, "  -n : run at normal priority\n");
    av_log(NULL, AV_LOG_INFO, "  -N info_suffix : save info text to a file with suffix\n");
    av_log(NULL, AV_LOG_INFO, "  -o %s : output suffix including image extension (.jpg or .png)\n", GB_O_SUFFIX);
    av_log(NULL, AV_LOG_INFO, "  -O directory : save output files in the specified directory\n");
    av_log(NULL, AV_LOG_INFO, "  -p : pause before exiting\n");
    av_log(NULL, AV_LOG_INFO, "  -P : don't pause before exiting; override -p\n");
    av_log(NULL, AV_LOG_INFO, "  -q : quiet mode (print only error messages)\n");
    av_log(NULL, AV_LOG_INFO, "  -r %d : # of rows; >0:override -s\n", GB_R_ROW);
    av_log(NULL, AV_LOG_INFO, "  -s %d : time step between each shot\n", GB_S_STEP);
    av_log(NULL, AV_LOG_INFO, "  -S #: select specific stream number\n");
    av_log(NULL, AV_LOG_INFO, "  -t : time stamp off\n");
    av_log(NULL, AV_LOG_INFO, "  -T text : add text above output image\n");
    av_log(NULL, AV_LOG_INFO, "  -v : verbose mode (debug)\n");
    av_log(NULL, AV_LOG_INFO, "  -w %d : width of output image; 0:column * movie width\n", GB_W_WIDTH);
    av_log(NULL, AV_LOG_INFO, "  -W : don't overwrite existing files, i.e. update mode\n");
    av_log(NULL, AV_LOG_INFO, "  -X : use full input filename (include extension)\n");
    av_log(NULL, AV_LOG_INFO, "  -z : always use seek mode\n");
    av_log(NULL, AV_LOG_INFO, "  -Z : always use non-seek mode -- slower but more accurate timing\n");
    av_log(NULL, AV_LOG_INFO, "  --shadow[=N]\n       draw shadows beneath thumbnails with radius N pixels if N >0; Radius is calculated if N=0 or N is omitted\n");
    av_log(NULL, AV_LOG_INFO, "  --transparent\n       set background color (-k) to transparent; works with PNG image only \n");
    av_log(NULL, AV_LOG_INFO, "  --cover[=_cover.jpg]\n       extract album art if exists \n");
    av_log(NULL, AV_LOG_INFO, "  --vtt[=path in .vtt]\n       export WebVTT file and sprite chunks\n");
    av_log(NULL, AV_LOG_INFO, "  --options=option_entries\n       list of options passed to the FFmpeg library. option_entries contains list of options separated by \"|\". Each option contains name and value separated by \":\".\n");
    av_log(NULL, AV_LOG_INFO, "  file_or_dirX\n       name of the movie file or directory containing movie files\n\n");
#ifdef _WIN32
    av_log(NULL, AV_LOG_INFO, "Examples:\n");
    av_log(NULL, AV_LOG_INFO, "  to save thumbnails to file infile%s with default options:\n    %s infile.avi\n", GB_O_SUFFIX, gb_argv0);
    av_log(NULL, AV_LOG_INFO, "  to change time step to 65 seconds & change total width to 900:\n    %s -s 65 -w 900 infile.avi\n", gb_argv0);
    av_log(NULL, AV_LOG_INFO, "  to step evenly to get 3 columns x 10 rows:\n    %s -c 3 -r 10 infile.avi\n", gb_argv0);
    av_log(NULL, AV_LOG_INFO, "  to save output files to writeable directory:\n    %s -O writeable /read/only/dir/infile.avi\n", gb_argv0);
    av_log(NULL, AV_LOG_INFO, "  to get 2 columns in original movie size:\n    %s -c 2 -w 0 infile.avi\n", gb_argv0);
    av_log(NULL, AV_LOG_INFO, "  to skip uninteresting shots, try:\n    %s -D 6 infile.avi\n", gb_argv0);
    av_log(NULL, AV_LOG_INFO, "  to save only individual shots and keep original size:\n    %s -I io infile.avi\n", gb_argv0);
    av_log(NULL, AV_LOG_INFO, "  to draw shadows of the individual shots, try:\n    %s --shadow=3 -g 7 infile.avi\n", gb_argv0);
    av_log(NULL, AV_LOG_INFO, "  to export thumbnails in WebVTT format every 10 seconds and max size of 1920x1920px:\n    %s -s 10 -w 1920 --vtt=/var/www/html/ -O /mnt/fileshare -Ii -o .jpg infile.avi\n", gb_argv0);
    av_log(NULL, AV_LOG_INFO, "  to skip warning messages to be printed to console (useful for flv files producing lot of warnings), try:\n    %s -q infile.avi\n", gb_argv0);
    av_log(NULL, AV_LOG_INFO, "  to enable additional protocols:\n    %s --options=protocol_whitelist:file,crypto,data,http,https,tcp,tls infile.avi\n", gb_argv0);
    av_log(NULL, AV_LOG_INFO, "\nIn windows, you can run %s from command prompt or drag files/dirs from\n", gb_argv0);
    av_log(NULL, AV_LOG_INFO, "windows explorer and drop them on %s. you can change the default options\n", gb_argv0);
    av_log(NULL, AV_LOG_INFO, "by creating a shortcut to %s and add options there (right click the\n", gb_argv0);
    av_log(NULL, AV_LOG_INFO, "shortcut -> Properties -> Target); then drop files/dirs on the shortcut\n");
    av_log(NULL, AV_LOG_INFO, "instead.\n");
#else
    av_log(NULL, AV_LOG_INFO, "\nYou'll probably need to change the truetype font path (-f fontfile).\n");
    av_log(NULL, AV_LOG_INFO, "the default is set to %s which might not exist in non-windows\n", GB_F_FONTNAME);
    av_log(NULL, AV_LOG_INFO, "systems. if you don't have a truetype font, you can turn the text off by\n");
    av_log(NULL, AV_LOG_INFO, "using -i -t.\n");
#endif
    av_log(NULL, AV_LOG_INFO, "\nMtn comes with ABSOLUTELY NO WARRANTY. this is free software, and you are\n");
    av_log(NULL, AV_LOG_INFO, "welcome to redistribute it under certain conditions; for details see file\n");
    av_log(NULL, AV_LOG_INFO, "gpl-2.0.txt.\n\n");

    av_log(NULL, AV_LOG_INFO, "wahibre@gmx.com\n");
    av_log(NULL, AV_LOG_INFO, "https://gitlab.com/movie_thumbnailer/mtn/wikis\n");
#endif
    free(ident);
}

static int options_to_AVDictionary(struct options *o, const char *input_string)
{
    char pair_sep[2] = { 0, 0 };
    if (strchr(input_string, '|'))
        pair_sep[0] = '|';
    if (o->dict)
        av_dict_free(&o->dict);
    if (av_dict_parse_string(&o->dict, input_string, ":", pair_sep, 0))
    {
        av_log(NULL, AV_LOG_ERROR, "Error parsing input parameter --options=%s!\n", input_string);
        return -1;
    }
    return 0;
}

int parse_options(struct options *o, int argc, char *argv[], int *start_index)
{
    static const struct option long_options[] =
    {
        { "shadow",      optional_argument, 0, 0 },
        { "transparent", no_argument,       0, 0 },
        { "cover",       optional_argument, 0, 0 },
        { "vtt",         optional_argument, 0, 0 },
        { "options",     required_argument, 0, 0 },
        { 0,             0,                 0, 0 }
    };
    int parse_error = 0, option_index = 0;
    int c;
    while (-1 != (c = getopt_long(argc, argv, "a:b:B:c:C:d:D:E:f:F:g:h:HiI:j:k:L:nN:o:O:pPqr:s:S:tT:vVw:WXzZ", long_options, &option_index)))
    {
        switch (c)
        {
        case 0:
            switch (option_index)
            {
                case 0: // shadow
                    if (optarg)
                        parse_error += get_int_opt("-shadow", &o->shadow, optarg, 0);
                    else
                        o->shadow = 0;
                    break;
                case 1: // transparent
                    o->transparent_bg = 1;
                    break;
                case 2: // cover
                    o->cover = 1;
                    if (optarg)
                    {
                        free((char *) o->cover_suffix);
                        o->cover_suffix = strdup(optarg);
                    }
                    break;
                case 3: // vtt
                    o->webvtt = 1;
                    if (optarg)
                    {
                        free((char *) o->webvtt_prefix);
                        o->webvtt_prefix = strdup(optarg);
                    }
                    break;
                case 4: // options
                    if (options_to_AVDictionary(o, optarg) != 0)
                        parse_error++;
                    break;
            }
            break;
        case 'a':
        {
            double val;
            if (!get_double_opt('a', &val, optarg, 1))
            {
                o->a_ratio_num = (int) (val * 10000);
                o->a_ratio_den = 10000;
            }
            else
                parse_error++;
            break;
        }
//		case 'A':
        case 'b':
            parse_error += get_double_opt('b', &o->b_blank, optarg, 0);
            if (o->b_blank < .2)
                av_log(NULL, AV_LOG_INFO, "%s: -b %.2f might be too extreme; try -b .5\n", gb_argv0, o->b_blank);
            if (o->b_blank > 1)
                o->D_edge = 0; // turn edge detection off cuz it requires blank detection
            break;
        case 'B':
            parse_error += get_double_opt('B', &o->B_begin, optarg, 0);
            break;
        case 'c':
            parse_error += get_int_opt("c", &o->c_column, optarg, 1);
            break;
        case 'C':
            parse_error += get_double_opt('C', &o->C_cut, optarg, 1);
            break;
        case 'd':
            parse_error += get_int_opt("d", &o->d_depth, optarg, 0);
            break;
        case 'D':
            parse_error += get_int_opt("D", &o->D_edge, optarg, 0);
            if (o->D_edge > 0 && (o->D_edge < 4 || o->D_edge > 12))
                av_log(NULL, AV_LOG_INFO, "%s: -D%d might be too extreme; try -D4, -D6, or -D8\n", gb_argv0, o->D_edge);
            break;
        case 'E':
            parse_error += get_double_opt('E', &o->E_end, optarg, 0);
            break;
        case 'f':
            free((char *) o->f_fontname);
            o->f_fontname = strdup(optarg);
            if (!strcmp(o->F_ts_fontname, GB_F_FONTNAME)) // ???
            {
                free((char *) o->F_ts_fontname);
                o->F_ts_fontname = strdup(o->f_fontname);
            }
            break;
        case 'F':
            parse_error += get_format_opt(o, 'F', optarg);
            break;
        case 'g':
            parse_error += get_int_opt("g", &o->g_gap, optarg, 0);
            break;
//		case 'G':
        case 'h':
            parse_error += get_int_opt("h", &o->h_height, optarg, 0);
            break;
        case 'H':
            o->H_human_filesize = 1;
            break;
        case 'i':
            o->i_info = 0;
            break;
        case 'I':
            o->I_individual = 1;
            parse_error += get_opt_for_I_arg(o, optarg);
            break;
        case 'j':
            parse_error += get_int_opt("j", &o->j_quality, optarg, 1);
            break;
//		case 'J':
        case 'k': // background color
            parse_error += get_color_opt(&o->k_bcolor, 'k', optarg);
            break;
//		case 'K':
//      case 'l':
        case 'L':
            parse_error += get_location_opt(o, 'L', optarg);
            break;
//		case 'm':
//		case 'M':
        case 'n':
            o->n_normal = 1; // normal priority
            break;
        case 'N':
            free((char *) o->N_suffix);
            o->N_suffix = strdup(optarg);
            break;
        case 'o':
            free((char *) o->o_suffix);
            o->o_suffix = strdup(optarg);
            break;
        case 'O':
            free((char *) o->O_outdir);
            o->O_outdir = strdup(optarg);
            //rem_trailing_slash(gb_O_outdir);
            break;
        case 'p':
            o->p_pause = 1; // pause before exiting
            break;
        case 'P':
            o->P_dontpause = 1; // don't pause
            break;
        case 'q':
            o->q_quiet = 1; //quiet
            break;
//		case 'Q':
        case 'r':
            parse_error += get_int_opt("r", &o->r_row, optarg, 0);
            break;
//		case 'R':
        case 's':
            parse_error += get_int_opt("s", &o->s_step, optarg, 0);
            break;
        case 'S':
            parse_error += get_int_opt("S", &o->S_select_video_stream, optarg, 0);
            break;
        case 't':
            o->t_timestamp = 0; // off
            break;
        case 'T':
            free((char *) o->T_text);
            o->T_text = strdup(optarg);
            break;
//		case 'u':
//		case 'U':
        case 'v':
            o->v_verbose = 1; // verbose
            break;
        case 'V':
            o->V = 1; // DEBUG
            av_log(NULL, AV_LOG_INFO, "%s: -V is only used for debugging\n", gb_argv0);
            break;
        case 'w':
            parse_error += get_int_opt("w", &o->w_width, optarg, 0);
            break;
        case 'W':
            o->W_overwrite = 0;
            break;
//		case 'x':
        case 'X':
            o->X_filename_use_full = 1;
            break;
//		case 'y':
//		case 'Y':
        case 'z':
            o->z_seek = 1; // always seek mode
            break;
        case 'Z':
            o->Z_nonseek = 1; // always non-seek mode
            break;
        default:
            parse_error++;
            break;
        }
    }

    if (optind == argc)
    {
        //av_log(NULL, AV_LOG_ERROR, "%s: no input files or directories specified", gb_argv0);
        parse_error++;
        usage();
    }

    /* check arguments */
    if (!o->r_row == 0 && !o->s_step)
    {
        av_log(NULL, AV_LOG_ERROR, "%s: option -r and -s can't be 0 at the same time", gb_argv0);
        parse_error++;
    }
    if (o->b_blank > 1 && o->D_edge > 0)
    {
        av_log(NULL, AV_LOG_ERROR, "%s: -D requires -b arg to be less than 1", gb_argv0);
        parse_error++;
    }
    if (o->z_seek && o->Z_nonseek)
    {
        av_log(NULL, AV_LOG_ERROR, "%s: option -z and -Z can't be used together", gb_argv0);
        parse_error++;
    }
    if (o->E_end > 0 && o->C_cut > 0)
    {
        av_log(NULL, AV_LOG_ERROR, "%s: option -C and -E can't be used together", gb_argv0);
        parse_error++;
    }
    *start_index = optind;
    return parse_error;
}

void free_options(struct options *o)
{
    free((char *) o->f_fontname);
    free((char *) o->F_ts_fontname);
    free((char *) o->N_suffix);
    free((char *) o->o_suffix);
    free((char *) o->O_outdir);
    free((char *) o->cover_suffix);
    free((char *) o->webvtt_prefix);
    if (o->dict)
        av_dict_free(&o->dict);
}
