/* nuklear - v1.00 - public domain */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <math.h>
#include <time.h>
#include <limits.h>

#include <GL/glx.h>
#include <GL/glxext.h>

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#define NK_XLIB_GL2_IMPLEMENTATION
#include "../../nuklear.h"
#include "nuklear_xlib_gl2.h"

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600

#define MAX_VERTEX_BUFFER 512 * 1024
#define MAX_ELEMENT_BUFFER 128 * 1024

#define UNUSED(a) (void)a
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) < (b) ? (b) : (a))
#define LEN(a) (sizeof(a)/sizeof(a)[0])

/* ===============================================================
 *
 *                          EXAMPLE
 *
 * ===============================================================*/
/* This are some code examples to provide a small overview of what can be
 * done with this library. To try out an example uncomment the include
 * and the corresponding function. */
/*#include "../style.c"*/
/*#include "../calculator.c"*/
/*#include "../overview.c"*/
/*#include "../node_editor.c"*/

/* ===============================================================
 *
 *                          DEMO
 *
 * ===============================================================*/
struct XWindow {
    Display *dpy;
    Window win;
    XVisualInfo *vis;
    Colormap cmap;
    XSetWindowAttributes swa;
    XWindowAttributes attr;
    GLXFBConfig fbc;
    int width, height;
};
static int gl_err = FALSE;
static int gl_error_handler(Display *dpy, XErrorEvent *ev)
{UNUSED((dpy, ev)); gl_err = TRUE;return 0;}

static void
die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputs("\n", stderr);
    exit(EXIT_FAILURE);
}

static int
has_extension(const char *string, const char *ext)
{
    const char *start, *where, *term;
    where = strchr(ext, ' ');
    if (where || *ext == '\0')
        return FALSE;

    for (start = string;;) {
        where = strstr((const char*)start, ext);
        if (!where) break;
        term = where + strlen(ext);
        if (where == start || *(where - 1) == ' ') {
            if (*term == ' ' || *term == '\0')
                return TRUE;
        }
        start = term;
    }
    return FALSE;
}

int main(int argc, char **argv)
{
    /* Platform */
    int running = 1;
    struct XWindow win;
    GLXContext glContext;
    struct nk_context *ctx;
    struct nk_color background;

    memset(&win, 0, sizeof(win));
    win.dpy = XOpenDisplay(NULL);
    if (!win.dpy) die("Failed to open X display\n");
    {
        /* check glx version */
        int glx_major, glx_minor;
        if (!glXQueryVersion(win.dpy, &glx_major, &glx_minor))
            die("[X11]: Error: Failed to query OpenGL version\n");
        if ((glx_major == 1 && glx_minor < 3) || (glx_major < 1))
            die("[X11]: Error: Invalid GLX version!\n");
    }
    {
        /* find and pick matching framebuffer visual */
        int fb_count;
        static GLint attr[] = {
            GLX_X_RENDERABLE,   True,
            GLX_DRAWABLE_TYPE,  GLX_WINDOW_BIT,
            GLX_RENDER_TYPE,    GLX_RGBA_BIT,
            GLX_X_VISUAL_TYPE,  GLX_TRUE_COLOR,
            GLX_RED_SIZE,       8,
            GLX_GREEN_SIZE,     8,
            GLX_BLUE_SIZE,      8,
            GLX_ALPHA_SIZE,     8,
            GLX_DEPTH_SIZE,     24,
            GLX_STENCIL_SIZE,   8,
            GLX_DOUBLEBUFFER,   True,
            None
        };
        GLXFBConfig *fbc;
        fbc = glXChooseFBConfig(win.dpy, DefaultScreen(win.dpy), attr, &fb_count);
        if (!fbc) die("[X11]: Error: failed to retrieve framebuffer configuration\n");
        {
            /* pick framebuffer with most samples per pixel */
            int i;
            int fb_best = -1, best_num_samples = -1;
            for (i = 0; i < fb_count; ++i) {
                XVisualInfo *vi = glXGetVisualFromFBConfig(win.dpy, fbc[i]);
                if (vi) {
                    int sample_buffer, samples;
                    glXGetFBConfigAttrib(win.dpy, fbc[i], GLX_SAMPLE_BUFFERS, &sample_buffer);
                    glXGetFBConfigAttrib(win.dpy, fbc[i], GLX_SAMPLES, &samples);
                    if ((fb_best < 0) || (sample_buffer && samples > best_num_samples))
                        fb_best = i; best_num_samples = samples;
                }
            }
            win.fbc = fbc[fb_best];
            XFree(fbc);
            win.vis = glXGetVisualFromFBConfig(win.dpy, win.fbc);
        }
    }
    {
        /* create window */
        win.cmap = XCreateColormap(win.dpy, RootWindow(win.dpy, win.vis->screen), win.vis->visual, AllocNone);
        win.swa.colormap =  win.cmap;
        win.swa.background_pixmap = None;
        win.swa.border_pixel = 0;
        win.swa.event_mask =
            ExposureMask | KeyPressMask | KeyReleaseMask |
            ButtonPress | ButtonReleaseMask| ButtonMotionMask |
            Button1MotionMask | Button3MotionMask | Button4MotionMask | Button5MotionMask|
            PointerMotionMask| StructureNotifyMask;
        win.win = XCreateWindow(win.dpy, RootWindow(win.dpy, win.vis->screen), 0, 0,
            WINDOW_WIDTH, WINDOW_HEIGHT, 0, win.vis->depth, InputOutput,
            win.vis->visual, CWBorderPixel|CWColormap|CWEventMask, &win.swa);
        if (!win.win) die("[X11]: Failed to create window\n");
        XFree(win.vis);
        XStoreName(win.dpy, win.win, "Demo");
        XMapWindow(win.dpy, win.win);
    }
    {
        /* create opengl context */
        typedef GLXContext(*glxCreateContext)(Display*, GLXFBConfig, GLXContext, Bool, const int*);
        int(*old_handler)(Display*, XErrorEvent*) = XSetErrorHandler(gl_error_handler);
        const char *extensions_str = glXQueryExtensionsString(win.dpy, DefaultScreen(win.dpy));
        glxCreateContext create_context = (glxCreateContext)
            glXGetProcAddressARB((const GLubyte*)"glXCreateContextAttribsARB");

        gl_err = FALSE;
        if (!has_extension(extensions_str, "GLX_ARB_create_context") || !create_context) {
            fprintf(stdout, "[X11]: glXCreateContextAttribARB() not found...\n");
            fprintf(stdout, "[X11]: ... using old-style GLX context\n");
            glContext = glXCreateNewContext(win.dpy, win.fbc, GLX_RGBA_TYPE, 0, True);
        } else {
            GLint attr[] = {
                GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
                GLX_CONTEXT_MINOR_VERSION_ARB, 0,
                None
            };
            glContext = create_context(win.dpy, win.fbc, 0, True, attr);
            XSync(win.dpy, False);
            if (gl_err || !glContext) {
                /* Could not create GL 3.0 context. Fallback to old 2.x context.
                 * If a version below 3.0 is requested, implementations will
                 * return the newest context version compatible with OpenGL
                 * version less than version 3.0.*/
                attr[1] = 1; attr[3] = 0;
                gl_err = FALSE;
                fprintf(stdout, "[X11] Failed to create OpenGL 3.0 context\n");
                fprintf(stdout, "[X11] ... using old-style GLX context!\n");
                glContext = create_context(win.dpy, win.fbc, 0, True, attr);
            }
        }
        XSync(win.dpy, False);
        XSetErrorHandler(old_handler);
        if (gl_err || !glContext)
            die("[X11]: Failed to create an OpenGL context\n");
        glXMakeCurrent(win.dpy, win.win, glContext);
    }

    ctx = nk_x11_init(win.dpy, win.win);
    /* Load Fonts: if none of these are loaded a default font will be used  */
    {struct nk_font_atlas *atlas;
    nk_x11_font_stash_begin(&atlas);
    /*struct nk_font *droid = nk_font_atlas_add_from_file(atlas, "../../../extra_font/DroidSans.ttf", 14, 0);*/
    /*struct nk_font *roboto = nk_font_atlas_add_from_file(atlas, "../../../extra_font/Roboto-Regular.ttf", 14, 0);*/
    /*struct nk_font *future = nk_font_atlas_add_from_file(atlas, "../../../extra_font/kenvector_future_thin.ttf", 13, 0);*/
    /*struct nk_font *clean = nk_font_atlas_add_from_file(atlas, "../../../extra_font/ProggyClean.ttf", 12, 0);*/
    /*struct nk_font *tiny = nk_font_atlas_add_from_file(atlas, "../../../extra_font/ProggyTiny.ttf", 10, 0);*/
    /*struct nk_font *cousine = nk_font_atlas_add_from_file(atlas, "../../../extra_font/Cousine-Regular.ttf", 13, 0);*/
    nk_x11_font_stash_end();
    /*nk_style_set_font(ctx, &droid->handle);*/}

    /* style.c */
    /*set_style(ctx, THEME_WHITE);*/
    /*set_style(ctx, THEME_RED);*/
    /*set_style(ctx, THEME_BLUE);*/
    /*set_style(ctx, THEME_DARK);*/

    background = nk_rgb(28,48,62);
    while (running)
    {
        /* Input */
        XEvent evt;
        nk_input_begin(ctx);
        while (XCheckWindowEvent(win.dpy, win.win, win.swa.event_mask, &evt)){
            if (XFilterEvent(&evt, win.win)) continue;
            nk_x11_handle_event(&evt);
        }
        nk_input_end(ctx);

        /* GUI */
        {struct nk_panel layout;
        if (nk_begin(ctx, &layout, "Demo", nk_rect(50, 50, 200, 200),
            NK_WINDOW_BORDER|NK_WINDOW_MOVABLE|NK_WINDOW_SCALABLE|
            NK_WINDOW_CLOSABLE|NK_WINDOW_MINIMIZABLE|NK_WINDOW_TITLE))
        {
            enum {EASY, HARD};
            static int op = EASY;
            static int property = 20;

            nk_layout_row_static(ctx, 30, 80, 1);
            if (nk_button_label(ctx, "button", NK_BUTTON_DEFAULT))
                fprintf(stdout, "button pressed\n");
            nk_layout_row_dynamic(ctx, 30, 2);
            if (nk_option_label(ctx, "easy", op == EASY)) op = EASY;
            if (nk_option_label(ctx, "hard", op == HARD)) op = HARD;
            nk_layout_row_dynamic(ctx, 25, 1);
            nk_property_int(ctx, "Compression:", 0, &property, 100, 10, 1);

            {struct nk_panel combo;
            nk_layout_row_dynamic(ctx, 20, 1);
            nk_label(ctx, "background:", NK_TEXT_LEFT);
            nk_layout_row_dynamic(ctx, 25, 1);
            if (nk_combo_begin_color(ctx, &combo, background, 400)) {
                nk_layout_row_dynamic(ctx, 120, 1);
                background = nk_color_picker(ctx, background, NK_RGBA);
                nk_layout_row_dynamic(ctx, 25, 1);
                background.r = (nk_byte)nk_propertyi(ctx, "#R:", 0, background.r, 255, 1,1);
                background.g = (nk_byte)nk_propertyi(ctx, "#G:", 0, background.g, 255, 1,1);
                background.b = (nk_byte)nk_propertyi(ctx, "#B:", 0, background.b, 255, 1,1);
                background.a = (nk_byte)nk_propertyi(ctx, "#A:", 0, background.a, 255, 1,1);
                nk_combo_end(ctx);
            }}
        }
        nk_end(ctx);}
        if (nk_window_is_closed(ctx, "Demo")) break;

        /* -------------- EXAMPLES ---------------- */
        /*calculator(ctx);*/
        /*overview(ctx);*/
        /*node_editor(ctx);*/
        /* ----------------------------------------- */

        /* Draw */
        {float bg[4];
        nk_color_fv(bg, background);
        XGetWindowAttributes(win.dpy, win.win, &win.attr);
        glViewport(0, 0, win.width, win.height);
        glClear(GL_COLOR_BUFFER_BIT);
        glClearColor(bg[0], bg[1], bg[2], bg[3]);
        /* IMPORTANT: `nk_x11_render` modifies some global OpenGL state
         * with blending, scissor, face culling, depth test and viewport and
         * defaults everything back into a default state.
         * Make sure to either a.) save and restore or b.) reset your own state after
         * rendering the UI. */
        nk_x11_render(NK_ANTI_ALIASING_ON, MAX_VERTEX_BUFFER, MAX_ELEMENT_BUFFER);
        glXSwapBuffers(win.dpy, win.win);}
    }

    nk_x11_shutdown();
    glXMakeCurrent(win.dpy, 0, 0);
    glXDestroyContext(win.dpy, glContext);
    XUnmapWindow(win.dpy, win.win);
    XFreeColormap(win.dpy, win.cmap);
    XDestroyWindow(win.dpy, win.win);
    XCloseDisplay(win.dpy);
    return 0;

}
