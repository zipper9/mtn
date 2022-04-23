/*  mtn - movie thumbnailer

    Copyright (C) 2007-2017 tuit <tuitfun@yahoo.co.th>, et al.	 		http://moviethumbnail.sourceforge.net/
    Copyright (C) 2017-2022 wahibre <wahibre@gmx.com>					https://gitlab.com/movie_thumbnailer/mtn/wikis

    based on "Using libavformat and libavcodec" by Martin Böhme:
        http://www.inb.uni-luebeck.de/~boehme/using_libavcodec.html
        http://www.inb.uni-luebeck.de/~boehme/libavcodec_update.html
    and "An ffmpeg and SDL Tutorial":
        http://www.dranger.com/ffmpeg/
    and "Using GD with FFMPeg":
        http://cvs.php.net/viewvc.cgi/gd/playground/ffmpeg/
    and ffplay.c in ffmpeg
        Copyright (c) 2003 Fabrice Bellard
        http://ffmpeg.mplayerhq.hu/
    and gd.c in libGD
        http://cvs.php.net/viewvc.cgi/php-src/ext/gd/libgd/gd.c?revision=1.111&view=markup

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <gd.h>

#include "options.h"
#include "file_utils.h"
#include "measure_time.h"
#include "scan_dir.h"
#include "string_buffer.h"

#include <libavutil/imgutils.h>
#include <libavutil/avutil.h>
#include <libavutil/display.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#define LINESIZE_ALIGN 1
#define MAX_PACKETS_WITHOUT_PICTURE 1000

#define EXIT_SUCCESS 0
#define EXIT_WARNING 1
#define EXIT_ERROR   2

#ifndef MIN
#define MIN(a,b) ((a)<(b)?(a):(b))
#endif
#ifndef MAX
#define MAX(a,b) ((a)<(b)?(b):(a))
#endif

#ifdef _WIN32
    #define TEXT_WRITE_MODE   _T("wt")
    #define BINARY_WRITE_MODE _T("wb")
#else
    #define TEXT_WRITE_MODE   "w"
    #define BINARY_WRITE_MODE "w"
#endif

#define EDGE_PARTS 6 // # of parts used in edge detection
#define EDGE_FOUND 0.001f // edge is considered found

#define IMAGE_EXTENSION_JPG ".jpg"
#define IMAGE_EXTENSION_PNG ".png"
#define LIBGD_FONT_HEIGHT_CORRECTION 1
#ifdef _WIN32
 #define FOLDER_SEPARATOR "\\"
#else
 #define FOLDER_SEPARATOR "/"
#endif

struct thumbnail
{
    gdImagePtr out_ip;
    struct string_buffer base_filename;
    struct string_buffer out_filename;
    struct string_buffer info_filename;
    struct string_buffer cover_filename;
    int out_saved;                          // 1 = out file is successfully saved
    int img_width, img_height;
    int txt_height;
    int column, row;
    double time_base;
    int64_t step_t;                         // in timebase units
    int shot_width_in,  shot_height_in;     // dimension stored in movie file
    int shot_width_out, shot_height_out;    // dimension  after possible rotation
    int center_gap;                         // horizontal gap to center the shots
    int idx;                                // index of the last shot; -1 = no shot
    int tiles_nr;                           // number of shots in thumbnail
    int rotation;                           // in degrees <-180; 180> stored in movie

    // dynamic
    int64_t *ppts; // array of pts value of each shot
};

struct sprite
{
    gdImagePtr ip;
    const struct thumbnail *parent;
    struct string_buffer vtt_content;
    struct string_buffer curr_filename;
    int w;
    int h;
    int columns;
    int rows;
    int nr_of_shots;
    int curr_file_idx;
    int64_t last_shot_pts;
};

typedef struct KEYS
{
    char *name;
    int count;
} Keys;

typedef struct KEYCOUNTER
{
    Keys *key;
    int count;
} KeyCounter;


/* command line options & default values */
AVRational gb_a_ratio = { 0, 1 };

static int V_DEBUG = 0;

/* more global variables */
const char *gb_argv0 = NULL;
const char *gb_version = "3.4.2";
filetime_t gb_st_start = 0; // start time of program

/* misc functions */

#ifdef _WIN32
static inline ascii_lower(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A' + 'a';
    return c;
}

#undef strcasecmp
int strcasecmp(const char *s1, const char *s2)
{
    while (1)
    {
        int c1 = ascii_lower(*s1++);
        int c2 = ascii_lower(*s2++);
        int diff = c1 - c2;
        if (diff) return diff > 0 ? 1 : -1;
        if (!c1) break;
    }
    return 0;
}
#endif

KeyCounter* kc_new()
{
    KeyCounter *kc = (KeyCounter*)malloc(sizeof(KeyCounter));
    kc->key=NULL;
    kc->count=0;

    return kc;
}

int kc_keyindex(const KeyCounter* const kc, const char* const key)
{
    int i;
    for(i=0; i < kc->count; i++)
    {
        if(strcmp(key, kc->key[i].name) == 0)
            return i;
    }
    return -1;
}

void kc_inc(KeyCounter *kc, const char *key)
{
    int keyindex = kc_keyindex(kc, key);

    if(keyindex >= 0)
    {
        kc->key[keyindex].count++;
        return;
    }
    else
    {
        kc->key = realloc(kc->key, (kc->count+1) * sizeof(Keys));
        kc->key[kc->count].name = malloc((strlen(key)+1) * sizeof(char));
        kc->key[kc->count].count = 1;

        sprintf(kc->key[kc->count].name, key);

        kc->count++;
    }
}

void kc_destroy(KeyCounter **kc)
{
    if(*kc)
    {
        int i;
        for(i=0; i < (*kc)->count; i++)
        {
            free((*kc)->key[i].name);
            free((*kc)->key);
        }
        free(*kc);
        *kc=NULL;
    }
}

/* strrstr not in mingw
*/
char *strlaststr (char *haystack, char *needle)
{
    // If needle is an empty string, the function returns haystack. -- from glibc doc
    if (0 == strlen(needle)) {
        return haystack;
    }

    char *start = haystack;
    char *found = NULL;
    char *prev = NULL;
    while ((found = strstr(start, needle)) != NULL) {
        prev = found;
        start++;
    }
    return prev;
}

char *format_time(double duration, char out[], size_t out_size, char sep)
{
    assert(out_size);
    if (out_size < 4)
    {
        out[0] = 0;
        return out;
    }
    if (duration < 0)
    {
        strcpy(out, "N/A");
        return out;
    }
    int hours, mins, secs;
    secs = (int) duration;
    mins = secs / 60;
    secs %= 60;
    hours = mins / 60;
    mins %= 60;
    snprintf(out, out_size, "%02d%c%02d%c%02d", hours, sep, mins, sep, secs);
    return out;
}

char *format_pts(int64_t pts, double time_base, char out[], size_t out_size)
{
    assert(out_size);
    if (out_size < 4)
    {
        out[0] = 0;
        return out;
    }
    if (pts < 0)
    {
        strcpy(out, "N/A");
        return out;
    }
    int hours, mins, secs, msec;
    double time_stamp = pts * time_base;
    secs = (int) time_stamp;
    msec = (int) ((time_stamp - secs) * 1000);
    mins = secs / 60;
    secs %= 60;
    hours = mins / 60;
    mins %= 60;
    snprintf(out, out_size, "%02d:%02d:%02d.%03d", hours, mins, secs, msec);
    return out;
}

char *format_size(int64_t size, char out[], size_t out_size)
{
    assert(out_size);
    if (size < 1024)
        snprintf(out, out_size, "%d B", (int) size);
    else if (size < 1024*1024)
        snprintf(out, out_size, "%.0f kiB", size/1024.0);
    else if (size < 1024*1024*1024)
        snprintf(out, out_size, "%.0f MiB", size/(1024.0*1024));
    else
        snprintf(out, out_size, "%.1f GiB", size/(1024.0*1024*1024));
    return out;
}

char *format_size_f(int64_t size, char out[], size_t out_size)
{
    assert(out_size);
    if (size < 1200)
        snprintf(out, out_size, "%d b", (int) size);
    else if (size < 1024*1024)
        snprintf(out, out_size, "%.0f kb", size/1000.0);
    else if (size < 1024*1024*1024)
        snprintf(out, out_size, "%.0f Mb", size/(1000.0*1000));
    else
        snprintf(out, out_size, "%.1f Gb", size/(1000.0*1000*1000));
    return out;
}

char *rem_trailing_slash(char *str)
{
#ifdef _WIN32
    // mingw doesn't seem to be happy about trailing '/' or '\\'
    // strip trailing '/' or '\\' that might get added by shell filename completion for directories
    int last_index = strlen(str) - 1;
    // we need last '/' or '\\' for root drive "c:\"
    while (last_index > 2 && 
        ('/' == str[last_index] || '\\' == str[last_index])) {
        str[last_index--] = '\0';
    }
#endif
    return str;
}


/* mtn */

/* 
return pointer to a new cropped image. the original one is freed.
if error, return original and the original stays intact
*/
gdImagePtr crop_image(gdImagePtr ip, int new_width, int new_height)
{
    // cant find GD's crop, so we'll need to create a smaller image
    gdImagePtr new_ip = gdImageCreateTrueColor(new_width, new_height);
    if (!new_ip)
        return ip; // return the original
    gdImageCopy(new_ip, ip, 0, 0, 0, 0, new_width, new_height);
    gdImageDestroy(ip);
    return new_ip;
}

/*
returns height, or 0 if error
*/
int image_string_height(const char *text, const char *font, double size)
{
    int brect[8];

    if (!(text && *text))
        return 0;

    char *err = gdImageStringFT(NULL, &brect[0], 0, (char *) font, size, 0, 0, 0, (char *) text);
    if (err)
    {
        av_log(NULL, AV_LOG_WARNING, "gdImageStringFT error: %s\n", err);
        return 0;
    }
    return brect[3] - brect[7] + LIBGD_FONT_HEIGHT_CORRECTION;
}

/*
position can be:
    1: lower left
    2: lower right
    3: upper right
    4: upper left
returns NULL if success, otherwise returns error message
*/
char *image_string(gdImagePtr ip, const char *font, uint32_t color, double size, int position, int gap, char *text, int shadow, uint32_t shadow_color, int padding)
{
    int brect[8];

    int gd_color = gdImageColorResolve(ip, RGB_R(color), RGB_G(color), RGB_B(color));
    char *err = gdImageStringFT(NULL, brect, gd_color, (char *) font, size, 0, 0, 0, (char *) text);

    if (err)
        return err;

    int x, y;
    switch (position)
    {
    case 1: // lower left
        x = -brect[0] + gap + padding;
        y = gdImageSY(ip) - brect[1] - gap - padding;
        break;
    case 2: // lower right
        x = gdImageSX(ip) - brect[2] - gap - padding;
        y = gdImageSY(ip) - brect[3] - gap - padding;
        break;
    case 3: // upper right
        x = gdImageSX(ip) - brect[4] - gap - padding;
        y = -brect[5] + gap + padding + LIBGD_FONT_HEIGHT_CORRECTION;
        break;
    case 4: // upper left
        x = -brect[6] + gap + padding;
        y = -brect[7] + gap + padding + LIBGD_FONT_HEIGHT_CORRECTION;
        break;
    default:
        return "image_string's position can only be 1, 2, 3, or 4";
    }

    if (shadow) {
        int shadowx, shadowy;
        switch (position)
        {
        case 1: // lower left
            shadowx = x+1;
            shadowy = y;
            y = y-1;
            break;
        case 2: // lower right
            shadowx = x;
            shadowy = y;
            x = x-1;
            y = y-1;
            break;
        case 3: // upper right
            shadowx = x;
            shadowy = y+1;
            x = x-1;
            break;
        case 4: // upper left
            shadowx = x+1;
            shadowy = y+1;
            break;
        default:
            return "image_string's position can only be 1, 2, 3, or 4";
        }
        int gd_shadow = gdImageColorResolve(ip, RGB_R(shadow_color), RGB_G(shadow_color), RGB_B(shadow_color));
        err = gdImageStringFT(ip, brect, gd_shadow, (char *) font, size, 0, shadowx, shadowy, (char *) text);
        if (err)
            return err;
    }

    return gdImageStringFT(ip, brect, gd_color, (char *) font, size, 0, x, y, (char *) text);
}

/*
 * return 30% of character height as a padding
 */
int image_string_padding(const char *font, double size)
{
    int padding = (int) (image_string_height("SAMPLE", font, size) * 0.3 + 0.5);
    if (padding > 1)
        return padding;
    return 1;
}

/*
return 0 if image is saved
*/
int save_image(gdImagePtr ip, const char *outname, const struct options *o)
{
    int result = -1;
    const tchar_t *tname = utf8_to_tchar(outname);
    FILE *fp = _tfopen(tname, BINARY_WRITE_MODE);
    if (fp)
    {
        const char *image_extension = strrchr(outname, '.');
        if (image_extension && strcasecmp(image_extension, ".png") == 0)
            gdImagePngEx(ip, fp, 9); // 9-best png compression
        else
            gdImageJpeg(ip, fp, o->j_quality);

        if (!fclose(fp))
            result = 0;
        else
            av_log(NULL, AV_LOG_ERROR, "\n%s: closing output image '%s' failed: %s\n", gb_argv0, outname, strerror(errno));
    }
    else
        av_log(NULL, AV_LOG_ERROR, "\n%s: creating output image '%s' failed: %s\n", gb_argv0, outname, strerror(errno));

    free_conv_result(tname);
    return result;
}

/*
pFrame must be a AV_PIX_FMT_RGB24 frame
*/
void FrameRGB_2_gdImage(AVFrame *pFrame, gdImagePtr ip, int width, int height)
{
    uint8_t *src = pFrame->data[0];
    int x, y;
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++)
        {
            gdImageSetPixel(ip, x, y, gdImageColorResolve(ip, src[0], src[1], src[2]));
            src += 3;
        }
}

/* initialize 
*/
void thumb_new(struct thumbnail *ptn)
{
    ptn->out_ip = NULL;
    ptn->out_saved = 0;
    ptn->img_width = ptn->img_height = 0;
    ptn->txt_height = 0;
    ptn->column = ptn->row = 0;
    ptn->step_t = 0;
    ptn->shot_width_in  = ptn->shot_height_in = 0;
    ptn->shot_width_out = ptn->shot_height_out = 0;
    ptn->center_gap = 0;
    ptn->idx = -1;
    ptn->tiles_nr = 0;    
    ptn->rotation = 0;

    // dynamic
    ptn->ppts = NULL;
    sb_init(&ptn->base_filename);
    sb_init(&ptn->out_filename);
    sb_init(&ptn->info_filename);
    sb_init(&ptn->cover_filename);
}

struct sprite *sprite_create(int max_size, int shot_width, int shot_height, const struct thumbnail *parent)
{
    struct sprite *s = malloc(sizeof(*s));
    s->nr_of_shots   = 0;
    s->curr_file_idx = 0;
    s->last_shot_pts = 0;
    s->w        = shot_width;
    s->h        = shot_height;
    s->columns  = max_size/s->w;
    s->rows     = max_size/s->h;
    s->parent = parent;
    sb_init(&s->vtt_content);
    sb_init(&s->curr_filename);

    sb_add_string(&s->vtt_content, "WEBVTT\n\nNOTE This file has been generated by Movie Thumbnailer\nhttps://gitlab.com/movie_thumbnailer/mtn/-/wikis");

    s->ip = gdImageCreateTrueColor(s->columns*shot_width, s->rows*shot_height);

    return s;
}

void sprite_fit(struct sprite *s)
{
    if (s->nr_of_shots > 0 && s->nr_of_shots < s->columns*s->rows)
    {
        int rows = s->nr_of_shots/s->columns;
        int cols = s->nr_of_shots % s->columns;

        if (cols > 0)
            rows++;

        int width =  (rows>0? s->columns : cols) * s->w;
        int height = rows * s->h;

        gdImagePtr reduced_ip = gdImageCreateTrueColor(width, height);
        gdImageCopy(reduced_ip, s->ip,
            0, 0,
            0, 0, width, height);
        gdImageDestroy(s->ip);
        s->ip = reduced_ip;
    }
}

void sprite_flush(struct sprite *s, const struct options *o)
{
    if (!s)
        return;

    if (s->nr_of_shots > 0)
    {
        sprite_fit(s);
        char num_buf[64];
        int num_len = sprintf(num_buf, "_vtt_%d", s->curr_file_idx);
        int filename_len = s->parent->base_filename.len;
        int suffix_len = strlen(o->o_suffix);
        char *outname = (char *) malloc(filename_len + num_len + suffix_len + 1);
        char *p = outname;
        memcpy(p, s->parent->base_filename.s, filename_len);
        p += filename_len;
        memcpy(p, num_buf, num_len);
        p += num_len;
        strcpy(p, o->o_suffix);
        save_image(s->ip, outname, o);
        free(outname);

        gdImageDestroy(s->ip);
        s->ip = gdImageCreateTrueColor(s->columns*s->w, s->rows*s->h);

        sb_clear(&s->curr_filename);
        s->nr_of_shots = 0;
        s->curr_file_idx++;
    }
}

void sprite_add_shot(struct sprite *s, gdImagePtr ip, int64_t pts, const struct options *o)
{
    int very_first_shot = (s->nr_of_shots==0 && s->curr_file_idx==0)? 1 : 0;

    int rows_used = s->nr_of_shots/s->columns;
    int cols_used = s->nr_of_shots - (rows_used*s->columns);

    int posY = rows_used * s->h;
    int posX = cols_used * s->w;

    char time_from[64], time_to[64];
    int64_t pts_from = s->last_shot_pts;
    int64_t pts_to = pts + s->parent->step_t / 2;

    if (!s->curr_filename.len)
    {
        sb_add_string(&s->curr_filename, o->webvtt_prefix);
        sb_add_buffer(&s->curr_filename, &s->parent->base_filename);
        char tmp_buf[64];
        sb_add_string_len(&s->curr_filename, tmp_buf, sprintf(tmp_buf, "_vtt_%d", s->curr_file_idx));
        sb_add_string(&s->curr_filename, o->o_suffix);
    }

    if (very_first_shot)
        format_pts(0, s->parent->time_base, time_from, sizeof(time_from));
    else
        format_pts(pts_from, s->parent->time_base, time_from, sizeof(time_from));

    format_pts(pts_to, s->parent->time_base, time_to, sizeof(time_to));

    sb_add_string(&s->vtt_content, "\n\n");
    sb_add_string(&s->vtt_content, time_from);
    sb_add_string(&s->vtt_content, " --> ");
    sb_add_string(&s->vtt_content, time_to);
    sb_add_char(&s->vtt_content, '\n');
    sb_add_buffer(&s->vtt_content, &s->curr_filename);
    char tmp_buf[256];
    sb_add_string_len(&s->vtt_content, tmp_buf,
        sprintf(tmp_buf, "#xywh=%d,%d,%d,%d", posX, posY, s->w, s->h));

    gdImageCopy(s->ip, ip,
        posX, posY,
        0, 0, s->w, s->h);

    s->last_shot_pts = pts_to;
    s->nr_of_shots++;

    if (s->nr_of_shots >= s->columns * s->rows)
        sprite_flush(s, o);
}

int sprite_export_vtt(struct sprite *s)
{
    if (s == NULL)
        return 0;

    int filename_len = s->parent->base_filename.len;
    char *outname = (char *) malloc(5 + filename_len);
    memcpy(outname, s->parent->base_filename.s, filename_len);
    strcpy(outname + filename_len, ".vtt");

    int result = -1;
    tchar_t *outname_w = utf8_to_tchar(outname);
    FILE *fp = _tfopen(outname_w, TEXT_WRITE_MODE);
    if (fp)
    {
        if (fwrite(s->vtt_content.s, 1, s->vtt_content.len, fp) <= 0)
            av_log(NULL, AV_LOG_ERROR, "\n%s: error writting to file '%s': %s\n", gb_argv0, outname, strerror(errno));
        if (!fclose(fp))
            result = 0;
        else
            av_log(NULL, AV_LOG_ERROR, "\n%s: closing output file '%s' failed: %s\n", gb_argv0, outname, strerror(errno));
    }
    else
        av_log(NULL, AV_LOG_ERROR, "\n%s: creating output file '%s' failed: %s\n", gb_argv0, outname, strerror(errno));

    free_conv_result(outname_w);
    free(outname);
    return result;
}

void sprite_destroy(struct sprite *s)
{
    if (!s) return;
    sb_destroy(&s->vtt_content);
    sb_destroy(&s->curr_filename);
    gdImageDestroy(s->ip);
    free(s);
}

/* 
alloc dynamic data; must be called after all required static data is filled in
return -1 if failed
*/
int thumb_alloc_dynamic(struct thumbnail *ptn)
{
    ptn->ppts = malloc(ptn->column * ptn->row * sizeof(*(ptn->ppts)));
    if (!ptn->ppts) return -1;
    return 0;
}

void thumb_cleanup_dynamic(struct thumbnail *ptn)
{
    free(ptn->ppts);
    ptn->ppts = NULL;
    sb_destroy(&ptn->base_filename);
    sb_destroy(&ptn->out_filename);
    sb_destroy(&ptn->info_filename);
    sb_destroy(&ptn->cover_filename);
}

/* returns blured shadow on success, NULL otherwise	*/
gdImagePtr create_shadow_image(int background, int width, int height, int *radius_inout, const struct options *o)
{
    gdImagePtr shadow;
    int shW, shH;

    int radius = *radius_inout;
    if (radius >= 0)
    {
        if (radius == 0)
        {
            radius = (int) (MIN(width, height) * 0.03);
            radius = MAX(radius, 3);
            *radius_inout = radius;
        }
        
        int shadowOffset = 2*radius+1;
        shW = width + shadowOffset;
        shH = height + shadowOffset;

        shadow = gdImageCreateTrueColor(shW, shH);
        if (shadow)
        {
            gdImageFilledRectangle(shadow, 0, 0, shW, shH, background);				//fill with background colour
            gdImageFilledRectangle(shadow, radius+1, radius+1, width, height, 0);	//fill black rectangle as a shadow
            //GaussianBlurred since libgd-2.1.1
            #if((GD_MAJOR_VERSION*1000000 + GD_MINOR_VERSION*1000 + GD_RELEASE_VERSION) >= 2001001)
            {
                gdImagePtr blurredShadow = gdImageCopyGaussianBlurred(shadow, radius, 0);			//blur shadow

                if (blurredShadow)
                {
                    gdImageDestroy(shadow);
                    av_log(NULL, AV_LOG_INFO, "  thumbnail shadow radius: %dpx\n", radius);
                    if (o->g_gap < shadowOffset)
                        av_log(NULL, AV_LOG_INFO, "  thumbnail shadow might be invisible. Consider increase gap between individual shots (-g %d).\n", shadowOffset);
                    return blurredShadow;
                }
                else
                    av_log(NULL, AV_LOG_ERROR, "Can't blur Shadow Image!\n");
            }
            #else
            {
                av_log(NULL, AV_LOG_INFO, "Can't blur Shadow Image. Libgd does not support blurring. Use version libgd-2.1.1 or newer.\n");
                return shadow;
            }
            #endif
        }
        else
            av_log(NULL, AV_LOG_ERROR, "Couldn't create Image in Size %dx%d!\n", shW, shH);
    }
    else
        av_log(NULL, AV_LOG_ERROR, "Shadow can't have negative value! (see option --shadow)\n");

    return NULL;
}

/* 
add shot
because ptn->idx is the last index, this function assumes that shots will be added 
in increasing order.
*/
void thumb_add_shot(struct thumbnail *ptn, gdImagePtr ip, gdImagePtr thumbShadowIm, int shadow_pos, int idx, int64_t pts, const struct options *o)
{
    int dstX = idx%ptn->column * (ptn->shot_width_out+o->g_gap) + o->g_gap + ptn->center_gap;
    int dstY = idx/ptn->column * (ptn->shot_height_out+o->g_gap) + o->g_gap
        + ((o->L_info_location == 3 || o->L_info_location == 4) ? ptn->txt_height : 0);

    if (thumbShadowIm)
        gdImageCopy(ptn->out_ip, thumbShadowIm, dstX+shadow_pos+1, dstY+shadow_pos+1, 0, 0, gdImageSX(thumbShadowIm), gdImageSY(thumbShadowIm));

    gdImageCopy(ptn->out_ip, ip, dstX, dstY, 0, 0, ptn->shot_width_out, ptn->shot_height_out);
    ptn->idx = idx;
    ptn->ppts[idx] = pts;
    ptn->tiles_nr++;
}

/*
perform convolution on pFrame and store result in ip
pFrame must be a AV_PIX_FMT_RGB24 frame
ip must be of the same size as pFrame
begin = upper left, end = lower right
filter should be a 2-dimensional but since we cant pass it without knowning the size, we'll use 1 dimension
modified from:
http://cvs.php.net/viewvc.cgi/php-src/ext/gd/libgd/gd.c?revision=1.111&view=markup
*/
void FrameRGB_convolution(AVFrame *pFrame, int width, int height, 
    float *filter, int filter_size, float filter_div, float offset,
    gdImagePtr ip, int xbegin, int ybegin, int xend, int yend)
{

    int x, y, i, j;
    float new_r, new_g, new_b;
    uint8_t *src = pFrame->data[0];

    for (y=ybegin; y<=yend; y++) {
        for(x=xbegin; x<=xend; x++) {
            new_r = new_g = new_b = 0;
            //float grey = 0;

            for (j=0; j<filter_size; j++) {
                int yv = MIN(MAX(y - filter_size/2 + j, 0), height - 1);
                for (i=0; i<filter_size; i++) {
                    int xv = MIN(MAX(x - filter_size/2 + i, 0), width - 1);
                    int pos = yv*width*3 + xv*3;
                    new_r += src[pos]   * filter[j * filter_size + i];
                    new_g += src[pos+1] * filter[j * filter_size + i];
                    new_b += src[pos+2] * filter[j * filter_size + i];
                    //grey += (src[pos] + src[pos+1] + src[pos+2])/3 * filter[j * filter_size + i];
                }
            }

            new_r = (new_r/filter_div)+offset;
            new_g = (new_g/filter_div)+offset;
            new_b = (new_b/filter_div)+offset;
            //grey = (grey/filter_div)+offset;

            new_r = (new_r > 255.0f)? 255.0f : ((new_r < 0.0f)? 0.0f:new_r);
            new_g = (new_g > 255.0f)? 255.0f : ((new_g < 0.0f)? 0.0f:new_g);
            new_b = (new_b > 255.0f)? 255.0f : ((new_b < 0.0f)? 0.0f:new_b);
            //grey = (grey > 255.0f)? 255.0f : ((grey < 0.0f)? 0.0f:grey);

            gdImageSetPixel(ip, x, y, gdImageColorResolve(ip, (int)new_r, (int)new_g, (int)new_b));
            //gdImageSetPixel(ip, x, y, gdTrueColor((int)new_r, (int)new_g, (int)new_b));
            //gdImageSetPixel(ip, x, y, gdTrueColor((int)grey, (int)grey, (int)grey));
        }
    }
}

/* begin = upper left, end = lower right
*/
float cmp_edge(gdImagePtr ip, int xbegin, int ybegin, int xend, int yend)
{
#define CMP_EDGE 208
    int count = 0;
    int i, j;
    for (j = ybegin; j <= yend; j++) {
        for (i = xbegin; i <= xend; i++) {
            int pixel = gdImageGetPixel(ip, i, j);
            if (gdImageRed(ip, pixel) >= CMP_EDGE 
                && gdImageGreen(ip, pixel) >= CMP_EDGE
                && gdImageBlue(ip, pixel) >= CMP_EDGE) {
                count++;
            }
        }
    }
    return (float)count / (yend - ybegin + 1) / (xend - xbegin + 1);
}

int is_edge(float *edge, float edge_found)
{
    if (V_DEBUG)
        return 1; // DEBUG

    int count = 0;
    int i;
    for (i = 0; i < EDGE_PARTS; i++)
        if (edge[i] >= edge_found)
            count++;
    if (count >= 2)
        return count;
    return 0;
}

/*
pFrame must be an AV_PIX_FMT_RGB24 frame
http://student.kuleuven.be/~m0216922/CG/
http://www.pages.drexel.edu/~weg22/edge.html
http://student.kuleuven.be/~m0216922/CG/filtering.html
http://cvs.php.net/viewvc.cgi/php-src/ext/gd/libgd/gd.c?revision=1.111&view=markup
*/
gdImagePtr detect_edge(AVFrame *pFrame, const struct thumbnail* const tn, float *edge, float edge_found, const struct options *o)
{
    int width =  tn->shot_width_in;
    int height = tn->shot_height_in;
    static float filter[] =
    {
         0,-1, 0,
        -1, 4,-1,
         0,-1, 0
    };
#define FILTER_SIZE 3 // 3x3
#define FILTER_DIV 1
#define OFFSET 128
    static int init_filter = 0; // FIXME
    if (!init_filter)
    {
        init_filter = 1;
        filter[1] = -o->D_edge/4.0f;
        filter[3] = -o->D_edge/4.0f;
        filter[4] =  o->D_edge;
        filter[5] = -o->D_edge/4.0f;
        filter[7] = -o->D_edge/4.0f;
    }

    gdImagePtr ip = gdImageCreateTrueColor(width, height);
    if (!ip)
    {
        av_log(NULL, AV_LOG_ERROR, "  gdImageCreateTrueColor failed\n");
        return NULL;
    }
    if (o->v_verbose)
        FrameRGB_2_gdImage(pFrame, ip, width, height);

    int i;
    for (i = 0; i < EDGE_PARTS; i++)
        edge[i] = 1;

    // check 6 parts to speed this up & to improve correctness
    int y_size = height/10;
    int ya = y_size*2;
    int yb = y_size*4;
    int yc = y_size*6;
    int x_crop = width/8;

    // only find edge if neccessary
    int parts[EDGE_PARTS][4] =
    {
        //xbegin, ybegin, xend, yend
        {x_crop, ya, width/2, ya+y_size},
        {width/2+1, ya+y_size, width-x_crop, ya+2*y_size},
        {x_crop, yb, width/2, yb+y_size},
        {width/2+1, yb+y_size, width-x_crop, yb+2*y_size},
        {x_crop, yc, width/2, yc+y_size},
        {width/2+1, yc+y_size, width-x_crop, yc+2*y_size},
    };
    int count = 0;
    for (i = 0; i < EDGE_PARTS && count < 2; i++)
    {
        FrameRGB_convolution(pFrame, width, height, filter, FILTER_SIZE, FILTER_DIV, OFFSET, 
            ip, parts[i][0], parts[i][1], parts[i][2], parts[i][3]);
        edge[i] = cmp_edge(ip, parts[i][0], parts[i][1], parts[i][2], parts[i][3]);
        if (edge[i] >= edge_found)
            count++;
    }
    return ip;
}

int
save_AVFrame(
    const AVFrame* const pFrame,
    int src_width,
    int src_height,
    enum AVPixelFormat pix_fmt,
    char *filename,
    int dst_width,
    int dst_height,
    const struct options *o
)
{
    AVFrame *pFrameRGB = NULL;
    uint8_t *rgb_buffer = NULL;
    struct SwsContext *pSwsCtx = NULL;
    gdImagePtr ip = NULL;
    const enum AVPixelFormat rgb_pix_fmt = AV_PIX_FMT_RGB24;
    int result = -1;

    pFrameRGB = av_frame_alloc();
    if (!pFrameRGB)
    {
        av_log(NULL, AV_LOG_ERROR, "  couldn't allocate a video frame\n");
        goto cleanup;
    }
    int rgb_bufsize = av_image_get_buffer_size(rgb_pix_fmt, dst_width, dst_height, LINESIZE_ALIGN);
    rgb_buffer = av_malloc(rgb_bufsize);
    if (!rgb_buffer)
    {
        av_log(NULL, AV_LOG_ERROR, "  av_malloc %d bytes failed\n", rgb_bufsize);
        goto cleanup;
    }

    av_image_fill_arrays(pFrameRGB->data, pFrameRGB->linesize, rgb_buffer, rgb_pix_fmt, dst_width, dst_height, LINESIZE_ALIGN);

    pSwsCtx = sws_getContext(src_width, src_height, pix_fmt,
        dst_width, dst_height, rgb_pix_fmt, SWS_BILINEAR, NULL, NULL, NULL);
    if (!pSwsCtx)
    {
        av_log(NULL, AV_LOG_ERROR, "  sws_getContext failed\n");
        goto cleanup;
    }

    sws_scale(pSwsCtx, (const uint8_t* const*)pFrame->data, pFrame->linesize, 0, src_height, 
        pFrameRGB->data, pFrameRGB->linesize);
    ip = gdImageCreateTrueColor(dst_width, dst_height);
    if (!ip)
    {
        av_log(NULL, AV_LOG_ERROR, "  gdImageCreateTrueColor failed: width %d, height %d\n", dst_width, dst_height);
        goto cleanup;
    }
    FrameRGB_2_gdImage(pFrameRGB, ip, dst_width, dst_height);
    
    int ret = save_image(ip, filename, o);
    if (ret)
    {
        av_log(NULL, AV_LOG_ERROR, "  save_image failed: %s\n", filename);
        goto cleanup;
    }

    result = 0;
    
cleanup:
    if (ip)
        gdImageDestroy(ip);
    if (pSwsCtx)
        sws_freeContext(pSwsCtx);
    if (rgb_buffer)
        av_free(rgb_buffer);
    if (pFrameRGB)
        av_free(pFrameRGB);

    return result;
}



/* av_pkt_dump_log()?? */
void dump_packet(AVPacket *p, AVStream * ps)
{
    /* from av_read_frame()
    pkt->pts, pkt->dts and pkt->duration are always set to correct values in 
    AVStream.timebase units (and guessed if the format cannot provided them). 
    pkt->pts can be AV_NOPTS_VALUE if the video format has B frames, so it is 
    better to rely on pkt->dts if you do not decompress the payload.
    */
    av_log(NULL, AV_LOG_VERBOSE, "***dump_packet: pos:%"PRId64"\n", p->pos);
    av_log(NULL, AV_LOG_VERBOSE, "pts tb: %"PRId64", dts tb: %"PRId64", duration tb: %"PRId64"\n",
        p->pts, p->dts, p->duration);
    av_log(NULL, AV_LOG_VERBOSE, "pts s: %.2f, dts s: %.2f, duration s: %.2f\n",
        p->pts * av_q2d(ps->time_base), p->dts * av_q2d(ps->time_base), 
        p->duration * av_q2d(ps->time_base)); // pts can be AV_NOPTS_VALUE
}

void dump_codec_context(AVCodecContext * p)
{
    if(p->codec == 0)
        av_log(NULL, AV_LOG_VERBOSE, "***dump_codec_context: codec = ?0?\n");
    else
        av_log(NULL, AV_LOG_VERBOSE, "***dump_codec_context %s, time_base: %d / %d\n", p->codec->name,
            p->time_base.num, p->time_base.den);

    av_log(NULL, AV_LOG_VERBOSE, "frame_number: %d, width: %d, height: %d, sample_aspect_ratio %d/%d%s\n",
        p->frame_number, p->width, p->height, p->sample_aspect_ratio.num, p->sample_aspect_ratio.den,
        (0 == p->sample_aspect_ratio.num) ? "" : "**a**");
}

/*
void dump_index_entries(AVStream * p)
{
    int i;
    double diff = 0;
    for (i=0; i < p->nb_index_entries; i++) { 
        AVIndexEntry *e = p->index_entries + i;
        double prev_ts = 0, cur_ts = 0;
        cur_ts = e->timestamp * av_q2d(p->time_base);
        //assert(cur_ts > 0);
        diff += cur_ts - prev_ts;
        if (i < 20) { // show only first 20
            av_log(NULL, AV_LOG_VERBOSE, "    i: %2d, pos: %8"PRId64", timestamp tb: %6"PRId64", timestamp s: %6.2f, flags: %d, size: %6d, min_distance: %3d\n",
                i, e->pos, e->timestamp, e->timestamp * av_q2d(p->time_base), e->flags, e->size, e->min_distance);
        }
        prev_ts = cur_ts;
    }
    av_log(NULL, AV_LOG_VERBOSE, "  *** nb_index_entries: %d, avg. timestamp s diff: %.2f\n", p->nb_index_entries, diff / p->nb_index_entries);
}
*/

//based on dump.c: static void dump_sidedata(void *ctx, AVStream *st, const char *indent)
double get_stream_rotation(const AVStream *st)
{
    if (st->nb_side_data)
    {
        int i;
        for (i=0; i < st->nb_side_data; i++)
        {
            const AVPacketSideData *sd = st->side_data + i;
            if (sd->type == AV_PKT_DATA_DISPLAYMATRIX)
                return av_display_rotation_get((const int32_t *) sd->data);
        }
    }
    return 0;
}

void dump_stream(AVStream * p)
{
    av_log(NULL, AV_LOG_VERBOSE, "***dump_stream, time_base: %d / %d\n", 
        p->time_base.num, p->time_base.den);
    av_log(NULL, AV_LOG_VERBOSE, "start_time tb: %"PRId64", duration tb: %"PRId64", nb_frames: %"PRId64"\n",
        p->start_time, p->duration, p->nb_frames);
    // get funny results here. use format_context's.
    av_log(NULL, AV_LOG_VERBOSE, "start_time s: %.2f, duration s: %.2f\n",
        p->start_time * av_q2d(p->time_base), 
        p->duration * av_q2d(p->time_base)); // duration can be AV_NOPTS_VALUE 
    // field pts in AVStream is for encoding
}

/*
set scale source width & height (scaled_w and scaled_h)
*/
void calc_scale_src(int width, int height, AVRational ratio, int *scaled_w, int *scaled_h)
{
    // mplayer dont downscale horizontally. however, we'll always scale
    // horizontally, up or down, which is the same as mpc's display and 
    // vlc's snapshot. this should make square pixel for both pal & ntsc.
    *scaled_w = width;
    *scaled_h = height;
    if (0 != ratio.num) { // ratio is defined
        assert(ratio.den != 0);
        *scaled_w = (int) (av_q2d(ratio) * width + 0.5); // round nearest
    }
}

long
get_bitrate_from_metadata(const AVDictionary *dict)
{
    if(av_dict_count(dict) > 0)
    {
        char *bps_value = NULL;
        AVDictionaryEntry* e = NULL;

        e = av_dict_get(dict, "BPS-eng", NULL, AV_DICT_IGNORE_SUFFIX);

        if(e)
            bps_value = e->value;
        else
        {
            e = av_dict_get(dict, "BPS", NULL, AV_DICT_IGNORE_SUFFIX);

            if(e)
                bps_value = e->value;
        }

        if(bps_value)
            return atol(bps_value);
    }

    return 0;
}

AVCodecContext* get_codecContext_from_codecParams(AVCodecParameters* pCodecPar)
{
    AVCodecContext *pCodecContext;

    pCodecContext = avcodec_alloc_context3(NULL);
    if (!pCodecContext)
    {
        av_log(NULL, AV_LOG_ERROR, "Couldn't alocate codec context\n");
        return NULL;
    }

    if (avcodec_parameters_to_context(pCodecContext, pCodecPar) < 0)
    {
        avcodec_free_context(&pCodecContext);
        return NULL;
    }

    return pCodecContext;
}

/*
modified from libavformat's dump_format
*/
void get_stream_info_type(struct string_buffer *sb, const AVFormatContext *ic, enum AVMediaType type, AVRational sample_aspect_ratio, int verbose)
{
    unsigned int i;
    struct string_buffer sb_sub;
    AVCodecContext *pCodexCtx = NULL;
    char subtitles_separator[3] = { 0, };
    KeyCounter *kc = kc_new();
    char tmp_buf[256];

    sb_init(&sb_sub);

    for (i = 0; i < ic->nb_streams; i++)
    {
        int flags = ic->iformat->flags;
        const AVStream *st = ic->streams[i];
        const AVDictionaryEntry *language = av_dict_get(st->metadata, "language", NULL, 0);

        if (type != st->codecpar->codec_type)
            continue;

        pCodexCtx = get_codecContext_from_codecParams(st->codecpar);

        if (st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE)
        {
            if (language)
            {
                const AVDictionaryEntry *subentry_title = av_dict_get(st->metadata, "title", NULL, 0);
                sb_add_string(&sb_sub, subtitles_separator);
                sb_add_string(&sb_sub, language->value);
                if (subentry_title && strcasecmp(subentry_title->value, "sub"))
                {
                    sb_add_string(&sb_sub, " (");
                    sb_add_string(&sb_sub, subentry_title->value);
                    sb_add_char(&sb_sub, ')');
                }
                strcpy(subtitles_separator, ", ");
            }
            else
            {
                const char *codec_name = avcodec_get_name(pCodexCtx->codec_id);
                kc_inc(kc, codec_name);
            }
            continue;
        }

        sb_add_char(sb, '\n');

        if (verbose)
        {
            int len = sprintf(tmp_buf, "Stream %d", i);
            if (flags & AVFMT_SHOW_IDS)
                len += sprintf(tmp_buf + len, "[0x%x]", st->id);

            /*
            int g = ff_gcd(st->time_base.num, st->time_base.den);
            len += sprintf(tmp_buf + len, ", %d/%d", st->time_base.num/g, st->time_base.den/g);
            */
            sb_add_string_len(sb, tmp_buf, len);
            sb_add_string(sb, ": ");
        }

        avcodec_string(tmp_buf, sizeof(tmp_buf), pCodexCtx, 0);

/* re-enable SAR & DAR
        // remove [SAR DAR] from string, it's not very useful.
        char *begin = NULL, *end = NULL;
        if ((begin=strstr(tmp_buf, " [SAR")) != NULL
            && (end=strchr(begin, ']')) != NULL) {
            while (*++end != '\0') {
                *begin++ = *end;
            }
            *begin = '\0';
        }
*/
        sb_add_string(sb, tmp_buf);

        /* if bitrate is missing, try to search elsewhere */
        if ((st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO ||
             st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) &&
             st->codecpar->bit_rate <= 0)
        {
            long metadata_bitrate = get_bitrate_from_metadata(st->metadata);
            if (metadata_bitrate > 0)
            {
                char formated_bitrate[64];
                sb_add_string(sb, ", ");
                sb_add_string(sb, format_size_f(metadata_bitrate, formated_bitrate, sizeof(formated_bitrate)));
                sb_add_string(sb, "/s");
            }
        }

        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            int len;
            if (st->r_frame_rate.den && st->r_frame_rate.num)
                len = sprintf(tmp_buf, ", %5.2f fps(r)", av_q2d(st->r_frame_rate));
            else
                len = sprintf(tmp_buf, ", %5.2f fps(c)", 1/av_q2d(st->time_base));
            sb_add_string_len(sb, tmp_buf, len);

            // show aspect ratio
            int scaled_src_width, scaled_src_height;

            calc_scale_src(st->codecpar->width, st->codecpar->height, sample_aspect_ratio,
                &scaled_src_width, &scaled_src_height);

            if (scaled_src_width != st->codecpar->width || scaled_src_height != st->codecpar->height)
            {
                len = sprintf(tmp_buf, " => %dx%d", scaled_src_width, scaled_src_height);
                sb_add_string_len(sb, tmp_buf, len);
            }
        }
        if (language)
        {
            sb_add_string(sb, " (");
            sb_add_string(sb, language->value);
            sb_add_char(sb, ')');
        }
    } //for

    if (sb_sub.len || kc->count > 0)
        sb_add_string(sb, "\nSubtitles: ");

    if (sb_sub.len)
    {
        sb_add_buffer(sb, &sb_sub);
        if (kc->count > 0)
            sb_add_string(sb, ", ");
    }

    int j;
    for (j = 0; j < kc->count; j++)
    {
        sb_add_string(sb, kc->key[j].name);
        if (kc->count > 1)
            sb_add_string_len(sb, tmp_buf, sprintf(tmp_buf, " (%dx)", kc->key[j].count));
    }

    if (pCodexCtx)
        avcodec_free_context(&pCodexCtx);

    kc_destroy(&kc);
    sb_destroy(&sb_sub);
}

/*
modified from libavformat's dump_format
*/
void get_stream_info(struct string_buffer *sb, const AVFormatContext *ic, const char *url, int strip_path, AVRational sample_aspect_ratio, const struct options *o)
{
    char tmp_buf[256];
    char size_buf[64];
    int duration = -1;

    const char *file_name = url;
    if (strip_path)
        file_name = basename(url);

    int64_t file_size = avio_size(ic->pb);

    sb_add_string(sb, "File: ");
    sb_add_string(sb, file_name);

    /* file format
    sprintf(buf + strlen(buf), " (%s)", ic->iformat->name);
    */

    format_size(file_size, size_buf, sizeof(size_buf));
    int len;
    if (o->H_human_filesize)
        /* File size only in MiB, GiB, ... */
        len = sprintf(tmp_buf, "\nSize: %s", size_buf);
    else
        /* File size i bytes and MiB */
        len = sprintf(tmp_buf, "\nSize: %"PRId64" bytes (%s)", file_size, size_buf);
    sb_add_string_len(sb, tmp_buf, len);

    if (ic->duration != AV_NOPTS_VALUE)
    {
        int hours, mins, secs;
        duration = secs = ic->duration / AV_TIME_BASE;
        mins = secs / 60;
        secs %= 60;
        hours = mins / 60;
        mins %= 60;
        sb_add_string_len(sb, tmp_buf, sprintf(tmp_buf, ", duration: %02d:%02d:%02d", hours, mins, secs));
    }
    else
        sb_add_string(sb, ", duration: N/A");

#if 0
    if (ic->start_time != AV_NOPTS_VALUE)
    {
        int secs, us;
        secs = ic->start_time / AV_TIME_BASE;
        us = ic->start_time % AV_TIME_BASE;
        sb_add_string_len(sb, tmp_buf, sprintf(tmp_buf, ", start: %d.%06d", secs, (int)av_rescale(us, 1000000, AV_TIME_BASE)));
    }
#endif

    // some formats, eg. flv, dont seem to support bit_rate, so we'll prefer to 
    // calculate from duration.
    // is this ok? probably not ok with .vob files when duration is wrong. DEBUG

    if (ic->bit_rate)
        sb_add_string_len(sb, tmp_buf, sprintf(tmp_buf, ", bitrate: %"PRId64" kb/s", ic->bit_rate / 1000));
    else if (duration > 0)
        sb_add_string_len(sb, tmp_buf, sprintf(tmp_buf, ", avg.bitrate: %.0f kb/s", (double) file_size * 8.0 / duration / 1000));
    else
        sb_add_string(sb, ", bitrate: N/A");

    get_stream_info_type(sb, ic, AVMEDIA_TYPE_AUDIO,    sample_aspect_ratio, o->v_verbose);
    get_stream_info_type(sb, ic, AVMEDIA_TYPE_VIDEO,    sample_aspect_ratio, o->v_verbose);
    get_stream_info_type(sb, ic, AVMEDIA_TYPE_SUBTITLE, sample_aspect_ratio, o->v_verbose);
}

void dump_format_context(AVFormatContext *p, int index, const char *url, const struct options *o)
{
    AVRational GB_A_RATIO = {0, 1};
    //av_log(NULL, AV_LOG_ERROR, "\n");
    av_log(NULL, AV_LOG_VERBOSE, "***dump_format_context, name: %s, long_name: %s\n", 
        p->iformat->name, p->iformat->long_name);

    // dont show scaling info at this time because we dont have the proper sample_aspect_ratio
    struct string_buffer sb;
    sb_init(&sb);
    get_stream_info(&sb, p, url, 0, GB_A_RATIO, o);
    av_log(NULL, AV_LOG_INFO, "%s\n", sb.s);
    sb_destroy(&sb);

    av_log(NULL, AV_LOG_VERBOSE, "start_time av: %"PRId64", duration av: %"PRId64"\n",
        p->start_time, p->duration);
    av_log(NULL, AV_LOG_VERBOSE, "start_time s: %.2f, duration s: %.2f\n",
        (double) p->start_time / AV_TIME_BASE, (double) p->duration / AV_TIME_BASE);

    AVDictionaryEntry* track    = av_dict_get(p->metadata, "track",     NULL, 0);
    AVDictionaryEntry* title    = av_dict_get(p->metadata, "title",     NULL, 0);
    AVDictionaryEntry* author   = av_dict_get(p->metadata, "author",    NULL, 0);
    AVDictionaryEntry* copyright= av_dict_get(p->metadata, "copyright", NULL, 0);
    AVDictionaryEntry* comment  = av_dict_get(p->metadata, "comment",   NULL, 0);
    AVDictionaryEntry* album    = av_dict_get(p->metadata, "album",     NULL, 0);
    AVDictionaryEntry* year     = av_dict_get(p->metadata, "year",      NULL, 0);
    AVDictionaryEntry* genre    = av_dict_get(p->metadata, "genre",     NULL, 0);

    if (track)
        av_log(NULL, AV_LOG_INFO, "  Track: %s\n",     track->value);
    if (title)
        av_log(NULL, AV_LOG_INFO, "  Title: %s\n",     title->value);
    if (author)
        av_log(NULL, AV_LOG_INFO, "  Author: %s\n",    author->value);
    if (copyright)
        av_log(NULL, AV_LOG_INFO, "  Copyright: %s\n", copyright->value);
    if (comment)
        av_log(NULL, AV_LOG_INFO, "  Comment: %s\n",   comment->value);
    if (album)
        av_log(NULL, AV_LOG_INFO, "  Album: %s\n",     album->value);
    if (year)
        av_log(NULL, AV_LOG_INFO, "  Year: %s\n",      year->value);
    if (genre)
        av_log(NULL, AV_LOG_INFO, "  Genre: %s\n",     genre->value);
}

double uint8_cmp(const uint8_t *pa, const uint8_t *pb, const uint8_t *pc, int n)
{
    int i, same = 0;
    for (i = 0; i < n; i++)
    {
        int diffab = pa[i] - pb[i];
        int diffac = pa[i] - pc[i];
        int diffbc = pb[i] - pc[i];

        if (abs(diffab) < 20 && abs(diffac) < 20 && abs(diffbc) < 20)
            same++;
    }
    return (double)same / n;
}

/*
return sameness of the frame; 1 means the frame is the same in all directions, i.e. blank
pFrame must be an AV_PIX_FMT_RGB24 frame
*/
double blank_frame(AVFrame *pFrame, int width, int height)
{
    uint8_t *src = pFrame->data[0];
    int hor_size = height/11 * width * 3;
    uint8_t *pa = src+hor_size*2;
    uint8_t *pb = src+hor_size*5;
    uint8_t *pc = src+hor_size*8;
    double same = .4*uint8_cmp(pa, pb, pc, hor_size);
    int ver_size = hor_size/3;
    same += .6/3*uint8_cmp(pa, pa + ver_size, pa + ver_size*2, ver_size);
    same += .6/3*uint8_cmp(pb, pb + ver_size, pb + ver_size*2, ver_size);
    same += .6/3*uint8_cmp(pc, pc + ver_size, pc + ver_size*2, ver_size);
    return same;
}

/* global */
uint64_t gb_video_pkt_pts = AV_NOPTS_VALUE;


/**
 * Convert an error code into a text message.
 * @param error Error code to be converted
 * @return Corresponding error text (not thread-safe)
 */
static const char *get_error_text(const int error)
{
    static char error_buffer[255];
    av_strerror(error, error_buffer, sizeof(error_buffer));
    return error_buffer;
}

int get_frame_from_packet(AVCodecContext *pCodecCtx,
                      AVPacket       *pkt,
                      AVFrame        *pFrame)
{
    int fret;

    /// send packet for decoding
    fret = avcodec_send_packet(pCodecCtx, pkt);

    // ignore invalid packets and continue
    if(fret == AVERROR_INVALIDDATA ||
       fret == -1 /* Operation not permitted */
    )
        return AVERROR(EAGAIN);
    
    if (fret < 0)
    {
        av_log(NULL, AV_LOG_ERROR,  "Error sending a packet for decoding - %s\n", get_error_text(fret));
        exit(EXIT_ERROR);
    }

    fret = avcodec_receive_frame(pCodecCtx, pFrame);

    if (fret == AVERROR(EAGAIN))
        return fret;

    if (fret == AVERROR_EOF)
    {
        av_log(NULL, AV_LOG_ERROR, "No more frames: recieved AVERROR_EOF\n");
        return -1;
    }
    if (fret == AVERROR(EINVAL))
    {
        av_log(NULL, AV_LOG_ERROR, "Codec not opened: recieved AVERROR(EINVAL)\n");
        return -1;
    }
    if (fret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Error during decoding packet\n");
        exit(EXIT_ERROR);
    }
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(55, 34, 100)
    av_log(NULL, AV_LOG_VERBOSE, "Got picture from frame pts=%"PRId64"\n", pFrame->pts);
#else
    av_log(NULL, AV_LOG_VERBOSE, "Got picture, Frame pkt_pts=%"PRId64"\n", pFrame->pkt_pts);
#endif
    return 0;
}

/**
 * @brief read packet and decode it into a frame
 * @param pFormatCtx - input
 * @param pCodecCtx - input
 * @param pFrame - decoded video frame
 * @param video_index - input
 * @param pPts - on succes it is set to packet's pts
 * @return >0 if can read packet(s) & decode a frame
 *          0 if end of file
 *         <0 if error
 */
int
video_decode_next_frame(AVFormatContext *pFormatCtx,
       AVCodecContext  *pCodecCtx,
       AVFrame         *pFrame,     /* OUTPUT */
       int              video_index,
       int64_t         *pPts        /* OUTPUT */
       )
{
    assert(pFrame);
    assert(pPts);

    AVPacket*   pkt;
    AVStream*   pStream = pFormatCtx->streams[video_index];
    int         fret;       //function return code
    uint64_t    pkt_without_pic=0;
    int         decoded_frame = 0;

    static int    run = 0;               // # of times read_and_decode has been called for a file
    static double avg_decoded_frame = 0; // average # of decoded frame

    pkt = av_packet_alloc();
    if (!pkt)
    {
        av_log(NULL, AV_LOG_ERROR, "Could not allocate packet\n");
        return -1;
    }

    while (1)
    {
        // read packet
        do
        {
            av_packet_unref(pkt);
            fret = av_read_frame(pFormatCtx, pkt);
            if (fret)
            {
                char errbuf[256];
                av_log(NULL, AV_LOG_ERROR, "Error reading from video file: %s\n", av_make_error_string(errbuf, sizeof(errbuf), fret));
                return 0;
            }
        } while (pkt->stream_index != video_index);

        pkt_without_pic++;

        dump_packet(pkt, pStream);

        // Save global pts to be stored in pFrame in first call
        av_log(NULL, AV_LOG_VERBOSE, "*saving gb_video_pkt_pts: %"PRId64"\n", pkt->pts);
        gb_video_pkt_pts = pkt->pts;

        // try to decode packet
        fret = get_frame_from_packet(pCodecCtx, pkt, pFrame);

        // need more video packet(s)
        if (fret == AVERROR(EAGAIN))
        {
            if (pkt_without_pic % 50 == 0)
                av_log(NULL, AV_LOG_INFO, "  no picture in %"PRId64" packets\n", pkt_without_pic);

            if (pkt_without_pic >= MAX_PACKETS_WITHOUT_PICTURE)
            {
                av_log(NULL, AV_LOG_ERROR, "  * av_read_frame couldn't decode picture in %d packets\n", MAX_PACKETS_WITHOUT_PICTURE);
                av_packet_unref(pkt);
                av_packet_free(&pkt);
                return -1;
            }
            continue;
        }

        // decoded frame
        if (fret == 0)
        {
            pkt_without_pic = 0;
            decoded_frame++;

            av_log(NULL, AV_LOG_VERBOSE, "*get_videoframe got frame: key_frame: %d, pict_type: %c\n", pFrame->key_frame, av_get_picture_type_char(pFrame->pict_type));

            if (decoded_frame % 200 == 0)
                av_log(NULL, AV_LOG_INFO, "  picture not decoded in %d frames\n", decoded_frame);
            break;
        }
        // error decoding packet
        av_packet_unref(pkt);
        av_packet_free(&pkt);
        return -1;
    }  // end of while

    av_packet_unref(pkt);
    av_packet_free(&pkt);

    run++;
    avg_decoded_frame = (avg_decoded_frame*(run-1) + decoded_frame) / run;

    av_log(NULL, AV_LOG_VERBOSE, "*****got picture, repeat_pict: %d%s, key_frame: %d, pict_type: %c\n", pFrame->repeat_pict,
        (pFrame->repeat_pict > 0) ? "**r**" : "", pFrame->key_frame, av_get_picture_type_char(pFrame->pict_type));

    dump_stream(pStream);
    dump_codec_context(pCodecCtx);

    *pPts = gb_video_pkt_pts;
    return 1;
}


/* calculate timestamp to display to users
*/
double calc_time(int64_t timestamp, AVRational time_base, double start_time)
{
    // for files with start_time > 0, we need to subtract the start_time 
    // from timestamp. this should match the time display by MPC & VLC. 
    // however, for .vob files of dvds, after subtracting start_time
    // each file will start with timestamp 0 instead of continuing from the previous file.

    return av_rescale(timestamp, time_base.num, time_base.den) - start_time;
}

/*
return the duration. guess when unknown.
must be called after codec has been opened
*/
double guess_duration(AVFormatContext *pFormatCtx, int index, AVCodecContext *pCodecCtx, AVFrame *pFrame)
{
    double duration = (double) pFormatCtx->duration / AV_TIME_BASE; // can be incorrect for .vob files
    if (duration > 0)
        return duration;

    AVStream *pStream = pFormatCtx->streams[index];
    double guess;

    // if stream bitrate is known we'll interpolate from file size.
    // pFormatCtx->start_time would be incorrect for .vob file with multiple titles.
    // pStream->start_time doesn't work either. so we'll need to disable timestamping.
    assert(pStream && pCodecCtx);

    int64_t file_size = avio_size(pFormatCtx->pb);

    if (pCodecCtx->bit_rate > 0 && file_size > 0)
    {
        guess = 0.9 * file_size / (pCodecCtx->bit_rate / 8);
        if (guess > 0)
        {
            av_log(NULL, AV_LOG_ERROR, "  ** duration is unknown: %.2f; guessing: %.2f s from bit_rate\n", duration, guess);
            return guess;
        }
    }

    return -1;
    
    // the following doesn't work.
    /*
    // we'll guess the duration by seeking to near the end of the file and
    // decode a frame. the timestamp of that frame is the guess.
    // things get more complicated for dvd's .vob files. each .vob file
    // can contain more than 1 title. and each title will have its own pts.
    // for example, 90% of a .vob might be for title 1 and the last 10%
    // might be for title 2; seeking to near the end will end up getting 
    // title 2's pts. this problem cannot be solved if we just look at the
    // .vob files. need to process other info outside .vob files too.
    // as a result, the following will probably never work.
    // .vob files weirdness will make our assumption to seek by byte incorrect too.
    if (pFormatCtx->file_size <= 0) {
        return -1;
    }
    int64_t byte_pos = 0.9 * pFormatCtx->file_size;
    int ret = av_seek_frame(pFormatCtx, index, byte_pos, AVSEEK_FLAG_BYTE);
    if (ret < 0) { // failed
        return -1;
    }
    avcodec_flush_buffers(pCodecCtx);
    int64_t pts;
    ret = read_and_decode(pFormatCtx, index, pCodecCtx, pFrame, &pts, 0); // FIXME: key or not?
    if (ret <= 0) { // end of file or error
        av_log(NULL, AV_LOG_VERBOSE, "  read_and_decode during guessing duration failed\n");
        return -1;
    }
    double start_time = (double) pFormatCtx->start_time / AV_TIME_BASE; // FIXME: can be unknown?
    guess = calc_time(pts, pStream->time_base, start_time);
    if (guess <= 0) {
        return -1;
    }
    av_log(NULL, AV_LOG_ERROR, "  ** duration is unknown: %.2f; guessing: %.2f s.\n", duration, guess);

    // seek back to 0 & flush buffer; FIXME: is 0 correct?
    av_seek_frame(pFormatCtx, index, 0, AVSEEK_FLAG_BYTE); // ignore errors
    avcodec_flush_buffers(pCodecCtx);

    return guess;
    */
}

/*
try hard to seek
assume flags can be either 0 or AVSEEK_FLAG_BACKWARD
*/
int really_seek(AVFormatContext *pFormatCtx, int index, int64_t timestamp, double duration)
{
    int ret;

    /* first try av_seek_frame */
    ret = av_seek_frame(pFormatCtx, index, timestamp, 0);
    if (ret >= 0) // success
        return ret;

    /* then we try seeking to any (non key) frame AVSEEK_FLAG_ANY */
    ret = av_seek_frame(pFormatCtx, index, timestamp, AVSEEK_FLAG_ANY);
    if (ret >= 0) // success
    {
        av_log(NULL, AV_LOG_INFO, "AVSEEK_FLAG_ANY: timestamp: %"PRId64"\n", timestamp); // DEBUG
        return ret;
    }

    /* and then we try seeking by byte (AVSEEK_FLAG_BYTE) */
    // here we assume that the whole file has duration seconds.
    // so we'll interpolate accordingly.
    AVStream *pStream = pFormatCtx->streams[index];
    double start_time = (double) pFormatCtx->start_time / AV_TIME_BASE; // in seconds
    // if start_time is negative, we ignore it; FIXME: is this ok?
    if (start_time < 0)
        start_time = 0;

    // normally when seeking by timestamp we add start_time to timestamp 
    // before seeking, but seeking by byte we need to subtract the added start_time
    timestamp -= start_time / av_q2d(pStream->time_base);
    int64_t file_size = avio_size(pFormatCtx->pb);
    if (file_size <= 0)
        return -1;
    if (duration > 0)
    {
        int64_t duration_tb = (int64_t) (duration / av_q2d(pStream->time_base)); // in time_base unit
        int64_t byte_pos = av_rescale(timestamp, file_size, duration_tb);
        av_log(NULL, AV_LOG_INFO, "AVSEEK_FLAG_BYTE: byte_pos: %"PRId64", timestamp: %"PRId64", file_size: %"PRId64", duration_tb: %"PRId64"\n", byte_pos, timestamp, file_size, duration_tb);
        return av_seek_frame(pFormatCtx, index, byte_pos, AVSEEK_FLAG_BYTE);
    }

    return -1;
}

#if 0
/* 
modify name so that it'll (hopefully) be unique
by inserting a unique string before suffix.
if unum is != 0, it'll be used
returns the unique number
*/
int make_unique_name(char *name, char *suffix, int unum)
{
    // tmpnam() in mingw always return names which start with \ -- unuseable.
    // so we'll use random number instead.

    char unique[FILENAME_MAX];
    if (unum == 0) {
        unum = rand();
    }
    sprintf(unique, "_%d", unum);

    char *found = strlaststr(name, suffix);
    if (NULL == found || found == name) {
        strcat(name, unique); // this shouldn't happen
    } else {
        strcat(unique, found);
        strcpy(found, unique);
    }
    return unum;
}
#endif

/*
 * find first usable video stream (not cover art)
 * based on av_find_default_stream_index()
 * returns
 *      >0: video index
 *      -1: can't find any usable video
 */
int
find_default_videostream_index(AVFormatContext *s, int user_selected_video_stream)
{
    int default_stream_idx = -1;
    int cover_image;
    int n_video_stream = 0;
    unsigned int i;
    AVStream *st;

    for (i = 0; i < s->nb_streams; i++)
    {
        st = s->streams[i];
        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            cover_image = st->disposition & AV_DISPOSITION_ATTACHED_PIC;
            if (user_selected_video_stream)
            {
                if (++n_video_stream == user_selected_video_stream)
                {
                    default_stream_idx = i;
                    av_log(NULL, AV_LOG_INFO, "Selecting video stream (-S): %d\n", user_selected_video_stream);
                    if (cover_image)
                        av_log(NULL, AV_LOG_INFO, "  Warning: Selected video stream is \"cover art\"\n");
                    break;
                }
            }
            else if (!cover_image)
            {
                default_stream_idx = i;
                break;
            }
        }
    }

    return default_stream_idx;
}

gdImagePtr rotate_gdImage(gdImagePtr ip, int angle)
{
    if(angle == 0)
        return ip;
    
    int win = gdImageSX(ip);
    int hin = gdImageSY(ip);
    int wout = win;
    int hout = hin;

    if(abs(angle) == 90) {
        wout = hin;
        hout = win;
    }

    gdImagePtr ipr = gdImageCreateTrueColor(wout, hout);

    int i,j;

    for(i=0; i<win; i++)
        for(j=0; j<hin; j++)
            switch(angle)
            {
                case -180:
                case +180:
                    gdImageSetPixel(ipr, wout-i, hout-j, gdImageGetPixel(ip, i, j));
                    break;
                case   90:
                    gdImageSetPixel(ipr, j,      hout-i, gdImageGetPixel(ip, i, j));
                    break;
                case  -90:
                    gdImageSetPixel(ipr, wout-j, i,      gdImageGetPixel(ip, i, j));
                    break;
                default:
                    gdImageDestroy(ipr);
                    return ip;
            }
            
    gdImageDestroy(ip);
    return ipr;
}

/*
 * Find and extract album art / cover image
 */
void save_cover_image(const AVFormatContext *s, const char* cover_filename)
{
    int cover_stream_idx = -1;
    unsigned int i;

    // find first stream with cover art
    for (i = 0; i < s->nb_streams; i++)
        if (s->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
            (s->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC))
        {
            cover_stream_idx = i;
            break;
        }

    if (cover_stream_idx > -1)
    {
        const AVPacket *pkt = &s->streams[cover_stream_idx]->attached_pic;
        if (pkt->data && pkt->size > 0)
        {
            av_log(NULL, AV_LOG_VERBOSE, "Found cover art in stream index %d.\n", cover_stream_idx);
            const tchar_t *cover_filename_w = utf8_to_tchar(cover_filename);
            FILE *image_file = _tfopen(cover_filename_w, BINARY_WRITE_MODE);
            free_conv_result((tchar_t *) cover_filename_w);
            if (image_file)
            {
                fwrite(pkt->data, pkt->size, 1, image_file);
                fclose(image_file);
            }
            else
                av_log(NULL, AV_LOG_ERROR, "Error opening file \"%s\" for writting!\n", cover_filename);
        }
    }
    else
        av_log(NULL, AV_LOG_VERBOSE, "No cover art found.\n");
}

void
calculate_thumbnail(
        int req_step,
        int req_cols,
        int req_rows,
        int src_width,
        int src_height,
        int duration,
        struct thumbnail *tn,
        const struct options *o
)
{

    tn->column = req_cols;

    if (req_step > 0)
        tn->step_t = (int64_t) (req_step / tn->time_base);
    else
        tn->step_t = (int64_t) (duration / tn->time_base / (tn->column * req_rows + 1));

    if (req_rows > 0)
        tn->row = req_rows;
        // if # of columns is reduced, we should increase # of rows so # of tiles would be almost the same
        // could some users not want this?
    else // as many rows as needed
        tn->row = (int) (floor(duration / tn->column / (tn->step_t * tn->time_base) + 0.5)); // round nearest

    if (tn->row < 1)
        tn->row = 1;

    // make sure last row is full
    tn->step_t = (int64_t) (duration / tn->time_base / (tn->column * tn->row + 1));

    int full_width = tn->column * (src_width + o->g_gap) + o->g_gap;
    if (o->w_width > 0 && o->w_width < full_width)
        tn->img_width = o->w_width;
    else
        tn->img_width = full_width;

    tn->shot_width_out = floor((tn->img_width - o->g_gap*(tn->column+1)) / (double) tn->column + 0.5); // round nearest
    tn->shot_width_out -= tn->shot_width_out%2; // floor to even number
    tn->shot_height_out = floor((double) src_height / src_width * tn->shot_width_out + 0.5); // round nearest
    tn->shot_height_out -= tn->shot_height_out%2; // floor to even number
    tn->center_gap = (tn->img_width - o->g_gap*(tn->column+1) - tn->shot_width_out * tn->column) / 2;
}

void
reduce_shots_to_fit_in(
    int req_step,
    int req_rows,
    int req_cols,
    int src_width,
    int src_height,
    int duration,
    struct thumbnail *tn,
    const struct options *o
)
{
    int reduced_columns = req_cols + 1;  // will be -1 in the loop

    tn->step_t = -99999;
    tn->column = -99999;
    tn->row = -99999;
    tn->img_width = -99999;
    tn->shot_width_out = -99999;
    tn->shot_height_out = -99999;

    // reduce # of columns to meet required height
    while (tn->shot_height_out < o->h_height && reduced_columns > 0 && tn->shot_width_out != src_width)
    {
        reduced_columns--;
        calculate_thumbnail(req_step, reduced_columns, req_rows, src_width, src_height, duration, tn, o);
    }

    // reduce # of rows if movie is too short
    if (tn->step_t <= 0 && tn->column > 0 && tn->row > 1)
    {
        int reduced_rows = (int) ((double) (duration-1) / (double) reduced_columns);

        if (reduced_rows == 0)
            reduced_rows = 1;

        av_log(NULL, AV_LOG_INFO, "  movie is too short, reducing number of rows to %d\n", reduced_rows);

        calculate_thumbnail(req_step, reduced_columns, reduced_rows, src_width, src_height, duration, tn, o);
    }
}

/*
 * return   0 ok
 *         -1 something went wrong
 *          1 some images are missing
 */
int make_thumbnail(const char *file, const struct options *o, int nb_file)
{
    int return_code = -1;
    av_log(NULL, AV_LOG_VERBOSE, "make_thumbnail: %s\n", file);
    int idx = 0;
    int thumb_nb = 0;

    int64_t tstart = get_current_time();

    struct thumbnail tn; // thumbnail data & info
    thumb_new(&tn);

    struct string_buffer info_buf;
    struct string_buffer individual_filename;

    sb_init(&info_buf);
    sb_init(&individual_filename);
    
    struct sprite *sprite = NULL;
    
    gdImagePtr thumbShadowIm = NULL;
    int shadow_radius = o->shadow;

    int nb_shots = 0; // # of decoded shots (stat purposes)

    /* these are checked during cleaning up, must be NULL if not used */
    AVFormatContext *pFormatCtx = NULL;
    AVCodecContext *pCodecCtx = NULL;
    AVFrame *pFrame = NULL;
    AVFrame *pFrameRGB = NULL;
    uint8_t *rgb_buffer = NULL;
    struct SwsContext *pSwsCtx = NULL;
    tn.out_ip = NULL;
    FILE *info_fp = NULL;
    gdImagePtr ip = NULL;
    tchar_t *out_filename = NULL;
    tchar_t *info_filename = NULL;

    int t_timestamp = o->t_timestamp; // local timestamp; can be turned off; 0 = off
    int ret;

    if (nb_file)
        av_log(NULL, AV_LOG_INFO, "\n");

    if (o->O_outdir && *o->O_outdir)
    {
        sb_add_string(&tn.base_filename, o->O_outdir);
        sb_add_string(&tn.base_filename, FOLDER_SEPARATOR);
        sb_add_string(&tn.base_filename, basename(file));
    }
    else
        sb_add_string(&tn.base_filename, file);

    if (!o->X_filename_use_full)
    {
        const char *filename = basename(tn.base_filename.s);
        const char *extpos = strrchr(filename, '.');
        // remove movie extenxtion (e.g. .avi)
        if (extpos)
            sb_shrink(&tn.base_filename, extpos - tn.base_filename.s);
    }

    sb_add_buffer(&tn.out_filename, &tn.base_filename);
    sb_add_string(&tn.out_filename, o->o_suffix);

    if (o->N_suffix && *o->N_suffix)
    {
        sb_add_buffer(&tn.info_filename, &tn.base_filename);
        sb_add_string(&tn.info_filename, o->N_suffix);
    }

    if (o->cover)
    {
        sb_add_buffer(&tn.cover_filename, &tn.base_filename);
        sb_add_string(&tn.cover_filename, o->cover_suffix);
    }

    // idenfity thumbnail image extension
    const char *image_extension;
    const char *suffix = strrchr(tn.out_filename.s, '.');
    if (suffix && strcasecmp(suffix, ".png") == 0)
        image_extension = IMAGE_EXTENSION_PNG;
    else
        image_extension = IMAGE_EXTENSION_JPG;

    out_filename = utf8_to_tchar(tn.out_filename.s);
    if (tn.info_filename.len)
        info_filename = utf8_to_tchar(tn.info_filename.s);
    
#if 0
    // if output files exist and modified time >= program start time,
    // we'll not overwrite and use a new name
    int unum = 0;
    if (is_reg_newer(out_filename, gb_st_start))
    {
        unum = make_unique_name(tn.out_filename, gb_o_suffix, unum);
        av_log(NULL, AV_LOG_INFO, "%s: output file already exists. using: %s\n", gb_argv0, tn.out_filename);
        free_conv_result(out_filename);
        out_filename = utf8_to_tchar(tn.out_filename);
    }
    if (gb_N_suffix && is_reg_newer(info_filename, gb_st_start))
    {
        unum = make_unique_name(tn.info_filename, gb_N_suffix, unum);
        av_log(NULL, AV_LOG_INFO, "%s: info file already exists. using: %s\n", gb_argv0, tn.info_filename);
        free_conv_result(info_filename);
        info_filename = utf8_to_tchar(tn.info_filename);
    }
#endif
    if (!o->W_overwrite) // don't overwrite mode
    {
        if (is_reg(out_filename))
        {
            av_log(NULL, AV_LOG_INFO, "%s: output file %s already exists. omitted.\n", gb_argv0, tn.out_filename.s);
            return_code = 0;
            goto cleanup;
        }
        if (info_filename && is_reg(info_filename))
        {
            av_log(NULL, AV_LOG_INFO, "%s: info file %s already exists. omitted.\n", gb_argv0, tn.info_filename.s);
            return_code = 0;
            goto cleanup;
        }
    }
    if (info_filename)
    {
        av_log(NULL, AV_LOG_INFO, "\nCreating info file %s\n", tn.info_filename.s);
        info_fp = _tfopen(info_filename, TEXT_WRITE_MODE);
        if (!info_fp)
        {
            av_log(NULL, AV_LOG_ERROR, "\n%s: creating info file '%s' failed: %s\n", gb_argv0, tn.info_filename.s, strerror(errno));
            goto cleanup;
        }
    }

    // Open video file
    AVDictionary *dict = NULL;
    if (o->dict)
        av_dict_copy(&dict, o->dict, 0);
    ret = avformat_open_input(&pFormatCtx, file, NULL, dict ? &dict : NULL);
    if (dict)
        av_dict_free(&dict);
    if (ret)
    {
        av_log(NULL, AV_LOG_ERROR, "\n%s: avformat_open_input %s failed: %d\n", gb_argv0, file, ret);
        goto cleanup;
    }

    // generate pts?? -- from ffplay, not documented
    // it should make av_read_frame() generate pts for unknown value
    assert(pFormatCtx);
    pFormatCtx->flags |= AVFMT_FLAG_GENPTS;

    // Retrieve stream information
    ret = avformat_find_stream_info(pFormatCtx, NULL);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "\n%s: avformat_find_stream_info %s failed: %d\n", gb_argv0, file, ret);
        goto cleanup;
    }
    dump_format_context(pFormatCtx, nb_file, file, o);

    // Find videostream
    int video_index = find_default_videostream_index(pFormatCtx, o->S_select_video_stream);
    if (video_index == -1)
    {
        if (!o->S_select_video_stream)
            av_log(NULL, AV_LOG_ERROR, "  couldn't find a video stream\n");
        else
            av_log(NULL, AV_LOG_ERROR, "  couldn't find selected video stream (-S %d)\n", o->S_select_video_stream);
        goto cleanup;
    }

    AVStream *pStream = pFormatCtx->streams[video_index];
    pCodecCtx = get_codecContext_from_codecParams(pStream->codecpar);
    tn.time_base = av_q2d(pStream->time_base);

    if (!pCodecCtx)
        goto cleanup;

    if ((tn.rotation = get_stream_rotation(pStream)) != 0)
        av_log(NULL, AV_LOG_INFO,  "  Rotation: %d degrees\n", tn.rotation);

    dump_stream(pStream);
    //dump_index_entries(pStream);
    dump_codec_context(pCodecCtx);
    av_log(NULL, AV_LOG_VERBOSE, "\n");

    // Find the decoder for the video stream
    const AVCodec *pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
    if (!pCodec)
    {
        av_log(NULL, AV_LOG_ERROR, "  couldn't find a decoder for codec_id: %d\n", pCodecCtx->codec_id);
        goto cleanup;
    }
//    const AVCodec *pCodec = pCodecCtx->codec;

    // discard frames; is this OK?? // FIXME
    if (o->s_step >= 0)
    {
        // nonkey & bidir cause program crash with some files, e.g. tokyo 275 .
        // codec bugs???
        //pCodecCtx->skip_frame = AVDISCARD_NONKEY; // slower with nike 15-11-07
        //pCodecCtx->skip_frame = AVDISCARD_BIDIR; // this seems to speed things up
        pCodecCtx->skip_frame = AVDISCARD_NONREF; // internal err msg but not crash
    }

    // Open codec
    ret = avcodec_open2(pCodecCtx, pCodec, NULL);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "  couldn't open codec %s id %d: %d\n", pCodec->name, pCodec->id, ret);
        goto cleanup;
    }

    // Allocate video frame
    pFrame = av_frame_alloc();
    if (!pFrame)
    {
        av_log(NULL, AV_LOG_ERROR, "  couldn't allocate a video frame\n");
        goto cleanup;
    }

    if (o->cover)
        save_cover_image(pFormatCtx, tn.cover_filename.s);

    // keep a copy of sample_aspect_ratio because it might be changed after 
    // decoding a frame, e.g. Dragonball Z 001 (720x480 H264 AAC).mkv
    AVRational sample_aspect_ratio = av_guess_sample_aspect_ratio(pFormatCtx, pStream, NULL);

    double duration = (double) pFormatCtx->duration / AV_TIME_BASE; // can be unknown & can be incorrect (e.g. .vob files)
    if (duration <= 0)
        duration = guess_duration(pFormatCtx, video_index, pCodecCtx, pFrame);
    if (duration <= 0)
    {
        // have to turn timestamping off because it'll be incorrect
        if (o->t_timestamp)
        {
            t_timestamp = 0;
            av_log(NULL, AV_LOG_ERROR, "  turning time stamp off because of duration\n");
        }
        av_log(NULL, AV_LOG_ERROR, "  duration is unknown: %.2f\n", duration);
        goto cleanup;
    }

    double start_time = (double) pFormatCtx->start_time / AV_TIME_BASE; // in seconds
    // VTS_01_2.VOB & beyond from DVD seem to be like this
    //if (start_time > duration) {
        //av_log(NULL, AV_LOG_VERBOSE, "  start_time: %.2f is more than duration: %.2f\n", start_time, duration);
        //goto cleanup;
    //}
    // if start_time is negative, we ignore it; FIXME: is this ok?
    if (start_time < 0)
        start_time = 0;
    int64_t start_time_tb = start_time * pStream->time_base.den / pStream->time_base.num; // in time_base unit
    //av_log(NULL, AV_LOG_ERROR, "  start_time_tb: %"PRId64"\n", start_time_tb);

    // decode the first frame without seeking.
    // without doing this, avcodec_decode_video wont be able to decode any picture
    // with some files, eg. http://download.pocketmovies.net/movies/3d/twittwit_320x184.mpg
    // bug reported by: swmaherl, jake_o from sourceforge
    // and pCodecCtx->width and pCodecCtx->height might not be correct without this
    // for .flv files. bug reported by: dragonbook 
    int64_t found_pts = -1;
    int64_t first_pts = -1; // pts of first frame
    ret = video_decode_next_frame(pFormatCtx, pCodecCtx, pFrame, video_index, &first_pts);
    if (!ret) // end of file
        goto eof;
    if (ret < 0) // error
    {
        av_log(NULL, AV_LOG_ERROR, "  read_and_decode first failed!\n");
        goto cleanup;
    }
    //av_log(NULL, AV_LOG_INFO, "first_pts: %"PRId64" (%.2f s)\n", first_pts, calc_time(first_pts, pStream->time_base, start_time)); // DEBUG

    // set sample_aspect_ratio
    // assuming sample_y = display_y
    if (o->a_ratio_num)
    {
        // use cmd line arg if specified
        AVRational ar = { o->a_ratio_num, o->a_ratio_den };
        sample_aspect_ratio.num = (double) pCodecCtx->height * av_q2d(ar) / pCodecCtx->width * 10000;
        sample_aspect_ratio.den = 10000;
        av_log(NULL, AV_LOG_INFO, "  *** using sample_aspect_ratio: %d/%d because of -a %.4f option\n", sample_aspect_ratio.num, sample_aspect_ratio.den, av_q2d(gb_a_ratio));
    }
    else
    {
        if (sample_aspect_ratio.num != 0 && pCodecCtx->sample_aspect_ratio.num != 0
            && av_q2d(sample_aspect_ratio) != av_q2d(pCodecCtx->sample_aspect_ratio))
        {
            av_log(NULL, AV_LOG_INFO, "  *** conflicting sample_aspect_ratio: %.2f vs %.2f: using %.2f\n",
                av_q2d(sample_aspect_ratio), av_q2d(pCodecCtx->sample_aspect_ratio), av_q2d(sample_aspect_ratio));
            av_log(NULL, AV_LOG_INFO, "      to use sample_aspect_ratio %.2f use: -a %.4f option\n",
                av_q2d(pCodecCtx->sample_aspect_ratio), av_q2d(pCodecCtx->sample_aspect_ratio) * pCodecCtx->width / pCodecCtx->height);
            // we'll continue with existing value. is this ok? FIXME
            // this is the same as mpc's and vlc's. 
        }
        if (sample_aspect_ratio.num == 0) // not defined
            sample_aspect_ratio = pCodecCtx->sample_aspect_ratio;
    }

    /* calc options */
    // FIXME: make sure values are ok when movies are very short or very small
    double net_duration;
    if (o->C_cut > 0)
    {
        net_duration = o->C_cut;
        if (net_duration + o->B_begin > duration)
        {
            net_duration = duration - o->B_begin;
            av_log(NULL, AV_LOG_ERROR, "  -C %.2f s is too long, using %.2f s.\n", o->C_cut, net_duration);
        }
    }
    else
    {
        //double net_duration = duration - start_time - gb_B_begin - gb_E_end;
        net_duration = duration - o->B_begin - o->E_end; // DVD
        if (net_duration <= 0)
        {
            av_log(NULL, AV_LOG_ERROR, "  duration: %.2f s, net duration after -B & -E is negative: %.2f s.\n", duration, net_duration);
            goto cleanup;
        }
    }

    /* scale according to sample_aspect_ratio. */
    int scaled_src_width, scaled_src_height;

    calc_scale_src(pCodecCtx->width, pCodecCtx->height, sample_aspect_ratio,
        &scaled_src_width, &scaled_src_height);

    if (scaled_src_width != pCodecCtx->width || scaled_src_height != pCodecCtx->height)
        av_log(NULL, AV_LOG_INFO, "  * scaling input * %dx%d => %dx%d according to sample_aspect_ratio %d/%d\n",
            pCodecCtx->width, pCodecCtx->height, scaled_src_width, scaled_src_height, 
            sample_aspect_ratio.num, sample_aspect_ratio.den);

    int seek_mode = 1; // 1 = seek; 0 = non-seek
    int scaled_src_width_out  = scaled_src_width;
    int scaled_src_height_out = scaled_src_height;

    if (abs(tn.rotation) == 90)
    {
        scaled_src_width_out  = scaled_src_height;
        scaled_src_height_out = scaled_src_width;
    }

    reduce_shots_to_fit_in(
        o->s_step,
        o->r_row,
        o->c_column,
        scaled_src_width_out,
        scaled_src_height_out,
        (int) net_duration,
        &tn, o
    );

    if (tn.column == 0)
    {
        int suggested_width, suggested_height;
        // guess new width and height to create thumbnails
        suggested_width = ceil(o->h_height * scaled_src_width_out/scaled_src_height_out + 2.0 * o->g_gap);
        suggested_width += suggested_width % 2;
        suggested_height = (int) floor((o->w_width - 2*o->g_gap) * scaled_src_height_out/scaled_src_width_out);
        suggested_height -= suggested_height % 2;

        av_log(NULL, AV_LOG_ERROR, "  thumbnail to small; increase image width to %d (-w) or decrease min. image height to %d (-h)\n" ,
               suggested_width, suggested_height);
        goto cleanup;
    }

    if (tn.step_t == 0)
    {
        av_log(NULL, AV_LOG_ERROR, "  step is zero; movie is too short?\n");
        goto cleanup;
    }

    if (abs(tn.rotation) == 90)
    {
        tn.shot_height_in = tn.shot_width_out;
        tn.shot_width_in  = tn.shot_height_out;
    }
    else
    {
        tn.shot_height_in = tn.shot_height_out;
        tn.shot_width_in  = tn.shot_width_out;
    }

    if (tn.column != o->c_column)
        av_log(NULL, AV_LOG_INFO, "  changing # of column to %d to meet minimum height of %d; see -h option\n", tn.column, o->h_height);
    if (o->w_width > 0 && o->w_width != tn.img_width)
        av_log(NULL, AV_LOG_INFO, "  changing width to %d to match movie's size (%dx%d)\n", tn.img_width, scaled_src_width, tn.column);
    if (info_fp || o->i_info)
        get_stream_info(&info_buf, pFormatCtx, file, 1, sample_aspect_ratio, o);
    if (info_fp)
    {
        fputs(info_buf.s, info_fp);
        fputc('\n', info_fp);
    }
    if (!o->i_info)
        sb_clear(&info_buf);
    if (o->T_text && *o->T_text)
    {
        sb_add_char(&info_buf, '\n');
        sb_add_string(&info_buf, o->T_text);
        if (info_fp)
        {
            fputs(o->T_text, info_fp);
            fputc('\n', info_fp);
        }
    }

    const int info_text_padding = image_string_padding(o->f_fontname, o->F_info_font_size);

    if (o->i_info)
        tn.txt_height = image_string_height(info_buf.s, o->f_fontname, o->F_info_font_size) + o->g_gap + info_text_padding;
    tn.img_height = tn.shot_height_out*tn.row + o->g_gap*(tn.row+1) + tn.txt_height;
    av_log(NULL, AV_LOG_INFO, "  step: %.1f s; # tiles: %dx%d, tile size: %dx%d; total size: %dx%d\n",
        tn.step_t*tn.time_base, tn.column, tn.row, tn.shot_width_out, tn.shot_height_out, tn.img_width, tn.img_height);

    // jpeg seems to have max size of 65500 pixels
    if (strcasecmp(image_extension, IMAGE_EXTENSION_JPG)==0 && (tn.img_width > 65500 || tn.img_height > 65500))
    {
        av_log(NULL, AV_LOG_ERROR, "  jpeg only supports max size of 65500\n");
        goto cleanup;
    }

    int64_t evade_step = MIN(10 / tn.time_base, tn.step_t / 14); // max 10 s to evade blank screen
    if (evade_step*tn.time_base <= 1)
    {
        evade_step = 0;
        av_log(NULL, AV_LOG_INFO, "  step is less than 14 s; blank & blur evasion is turned off.\n");
    }

    /* prepare for resize & conversion to AV_PIX_FMT_RGB24 */
    pFrameRGB = av_frame_alloc();
    if (!pFrameRGB)
    {
        av_log(NULL, AV_LOG_ERROR, "  couldn't allocate a video frame\n");
        goto cleanup;
    }
    int rgb_bufsize = av_image_get_buffer_size(AV_PIX_FMT_RGB24, tn.shot_width_in, tn.shot_height_in, LINESIZE_ALIGN);
    rgb_buffer = av_malloc(rgb_bufsize);
    if (!rgb_buffer)
    {
        av_log(NULL, AV_LOG_ERROR, "  av_malloc %d bytes failed\n", rgb_bufsize);
        goto cleanup;
    }
    // Returns: the size in bytes required for src, a negative error code in case of failure
    ret = av_image_fill_arrays(pFrameRGB->data, pFrameRGB->linesize, rgb_buffer, AV_PIX_FMT_RGB24, tn.shot_width_in, tn.shot_height_in, LINESIZE_ALIGN);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "  av_image_fill_arrays failed (%d)\n", ret);
        goto cleanup;
    }

    pSwsCtx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
        tn.shot_width_in, tn.shot_height_in, AV_PIX_FMT_RGB24, SWS_BILINEAR, NULL, NULL, NULL);
    if (!pSwsCtx)
    {
        av_log(NULL, AV_LOG_ERROR, "  sws_getContext failed\n");
        goto cleanup;
    }

    /* create the output image */
    tn.out_ip = gdImageCreateTrueColor(tn.img_width, tn.img_height);
    if (!tn.out_ip)
    {
        av_log(NULL, AV_LOG_ERROR, "  gdImageCreateTrueColor failed: width %d, height %d\n", tn.img_width, tn.img_height);
        goto cleanup;
    }

    if (o->webvtt)
        sprite = sprite_create(o->w_width, tn.shot_width_in, tn.shot_height_out, &tn);
    
    /* setting alpha blending is not needed, using default mode:
     * https://libgd.github.io/manuals/2.2.5/files/gd-h.html#Effects
    gdImageAlphaBlending(tn.out_ip,	
        //gdEffectReplace		//replace pixels
        gdEffectAlphaBlend	   	//blend pixels, see gdAlphaBlend
        //gdEffectNormal		//default mode; same as gdEffectAlphaBlend
        //gdEffectOverlay		//overlay pixels, see gdLayerOverlay
        //gdEffectMultiply	//overlay pixels with multiply effect, see gdLayerMultiply
    );
    */
    int background = gdImageColorResolve(tn.out_ip, RGB_R(o->k_bcolor), RGB_G(o->k_bcolor), RGB_B(o->k_bcolor)); // set backgroud
    gdImageFilledRectangle(tn.out_ip, 0, 0, tn.img_width, tn.img_height, background);

    if (o->transparent_bg)
        gdImageColorTransparent(tn.out_ip, background);

    /* add info & text */ // do this early so when font is not found we'll quit early
    if (info_buf.len)
    {
        char *error = image_string(tn.out_ip,
            o->f_fontname, o->F_info_color, o->F_info_font_size,
            o->L_info_location, o->g_gap, info_buf.s, 0, COLOR_WHITE, info_text_padding);
        if (error)
        {
            av_log(NULL, AV_LOG_ERROR, "  %s; font problem? see -f option\n", error);
            goto cleanup;
        }
    }

    /* if needed create shadow image used for every shot	*/
    if (o->shadow >= 0)
    {
        thumbShadowIm = create_shadow_image(background, tn.shot_width_out, tn.shot_height_out, &shadow_radius, o);
        if (!thumbShadowIm)
            goto cleanup;
    }

    /* alloc dynamic thumb data */
    if (thumb_alloc_dynamic(&tn) == -1)
    {
        av_log(NULL, AV_LOG_ERROR, "  thumb_alloc_dynamic failed\n");
        goto cleanup;
    }

    if (o->z_seek)
        seek_mode = 1;
    if (o->Z_nonseek)
    {
        seek_mode = 0;
        av_log(NULL, AV_LOG_INFO, "  *** using non-seek mode -- slower but more accurate timing.\n");
    }

    int64_t seek_target, seek_evade; // in time_base unit

    /* decode & fill in the shots */
  restart:
    seek_target = 0, seek_evade = 0; // in time_base unit
    if (!seek_mode && o->B_begin > 10)
        av_log(NULL, AV_LOG_INFO, "  -B %.2f with non-seek mode will take some time.\n", o->B_begin);

    int evade_try = 0; // blank screen evasion index
    double avg_evade_try = 0; // average
    seek_target = tn.step_t + (int64_t) ((start_time + o->B_begin) / tn.time_base);
    idx = 0; // idx = thumb_idx
    thumb_nb = tn.row * tn.column; // thumb_nb = # of shots we need
    int64_t prevshot_pts = -1; // pts of previous good shot
    int64_t prevfound_pts = -1; // pts of previous decoding
    gdImagePtr edge_ip = NULL; // edge image

    for (idx = 0; idx < thumb_nb; idx++)
    {
        int64_t eff_target = seek_target + seek_evade; // effective target
        eff_target = MAX(eff_target, start_time_tb); // make sure eff_target > start_time
        char time_str[64];
        format_time(calc_time(eff_target, pStream->time_base, start_time), time_str, sizeof(time_str), ':');

        /* for some formats, previous seek might over shoot pass this seek_target; is this a bug in libavcodec? */
        if (prevshot_pts > eff_target && !evade_try)
        {
            // restart in seek mode of skipping shots (FIXME)
            if (seek_mode && !o->z_seek)
            {
                av_log(NULL, AV_LOG_INFO, "  *** previous seek overshot target %s; switching to non-seek mode\n", time_str);
                av_seek_frame(pFormatCtx, video_index, 0, 0);
                avcodec_flush_buffers(pCodecCtx);
                seek_mode = 0;
                goto restart;
            }
            av_log(NULL, AV_LOG_INFO, "  skipping shot at %s because of previous seek or evasions\n", time_str);
            idx--;
            thumb_nb--;
            goto skip_shot;
        }

        // make sure eff_target > previous found
        eff_target = MAX(eff_target, prevfound_pts+1);

        format_time(calc_time(eff_target, pStream->time_base, start_time), time_str, sizeof(time_str), ':');
        av_log(NULL, AV_LOG_VERBOSE, "\n***eff_target tb: %"PRId64", eff_target s:%.2f (%s), prevshot_pts: %"PRId64"\n", 
            eff_target, calc_time(eff_target, pStream->time_base, start_time), time_str, prevshot_pts);

        /* jump to next shot */
        if (seek_mode)
        {
            ret = really_seek(pFormatCtx, video_index, eff_target, duration);
            if (ret < 0)
            {
                av_log(NULL, AV_LOG_ERROR, "  seeking to %.2f s failed\n", calc_time(eff_target, pStream->time_base, start_time));
                goto eof;
            }
            avcodec_flush_buffers(pCodecCtx);

            ret = video_decode_next_frame(pFormatCtx, pCodecCtx, pFrame, video_index, &found_pts);
            if (!ret) // end of file
                goto eof; // write into image everything we have so far
            if (ret < 0) // error
            {
                av_log(NULL, AV_LOG_ERROR, "  read&decode failed!\n");
                goto eof;
            }
        }
        else
        {
            // non-seek mode -- we keep decoding until we get to the next shot
            found_pts = 0;
            while (found_pts < eff_target)
            {
                // we should check if it's taking too long for this loop. FIXME
                ret =  video_decode_next_frame(pFormatCtx, pCodecCtx, pFrame, video_index, &found_pts);
                if (!ret) // end of file
                    goto eof;
                if (ret < 0) // error
                {
                    av_log(NULL, AV_LOG_ERROR, "  read&decode failed!\n");
                    goto eof;
                }
            }
        }
        //struct timeval dfinish; // DEBUG
        //gettimeofday(&dfinish, NULL); // calendar time; effected by load & io & etc. DEBUG
        //double decode_time = (dfinish.tv_sec + dfinish.tv_usec/1000000.0) - (dstart.tv_sec + dstart.tv_usec/1000000.0);
        double decode_time = 0;

        int64_t found_diff = found_pts - eff_target;
        //av_log(NULL, AV_LOG_INFO, "  found_diff: %.2f\n", found_diff); // DEBUG
        // if found frame is too far off from target, we'll disable seeking and start over
        if (idx < 5 && seek_mode && !o->z_seek 
            // usually movies have key frames every 10 s
            && (tn.step_t < (15/tn.time_base) || found_diff > 15/tn.time_base)
            && (found_diff <= -tn.step_t || found_diff >= tn.step_t))
        {
            // compute the approx. time it take for the non-seek mode, if too long print a msg instead
            double shot_dtime;
            if (scaled_src_width > 576*4/3.0) // HD
                shot_dtime = tn.step_t*tn.time_base * 30 / 30.0;
            else if (scaled_src_width > 288*4/3.0) // ~DVD
                shot_dtime = tn.step_t*tn.time_base * 30 / 80.0;
            else // small
                shot_dtime = tn.step_t*tn.time_base * 30 / 500.0;
            if (shot_dtime > 2 || shot_dtime * tn.column * tn.row > 120)
            {
                av_log(NULL, AV_LOG_INFO, "  *** seeking off target %.2f s, increase time step or use non-seek mode.\n", found_diff*tn.time_base);
                goto non_seek_too_long;
            }

            // disable seeking and start over
            av_seek_frame(pFormatCtx, video_index, 0, 0);
            avcodec_flush_buffers(pCodecCtx);
            seek_mode = 0;
            av_log(NULL, AV_LOG_INFO, "  *** switching to non-seek mode because seeking was off target by %.2f s.\n", found_diff*tn.time_base);
            av_log(NULL, AV_LOG_INFO, "  non-seek mode is slower. increase time step or use -z if you don't want this.\n");
            goto restart;
        }
      non_seek_too_long:

        nb_shots++;
        av_log(NULL, AV_LOG_VERBOSE, "shot %d: found_: %"PRId64" (%.2fs), eff_: %"PRId64" (%.2fs), dtime: %.3f\n", 
            idx, found_pts, calc_time(found_pts, pStream->time_base, start_time), 
            eff_target, calc_time(eff_target, pStream->time_base, start_time), decode_time);
        av_log(NULL, AV_LOG_VERBOSE, "approx. decoded frames/s: %.2f\n", tn.step_t * tn.time_base * 30 / decode_time); //TODO W: decode_time allways==0
        /*
        char debug_filename[2048]; // DEBUG
        sprintf(debug_filename, "%s_decoded%05d.jpg", tn.out_filename, nb_shots - 1);
        save_AVFrame(pFrame, pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt, 
            debug_filename, pCodecCtx->width, pCodecCtx->height);
        */

        // got same picture as previous shot, we'll skip it
        if (prevshot_pts == found_pts && !evade_try)
        {
            av_log(NULL, AV_LOG_INFO, "  skipping shot at %s because got previous shot\n", time_str);
            idx--;
            thumb_nb--;
            goto skip_shot;
        }

        /* convert to AV_PIX_FMT_RGB24 & resize */
        int output_height; //the height of the output slice
        output_height = sws_scale(pSwsCtx, (const uint8_t* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height,
            pFrameRGB->data, pFrameRGB->linesize);
        if (output_height <= 0)
        {
            av_log(NULL, AV_LOG_ERROR, "  sws_scale() failed\n");
            goto cleanup;
        }
        /*
        sprintf(debug_filename, "%s_resized%05d.jpg", tn.out_filename, nb_shots - 1); // DEBUG
        save_AVFrame(pFrameRGB, tn.shot_width, tn.shot_height, AV_PIX_FMT_RGB24,
            debug_filename, tn.shot_width, tn.shot_height);
        */

        /* if blank screen, try again */
        // FIXME: make sure this'll work when step is small
        // FIXME: make sure each shot wont get repeated
        double blank = blank_frame(pFrameRGB, tn.shot_width_out, tn.shot_height_out);
        // only do edge when blank detection doesn't work
        float edge[EDGE_PARTS] = {1,1,1,1,1,1}; // FIXME: change this if EDGE_PARTS is changed
        if (evade_step > 0 && blank <= o->b_blank && o->D_edge > 0)
            edge_ip = rotate_gdImage(
                detect_edge(pFrameRGB, &tn, edge, EDGE_FOUND, o),
                tn.rotation);

        //av_log(NULL, AV_LOG_VERBOSE, "  idx: %d, evade_try: %d, blank: %.2f%s edge: %.3f %.3f %.3f %.3f %.3f %.3f%s\n", 
        //    idx, evade_try, blank, (blank > gb_b_blank) ? "**b**" : "", 
        //    edge[0], edge[1], edge[2], edge[3], edge[4], edge[5], is_edge(edge, EDGE_FOUND) ? "" : "**e**"); // DEBUG
        if (evade_step > 0 && (blank > o->b_blank || !is_edge(edge, EDGE_FOUND)))
        {
            idx--;
            evade_try++;
            // we'll always search forward to support non-seek mode, which cant go backward
            // keep trying until getting close to next step
            seek_evade = evade_step * evade_try;
            if (seek_evade < tn.step_t - evade_step)
            {
                av_log(NULL, AV_LOG_VERBOSE, "  * blank or no edge * try #%d: seeking forward seek_evade: %"PRId64" (%.2f s)\n", 
                    evade_try, seek_evade, seek_evade * av_q2d(pStream->time_base));
                goto continue_cleanup;
            }

            // not found -- skip shot
            char time_str[64];
            format_time(calc_time(seek_target, pStream->time_base, start_time), time_str, sizeof(time_str), ':');
            av_log(NULL, AV_LOG_INFO, "  * blank %.2f or no edge * skipping shot at %s after %d tries\n", blank, time_str, evade_try);
            thumb_nb--; // reduce # shots
            goto skip_shot;
        }

        //
        avg_evade_try = (avg_evade_try * idx + evade_try ) / (idx+1); // DEBUG
        //av_log(NULL, AV_LOG_VERBOSE, "  *** avg_evade_try: %.2f\n", avg_evade_try); // DEBUG

        /* convert to GD image */
        ip = gdImageCreateTrueColor(tn.shot_width_in, tn.shot_height_in);
        if (!ip)
        {
            av_log(NULL, AV_LOG_ERROR, "  gdImageCreateTrueColor failed: width %d, height %d\n", tn.shot_width_in, tn.shot_height_in);
            goto cleanup;
        }
        FrameRGB_2_gdImage(pFrameRGB, ip, tn.shot_width_in, tn.shot_height_in);
        ip = rotate_gdImage(ip, tn.rotation);

        /* if debugging, save the edge instead */
        if (o->v_verbose && edge_ip)
        {
            gdImageDestroy(ip);
            ip = edge_ip;
            edge_ip = NULL;
        }

        if (o->webvtt)
            sprite_add_shot(sprite, ip, found_pts, o);

        const int timestamp_text_padding = image_string_padding(o->F_ts_fontname, o->F_ts_font_size);

        /* timestamping */
        // FIXME: this frame might not actually be at the requested position. is pts correct?
        if (t_timestamp)
        {
            char time_str[64];
            format_time(calc_time(found_pts, pStream->time_base, start_time), time_str, sizeof(time_str), ':');
            char *str_ret = image_string(ip, 
                o->F_ts_fontname, o->F_ts_color, o->F_ts_font_size, 
                o->L_time_location, 0, time_str, 1, o->F_ts_shadow, timestamp_text_padding);
            if (str_ret)
            {
                av_log(NULL, AV_LOG_ERROR, "  %s; font problem? see -f option or -F option\n", str_ret);
                goto cleanup; // LEAK: ip, edge_ip
            }
            /* stamp idx & blank & edge for debugging */
            if (o->v_verbose)
            {
                char idx_str[1024];
                snprintf(idx_str, sizeof(idx_str), "idx: %d, blank: %.2f\n%.6f  %.6f\n%.6f  %.6f\n%.6f  %.6f",
                    idx, blank, edge[0], edge[1], edge[2], edge[3], edge[4], edge[5]);
                image_string(ip, o->f_fontname, COLOR_WHITE, o->F_ts_font_size, 2, 0, idx_str, 1, COLOR_BLACK, 0);
            }
        }

        /* save individual shots */
        if (o->I_individual)
        {
            char time_str[64];
            format_time(calc_time(found_pts, pStream->time_base, start_time), time_str, sizeof(time_str), '_');

            if (!individual_filename.len)
                sb_add_buffer(&individual_filename, &tn.base_filename);

            char index_buf[64];
            int index_len = sprintf(index_buf, "_%05d%s", idx, image_extension); // image_extension can be ".jpg" or ".png"
            if (o->I_individual_thumbnail)
            {
                sb_add_string_len(&individual_filename, "_t_", 3);
                sb_add_string(&individual_filename, time_str);
                sb_add_string_len(&individual_filename, index_buf, index_len);
                if (save_image(ip, individual_filename.s, o))
                    av_log(NULL, AV_LOG_ERROR, "  saving individual shot #%05d to %s failed\n", idx, individual_filename.s);
                sb_shrink(&individual_filename, tn.base_filename.len);
            }

            if (o->I_individual_original)
            {
                sb_add_string_len(&individual_filename, "_o_", 3);
                sb_add_string(&individual_filename, time_str);
                sb_add_string_len(&individual_filename, index_buf, index_len);
                if (save_AVFrame(pFrame, pCodecCtx->width, pCodecCtx->height,
                    pCodecCtx->pix_fmt, individual_filename.s, pCodecCtx->width, pCodecCtx->height, o))
                    av_log(NULL, AV_LOG_ERROR, "  saving individual shot #%05d to %s failed\n", idx, individual_filename.s);
                sb_shrink(&individual_filename, tn.base_filename.len);
            }
        }

        /* add picture to output image */
        if (!o->I_individual_ignore_grid)
            thumb_add_shot(&tn, ip, thumbShadowIm, shadow_radius, idx, found_pts, o);

        gdImageDestroy(ip);
        ip = NULL;

      skip_shot:
        /* step */
        seek_target += tn.step_t;
        
        seek_evade = 0;
        evade_try = 0;
        prevshot_pts = found_pts;
        av_log(NULL, AV_LOG_VERBOSE, "found_pts bottom: %"PRId64"\n", found_pts);
    
      continue_cleanup: // cleaning up before continuing the loop
        prevfound_pts = found_pts;
        if (edge_ip)
        {
            gdImageDestroy(edge_ip);
            edge_ip = NULL;
        }
    }
    av_log(NULL, AV_LOG_VERBOSE, "  *** avg_evade_try: %.2f\n", avg_evade_try); // DEBUG

    sprite_flush(sprite, o);
    sprite_export_vtt(sprite);

    if (o->I_individual_ignore_grid)
    {
        return_code = 0;
        goto cleanup;
    }

  eof: ;
    /* crop if we dont get enough shots */
    int crop_needed = 0;
    const int created_rows = (int) ceil((double)idx / tn.column);
    int skipped_rows = tn.row - created_rows;
    if (skipped_rows == tn.row)
    {
        av_log(NULL, AV_LOG_ERROR, "  all rows're skipped?\n");
        goto cleanup;
    }
    if (skipped_rows)
    {
        int cropped_height = tn.img_height - skipped_rows*tn.shot_height_out;
        tn.img_height = cropped_height;
        tn.row = created_rows;
        crop_needed = 1;
    }

    if (created_rows == 1)
    {
        const int created_cols = idx;

        if (created_cols < tn.column)
        {
            int cropped_width = tn.img_width - (tn.column-created_cols)*tn.shot_width_out;
            tn.img_width = cropped_width;
            tn.column = created_cols;
            crop_needed = 1;
        }
    }

    if (crop_needed)
    {
        tn.out_ip = crop_image(tn.out_ip, tn.img_width, tn.img_height);

        av_log(NULL, AV_LOG_INFO, "  changing # of tiles to %dx%d because of skipped shots; total size: %dx%d\n", 
            tn.column,
            tn.row,
            tn.img_width,
            tn.img_height
        );
    }

    /* save output image */
    if (save_image(tn.out_ip, tn.out_filename.s, o) == 0)
        tn.out_saved = 1;
    else
        goto cleanup;

    int64_t tfinish = get_current_time();
    double diff_time = diff_time_sec(tstart, tfinish);
    // previous version reported # of decoded shots/s; now we report the # of final shots/s
    av_log(NULL, AV_LOG_INFO, "  %.2f s, %.2f shots/s; output: %s\n",
        diff_time, (tn.idx + 1) / diff_time, tn.out_filename.s);

    if (tn.tiles_nr == tn.row * tn.column)
        return_code = 0; // everything is fine
    else
        return_code = 1; // warning - some images are missing

  cleanup:
    if (ip)
        gdImageDestroy(ip);
    if (thumbShadowIm)
        gdImageDestroy(thumbShadowIm);
    if (tn.out_ip)
        gdImageDestroy(tn.out_ip);

    if (info_fp)
    {
        fclose(info_fp);
        if (!o->I_individual_ignore_grid && !tn.out_saved)
            delete_file(info_filename);
    }

    if (pSwsCtx)
        sws_freeContext(pSwsCtx); // do we need to do this?

    // Free the video frame
    if (rgb_buffer)
        av_free(rgb_buffer);
    if (pFrameRGB)
        av_free(pFrameRGB);
    if (pFrame)
        av_free(pFrame);

    // Close the codec
    if (pCodecCtx)
    {
        avcodec_close(pCodecCtx);
        avcodec_free_context(&pCodecCtx);
    }    

    // Close the video file
    if (pFormatCtx)
        avformat_close_input(&pFormatCtx);

    thumb_cleanup_dynamic(&tn);
    sprite_destroy(sprite);
    sb_destroy(&info_buf);
    sb_destroy(&individual_filename);
    free_conv_result(out_filename);
    free_conv_result(info_filename);

    av_log(NULL, AV_LOG_VERBOSE, "make_thumbnail: done\n");
    return return_code;
}

/* modified from glibc
*/
int myalphasort(const void *a, const void *b)
{
    return strcoll(*(const char **) a, *(const char **) b);
}

/* modified from glibc
*/
int myalphacasesort(const void *a, const void *b)
{
    return strcasecmp(*(const char **) a, *(const char **) b);
}

/*
return 1 if filename has one of the predefined extensions
*/
int check_extension(const char *filename)
{
    static const char *movie_ext[] =
    {
        "3g2", "3gp", "asf", "avi", "avs", "divx", "dsm", "evo", "flv", "h264",
        "m1v", "m2ts", "m2v", "m4v", "mj2", "mjpeg", "mjpg", "mkv", "moov",
        "mov", "mp4", "mpeg", "mpg", "mpv", "mts", "nut", "ogg", "ogm", "ogv", "qt",
        "rm",  "rmvb", "swf", "ts", "vob", "webm", "wmv", "xvid"
    };

    static const int nb_ext = sizeof(movie_ext) / sizeof(*movie_ext);
#if defined(_DEBUG) && !defined(NDEBUG)
    int i;
    for (i = 0; i < nb_ext-1; i++)
        assert(strcasecmp(movie_ext[i], movie_ext[i+1]) < 0);
#endif

    if (strstr(filename, "uTorrentPartFile"))
        return 0;
    const char *ext = strrchr(filename, '.');
    if (!ext)
        return 0;
    ext++;
    return bsearch(&ext, movie_ext, nb_ext, sizeof(*movie_ext), myalphacasesort) != NULL;
}

struct process_state
{
    struct options opt;
    int nb_file;
    int processed;
    int errors;
    int all_extensions;
};

static void process_dir_func(void *context, const tchar_t *path)
{
    struct process_state *ps = (struct process_state *) context;
    const char *converted_path = tchar_to_utf8(path);
    if (ps->all_extensions || check_extension(converted_path))
    {
        if (make_thumbnail(converted_path, &ps->opt, ++ps->nb_file))
            ps->errors++;
        ps->processed++;
    }
    free_conv_result(converted_path);
}

void process_files(struct process_state *ps, char *paths[], int count)
{
    int i;
    for (i = 0; i < count; i++)
    {
        rem_trailing_slash(paths[i]);
        tchar_t *path = utf8_to_tchar(paths[i]);
        if (is_dir(path))
        {
            ps->all_extensions = 0;
            scan_dir(path, process_dir_func, ps, ps->opt.d_depth);
        }
        else
        {
#ifdef _WIN32
            tchar_t *file = (tchar_t *) tbasename(path);
            if (_tcschr(file, _T('*')) || _tcschr(file, _T('?')))
            {
                const tchar_t *dir = path;
                if (file == path)
                    dir = _T(".");
                else
                    file[-1] = 0;
                ps->all_extensions = 1;
                scan_dir_pattern(dir, file, process_dir_func, ps, 0);
            }
            else
#endif
                make_thumbnail(paths[i], &ps->opt, ++ps->nb_file);
        }
        free_conv_result(path);
    }
}

/*
get command line arguments and expand wildcards in utf-8 in windows
caller needs to free argv[i]
return 0 if ok
*/
#if defined(_WIN32) && defined(_UNICODE)
int get_windows_argv(int *pargc, char ***pargv)
{
    const WCHAR *cmd_line = GetCommandLineW();
    int i, count;
    LPWSTR *result = CommandLineToArgvW(cmd_line, &count);
    if (!result)
    {
        *pargc = 0;
        *pargv = NULL;
        return -1;
    }
    *pargv = malloc(sizeof(char*) * count);
    for (i = 0; i < count; i++)
        (*pargv)[i] = wstr_to_utf8(result[i]);
    *pargc = count;
    LocalFree(result);
    return 0;
}
#endif

/**
 * @return 0- success, otherwise - failed
 */
int main(int argc, char *argv[])
{
    int return_code = -1;

    gb_argv0 = basename(argv[0]);
    setvbuf(stderr, NULL, _IONBF, 0); // turn off buffering in mingw

    gb_st_start = get_current_filetime(); // program start time
    srand(gb_st_start);

#if defined(_WIN32) && defined(_UNICODE)
    // get utf-8 argv in windows
    if (get_windows_argv(&argc, &argv))
    {
        av_log(NULL, AV_LOG_ERROR, "%s: cannot get command line arguments\n", gb_argv0);
        return -1;
    }
#endif

    // set locale
    char *locale = setlocale(LC_ALL, "");
    //av_log(NULL, AV_LOG_VERBOSE, "locale: %s\n", locale);

    /* get & check options */
    struct process_state ps;
    ps.nb_file = ps.processed = ps.errors = 0;
    init_options(&ps.opt);
    
    int start_index;
    if (parse_options(&ps.opt, argc, argv, &start_index))
        goto exit;

    /* lower priority */
    if (!ps.opt.n_normal)
    {
        // lower priority
#ifdef _WIN32
        SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS);
#else
        errno = 0;
        int nice_ret = nice(10); // mingw doesn't have nice??
        //setpriority (PRIO_PROCESS, 0, PRIO_MAX/2);
        if(nice_ret == -1 && errno != 0)
            av_log(NULL, AV_LOG_ERROR, "error setting process priority (nice=10)\n");
#endif
    }

    /* create output directory */
    if (ps.opt.O_outdir)
    {
        const tchar_t *tmp = utf8_to_tchar(ps.opt.O_outdir);
        if (!is_dir(tmp) && create_directory(tmp))
        {
            free_conv_result((tchar_t *) tmp);
#ifdef _WIN32
            // FIXME
            av_log(NULL, AV_LOG_ERROR, "\n%s: creating output directory '%s' failed\n", gb_argv0, ps.opt.O_outdir);
#else
            av_log(NULL, AV_LOG_ERROR, "\n%s: creating output directory '%s' failed: %s\n", gb_argv0, ps.opt.O_outdir, strerror(errno));
#endif
            goto exit;
        }
        free_conv_result((tchar_t *) tmp);
    }

    /* init */
#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(58, 9, 100)
    av_register_all();          // Register all formats and codecs
#endif
    if (ps.opt.q_quiet)
        av_log_set_level(AV_LOG_ERROR);
    else if (ps.opt.v_verbose)
        av_log_set_level(AV_LOG_VERBOSE);
    else
        av_log_set_level(AV_LOG_INFO);

    // display mtn+libraries versions for bug reporting
    char *ident = mtn_identification();
    av_log(NULL, AV_LOG_VERBOSE, "%s\n\n", ident);
    free(ident);
        
    //gdUseFontConfig(1); // set GD to use fontconfig patterns

    /* process movie files */
    V_DEBUG = ps.opt.V;
    process_files(&ps, argv + start_index, argc - start_index);

  exit:
    // clean up

#if defined(_WIN32) && defined(_UNICODE)
    while (argc) free(argv[--argc]);
#endif

    //av_log(NULL, AV_LOG_VERBOSE, "\n%s: total run time: %.2f s.\n", gb_argv0, difftime(time(NULL), gb_st_start));

    if (ps.opt.p_pause && !ps.opt.P_dontpause)
    {
        av_log(NULL, AV_LOG_ERROR, "\npausing... press Enter key to exit (see -P option)\n");
        fflush(stdout);
        fflush(stderr);
        getchar();
    }
    free_options(&ps.opt);
    return return_code;
}
