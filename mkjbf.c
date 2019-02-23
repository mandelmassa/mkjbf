/***************************************************************************
 *
 * Copyright (c) 2019 Mathias Thore
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 ***************************************************************************/
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <wand/magick_wand.h>

#define MKJBF       0
#define THUMB_SIZE  150
#define THUMB_Q     100

enum {
    SORT_NONE       = 0,
    SORT_NAME       = 1,
    SORT_GENERAL    = 2,
    SORT_DATE       = 3,
    SORT_FSIZE      = 4,
    SORT_WIDTH      = 5,
    SORT_HEIGHT     = 6,
    SORT_SIZE       = 7
};

enum {
    TYPE_NONE       = 0x00,
    TYPE_JPG        = 0x11,
    TYPE_PNG        = 0x1c,
    TYPE_PSD        = 0x1e,
};

typedef struct {
    int             status;

    struct {
        unsigned    width;
        unsigned    height;
        unsigned    pixels;
    }               image;

    struct {
        uint8_t    *blob;
        size_t      len;
    }               thumb;

    struct {
        off_t       size;
        time_t      mtime;
        int         type;
        char       *name;
        char        path[512];
    }               file;

} file_t;

static int          g_verbose  = 0;
static int          g_reverse  = 1;
static int          g_case     = 1;
static int          g_sort     = SORT_NAME;
static int          g_testmode = 0;
static int          g_thumb_sz = THUMB_SIZE;
static int          g_thumb_q  = THUMB_Q;

static void help(void)
{
    printf("mkjbf [-s 0/n/g/d/f/w/h/x | -z <size> | -q <quality> | -r | -c | -h] <path>\n"
           "\n"
           " -s <mode>         sort by\n"
           "     0              no sorting\n"
           "     n              file name\n"
           "     g              file name general numeric (default)\n"
           "     d              file date\n"
           "     f              file size\n"
           "     w              image width\n"
           "     h              image height\n"
           "     x              image size in pixels\n"
           " -r                reverse sort order\n"
           " -c                ignore case when sorting\n"
           " -z <size>         thumbnail size, default 150\n"
           " -q <quality>      thumbnail quality, default 100\n"
           " -h                show help\n"
           " <path>            working directory, default .\n");
}

static void testsort(void);
static void findfiles(char *path, file_t ***r_files, int *r_count);
static int  compare_function(const void *a, const void *b);
static void do_wand(file_t *file);
static void print_jbf(file_t **file, int count, char *path);

int main(int argc, char *argv[])
{
    int opt;
    char *indir = ".";
    int count;
    int i;
    file_t **files;

    /************************************************************************
     * Parse command line
     */

    while ((opt = getopt(argc, argv, "s:z:q:y:rvcth")) != -1) {
        switch (opt) {
        case 's':
            switch(optarg[0]) {
            case '0':
                g_sort = SORT_NONE;
                break;

            case 'n':
                g_sort = SORT_NAME;
                break;

            case 'g':
                g_sort = SORT_GENERAL;
                break;

            case 'd':
                g_sort = SORT_DATE;
                break;

            case 'f':
                g_sort = SORT_FSIZE;
                break;

            case 'w':
                g_sort = SORT_WIDTH;
                break;

            case 'h':
                g_sort = SORT_HEIGHT;
                break;

            case 'x':
                g_sort = SORT_SIZE;
                break;

            default:
                printf("error: unknown sort method %s\n", optarg);
                return -1;
            }
            break;

        case 'r':
            g_reverse = -1;
            break;

        case 'v':
            g_verbose = 1;
            break;

        case 'c':
            g_case = 0;
            break;

        case 't':
            g_testmode = 1;
            break;

        case 'z':
            g_thumb_sz = strtoul(optarg, NULL, 0);
            if (g_thumb_sz < 1) g_thumb_sz = 1;
            break;

        case 'q':
            g_thumb_q = strtoul(optarg, NULL, 0);
            if (g_thumb_q < 15)  g_thumb_q = 15;
            if (g_thumb_q > 100) g_thumb_q = 100;
            break;

        case 'h':
            help();
            return 0;
        }
    }

    if (g_testmode) {
        testsort();
        return 0;
    }

    if (optind < argc) {
        indir = argv[optind];
        while (indir[strlen(indir) - 1] == '/' && strlen(indir))
            indir[strlen(indir) - 1] = '\0';
    }

    /************************************************************************
     * Find images to thumbnail
     */

    findfiles(indir, &files, &count);
    if (count < 1) {
        printf("no images found\n");
        return 0;
    }

    /************************************************************************
     * Generate thumbnails and extract file information
     */

    printf("Processing %d images\n", count);
    MagickWandGenesis();
#pragma omp parallel for schedule(dynamic)
    for (i = 0; i < count; i++) {
        struct stat buffer;
        int fd;

        files[i]->status = -1;
        fd = open(files[i]->file.path, O_RDONLY);
        if (fd != -1) {
            files[i]->status = fstat(fd, &buffer);
            if (files[i]->status == 0) {
                files[i]->file.size = buffer.st_size;
                files[i]->file.mtime = buffer.st_mtime;
                do_wand(files[i]);
            }
            close(fd);
        }
    }
    MagickWandTerminus();

    /************************************************************************
     * Sort the files
     */

    if (g_sort != SORT_NONE) {
        qsort(files, count, sizeof(*files), compare_function);
    }

    /************************************************************************
     * Generate HTML
     */

    print_jbf(files, count, indir);

    free(files);
}

/***************************************************************************
 *
 * LIST FILES
 *
 ***************************************************************************/

static void findfiles(char *path, file_t ***r_files, int *r_count)
{
    DIR *dir;
    struct dirent *ent;
    int count = 0;
    file_t **files = NULL;
    size_t pathlen;
    int type;

    dir = opendir(path);
    if (dir == NULL) {
        printf("error: could not open directory\n");
        *r_count = 0;
        return;
    }

    pathlen = strlen(path);
    while ((ent = readdir(dir)) != NULL) {
        char *casename = strdup(ent->d_name);
        for (char *c = casename; *c; c++) {
            *c = tolower(*c);
        }
        type = TYPE_NONE;
        if (strstr(casename, ".jpg") != NULL ||
            strstr(casename, ".jpeg") != NULL ||
            strstr(casename, ".jpe") != NULL ||
            strstr(casename, ".jif") != NULL ||
            strstr(casename, ".jfif") != NULL ||
            strstr(casename, ".jfi") != NULL)
        {
            type = TYPE_JPG;
        }
        if (strstr(casename, ".png") != NULL) {
            type = TYPE_PNG;
        }
        /*
        if (strstr(casename, ".psd") != NULL) {
            type = TYPE_PSD;
        }
        */

        if (type != TYPE_NONE) {
            files = (file_t **) realloc(files, (count + 1) * sizeof(char *));
            files[count] = calloc(1, sizeof(*files[count]));
            sprintf(files[count]->file.path, "%s/%s", path, ent->d_name);
            files[count]->file.name = files[count]->file.path + pathlen + 1;
            files[count]->file.type = type;
            count++;
        }
        free(casename);
    }

    closedir(dir);

    *r_files = files;
    *r_count = count;
    return;
}


/***************************************************************************
 *
 * MAGICK WAND (create thumbnail image)
 *
 ***************************************************************************/

static void do_wand(file_t *file)
{
    MagickWand *wand = NULL;
    int width, height;
    double factor;

    wand = NewMagickWand();
    MagickReadImage(wand, file->file.path);

    if (MagickGetExceptionType(wand) != UndefinedException) {
        printf("error detected when opening file: %s\n", file->file.name);
        file->status = -1;
        goto bail;
    }
    width = MagickGetImageWidth(wand);
    height = MagickGetImageHeight(wand);
    file->image.width = width;
    file->image.height = height;
    file->image.pixels = width * height;

    if (width > g_thumb_sz || height > g_thumb_sz) {
        if (width > height) {
            factor = (double) g_thumb_sz / (double) width;
        }
        else {
            factor = (double) g_thumb_sz / (double) height;
        }

        width = floor(width * factor);
        height = floor(height * factor);
        if (width < 1) width = 1;
        if (height < 1) height = 1;

        MagickResizeImage(wand, width, height, LanczosFilter, 1);
    }

    MagickSetImageFormat(wand, "JPEG");
    MagickSetImageCompressionQuality(wand, g_thumb_q);
    file->thumb.blob = MagickGetImageBlob(wand, &file->thumb.len);

  bail:
    DestroyMagickWand(wand);
}

/***************************************************************************
 *
 * SORTING
 *
 ***************************************************************************/

static int general_strcmp(const char *s1, const char *s2)
{
    while (*s1 &&
           *s2 &&
           *s1 == *s2)
    {
        s1++;
        s2++;
    }

    if (*s1 == 0 && *s2 == 0) {
        return 0;
    }
    if (*s1 == 0 && *s2 !=  0) {
        return -1;
    }
    if (*s1 != 0 && *s2 == 0) {
        return 1;
    }

    while (*s1 == '0') s1++;
    while (*s2 == '0') s2++;
    if (isdigit(*s1) && isdigit(*s2)) {
        unsigned long anum = strtoul(s1, NULL, 0);
        unsigned long bnum = strtoul(s2, NULL, 0);

        if (anum > bnum) return 1;
        if (bnum > anum) return -1;
        return 0;
    }

    return *s1 - *s2;
}

static int compare_str(const void *a, const void *b)
{
    char *s1;
    char *s2;
    int ret = 0;

    s1 = strdup(*(char **) a);
    s2 = strdup(*(char **) b);

    if (g_case == 0) {
        for (char *c = s1; *c; c++) {
            *c = tolower(*c);
        }
        for (char *c = s2; *c; c++) {
            *c = tolower(*c);
        }
    }

    switch (g_sort) {
    case SORT_NAME:
        ret = strcmp(s1, s2);
        break;

    case SORT_GENERAL:
        ret = general_strcmp(s1, s2);
        break;
    }

    free(s1);
    free(s2);

    return ret;
}

static int compare_function(const void *a, const void *b)
{
    file_t *f1, *f2;
    int ret = 0;

    f1 = *(file_t **) a;
    f2 = *(file_t **) b;

    switch (g_sort) {
    case SORT_NAME:
    case SORT_GENERAL:
        ret = compare_str(&f1->file.name, &f2->file.name);
        break;

    case SORT_DATE:
        ret = f1->file.mtime - f2->file.mtime;
        break;

    case SORT_FSIZE:
        ret = f1->file.size - f2->file.size;
        break;

    case SORT_WIDTH:
        ret = f1->image.width - f2->image.width;
        break;

    case SORT_HEIGHT:
        ret = f1->image.height - f2->image.height;
        break;

    case SORT_SIZE:
        ret = f1->image.pixels - f2->image.pixels;
        break;
    }

    return ret * g_reverse;
}

static void testsort(void)
{
    char *testdata[6][5] ={ {"a.jpg", "b.jpg", "c.jpg", "d.jpg", "e.jpg"},
                            {"1.jpg", "2.jpg", "3.jpg", "4.jpg", "5.jpg"},
                            {"a100.jpg", "a99.jpg", "a98.jpg", "a101.jpg", "a102.jpg"},
                            {"a.jpg", "A.jpg", "b.jpg", "B.jpg", "c.jpg"},
                            {"a", "aaa", "aaaa", "aa", "a"},
                            {"a8_x.jpg", "a11_x.jpg", "a10_x.jpg", "a9_x.jpg", "a12_x.jpg"} };
    int i, j;

    for (i = 0; i < 6; i++) {
        qsort(testdata[i], 5, sizeof(char *), compare_str);
        for (j = 0; j < 5; j++) {
            printf("%s ", testdata[i][j]);
        }
        printf("\n");
    }
}

/***************************************************************************
 *
 * JBF
 *
 ***************************************************************************/

static void printentry(FILE *out, file_t *file);

struct filehdr {
    char          magic[0x10];  // NULL terminated string "JASC BROWS FILE"
    uint8_t       data1[3];     // Ignored for now
    uint32_t      count;        // Number of thumbnail objects in the file
    char          path[0xb3];   // place of generation (0-term, pad with 0x20)
    char          zero[0x4d];   // 0x0
    char          drive[0x20];  // pad with 0x0
    uint16_t      one;          // the short number 1
    uint8_t       data3[0x2c7]; // 0xff    
} __attribute__ ((packed));

struct entryhdr {
    uint64_t      filetime;     // Win32 FILETIME, last time the image was modified
    uint32_t      filetype;     // Image type, represented by the JbfFileTypeE enum
    uint32_t      width;        // Image width in pixels
    uint32_t      height;       // Image height in pixels
    uint32_t      bpp;          // Image bits per pixel. 1, 4, 8, or 24.
    uint32_t      bufsize;      // Seems to contain width*height*bpp, buffer size
                                // required to hold the uncompressed image data?
    uint32_t      filesize;     // Image file size
    uint32_t      data1[2];     // data1[0] = 2, unknown interpretation
                                // data1[1] = 1, #of planes?
    uint32_t      thumbmagic;   // 0xffffffff
    uint32_t      thumbsize;    // number of bytes of thumbnail data
} __attribute__ ((packed));

static void print_jbf(file_t **files, int count, char *path)
{
    int i;
    FILE *jbf;
    struct filehdr header;

    jbf = fopen("pspbrwse.jbf", "w");
    if (jbf == NULL) {
        printf("error: could not open output\n");
        return;
    }

    memset(&header, 0, 1024);
    sprintf(header.magic, "JASC BROWS FILE");
    header.data1[0] = 2;
    header.count = count;
    memset(header.path, 0x20, sizeof(header.path));

    // find actual windows path by calling cygpath
#ifdef __CYGWIN__
    {
        char command[1024];
        FILE *pf;

        sprintf(command, "cygpath -aw %s", path);

        pf = popen(command, "r");
        if (pf == NULL) {
            sprintf(header.path, "c:\\");
        }
        else {
            int t;
            int p;

            for (p = 0; p < sizeof(header.path); p++) {
                t = fgetc(pf);
                if (t == EOF ||
                    t == '\n' ||
                    t == '\0')
                {
                    break;
                }
                header.path[p] = t & 0xff;
            }
            header.path[p] = '\0';
        }
        fclose(pf);
    }
#else
    sprintf(header.path, "c:\\");
#endif
    sprintf(header.drive, "mkjbf-%d", MKJBF);
    header.one = 0x0001;
    memset(header.data3, 0xff, sizeof(header.data3));
    fwrite(&header, sizeof(header), 1, jbf);

    for (i = 0; i < count; i++) {
        if (files[i]->status) {
            continue;
        }
        printentry(jbf, files[i]);
        free(files[i]);
    }

    fclose(jbf);
}

#ifdef __CYGWIN__
#include <windows.h>
#endif

static void printentry(FILE *jbf, file_t *file)
{
    int namelength = strlen(file->file.name);
    struct entryhdr header;

    memset(&header, 0, sizeof(header));

    // write filename
    fwrite(&namelength, sizeof(namelength), 1, jbf);
    fwrite(file->file.name, 1, namelength, jbf);

    // mtime, get actual one on windows, or fake one otherwise
#ifdef __CYGWIN__
    {
        FILETIME CreationTime;
        FILETIME LastAccessTime;
        FILETIME LastWriteTime;
        HANDLE File;

        File = CreateFile(file->file.path, GENERIC_READ, FILE_SHARE_READ, NULL,
                          OPEN_EXISTING, 0, NULL);
        GetFileTime(File, &CreationTime, &LastAccessTime, &LastWriteTime);
        CloseHandle(File);
        header.filetime   = LastWriteTime.dwHighDateTime;
        header.filetime <<= 32;
        header.filetime  |= LastWriteTime.dwLowDateTime;
    }
#else
    header.filetime   = file->file.mtime;
    header.filetime  *= 10000000;
    header.filetime  += 116444736000000000;
#endif

    // prepare rest of header
    header.filetype   = file->file.type;
    header.width      = file->image.width;
    header.height     = file->image.height;
    header.bpp        = 24;
    header.bufsize    = header.width * header.height * 3;
    header.filesize   = file->file.size;
    header.data1[0]   = 2;
    header.data1[1]   = 1;
    header.thumbmagic = 0xffffffff;
    header.thumbsize  = file->thumb.len;

    // write header
    fwrite(&header, sizeof(header), 1, jbf);

    // write thumbnail
    fwrite(file->thumb.blob, file->thumb.len, 1, jbf);

    free(file->thumb.blob);
    file->thumb.len = 0;
}
