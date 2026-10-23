/*
 * berry_embed: LD_PRELOAD shim that puts Berry Browser's content_shell inside
 * a Cascades ForeignWindowControl.
 *
 * content_shell's qnx_screen Ozone backend makes its window a top-level
 * application window with a window group of its own. A ForeignWindowControl
 * only binds a child window that has joined the Cascades window group and
 * carries the control's windowId as its SCREEN_PROPERTY_ID_STRING. The
 * engine binary is prebuilt, so instead of rebuilding Chromium this library
 * interposes the few libscreen/libbps calls that decide that:
 *
 *   screen_create_window         -> screen_create_window_type(CHILD)
 *   screen_create_window_group   -> set ID_STRING, screen_join_window_group
 *   screen_set_window_property_* -> keep our ID_STRING and rotation
 *   screen_set_group_property_pv -> keyboard FOCUS stays with Cascades
 *   navigator_request_events     -> the Cascades process owns the Navigator
 *   screen_get_event_property_iv -> touch positions relative to the window
 *
 * Everything is driven by the environment the Cascades app sets:
 *   BERRY_EMBED_GROUP     window group of the Cascades main window (required)
 *   BERRY_EMBED_ID        the ForeignWindowControl's windowId (required)
 *   BERRY_EMBED_ROTATION  0/90/180/270 for the child window (optional)
 *   BERRY_EMBED_NAVIGATOR 1 lets the engine talk to the Navigator itself
 *   BERRY_EMBED_TOUCH     source, window or display: how taps are mapped
 * Without BERRY_EMBED_GROUP and BERRY_EMBED_ID every call passes through.
 */
#include <dlfcn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <bps/bps.h>
#include <screen/screen.h>

static screen_window_t g_window;

static const char* env(const char* name) {
  const char* v = getenv(name);
  return (v && v[0]) ? v : NULL;
}

static int embedding(void) {
  return env("BERRY_EMBED_GROUP") && env("BERRY_EMBED_ID");
}

static void trace(const char* fmt, const char* arg, int rc) {
  fprintf(stderr, "berry_embed: ");
  fprintf(stderr, fmt, arg, rc);
  fputc('\n', stderr);
}

static void* next_symbol(const char* name) {
  void* sym = dlsym(RTLD_NEXT, name);
  if (!sym)
    trace("no next symbol for %s (%d)", name, 0);
  return sym;
}

typedef int (*create_window_fn)(screen_window_t*, screen_context_t);
typedef int (*create_group_fn)(screen_window_t, const char*);
typedef int (*set_window_cv_fn)(screen_window_t, int, int, const char*);
typedef int (*set_window_iv_fn)(screen_window_t, int, const int*);
typedef int (*set_group_pv_fn)(screen_group_t, int, void**);
typedef int (*get_event_iv_fn)(screen_event_t, int, int*);
typedef int (*request_events_fn)(int);
typedef int (*keyboard_tracking_fn)(bool);

static int set_id_string(screen_window_t win) {
  static set_window_cv_fn real;
  const char* id = env("BERRY_EMBED_ID");
  if (!real)
    real = (set_window_cv_fn)next_symbol("screen_set_window_property_cv");
  if (!real)
    return -1;
  return real(win, SCREEN_PROPERTY_ID_STRING, (int)strlen(id), id);
}

int screen_create_window(screen_window_t* pwin, screen_context_t ctx) {
  static create_window_fn real;
  if (!embedding() || g_window) {
    if (!real)
      real = (create_window_fn)next_symbol("screen_create_window");
    return real ? real(pwin, ctx) : -1;
  }
  int rc = screen_create_window_type(pwin, ctx, SCREEN_CHILD_WINDOW);
  if (rc == 0)
    g_window = *pwin;
  trace("child window created (%s) rc=%d", env("BERRY_EMBED_ID"), rc);
  return rc;
}

int screen_create_window_group(screen_window_t win, const char* name) {
  static create_group_fn real;
  if (!embedding() || win != g_window) {
    if (!real)
      real = (create_group_fn)next_symbol("screen_create_window_group");
    return real ? real(win, name) : -1;
  }
  set_id_string(win);
  int rc = screen_join_window_group(win, env("BERRY_EMBED_GROUP"));
  trace("joined window group %s rc=%d", env("BERRY_EMBED_GROUP"), rc);
  return rc;
}

int screen_set_window_property_cv(screen_window_t win, int pname, int len,
                                  const char* value) {
  static set_window_cv_fn real;
  if (embedding() && win == g_window && pname == SCREEN_PROPERTY_ID_STRING)
    return set_id_string(win);
  if (!real)
    real = (set_window_cv_fn)next_symbol("screen_set_window_property_cv");
  return real ? real(win, pname, len, value) : -1;
}

int screen_set_window_property_iv(screen_window_t win, int pname,
                                  const int* value) {
  static set_window_iv_fn real;
  if (!real)
    real = (set_window_iv_fn)next_symbol("screen_set_window_property_iv");
  if (!real)
    return -1;
  if (embedding() && win == g_window && pname == SCREEN_PROPERTY_ROTATION &&
      env("BERRY_EMBED_ROTATION")) {
    int rotation = atoi(env("BERRY_EMBED_ROTATION"));
    return real(win, pname, &rotation);
  }
  return real(win, pname, value);
}

int screen_set_group_property_pv(screen_group_t group, int pname,
                                 void** value) {
  static set_group_pv_fn real;
  /* The group belongs to Cascades: taking its keyboard focus would starve
   * the address bar. Keys reach the page through DevTools instead. */
  if (embedding() && pname == SCREEN_PROPERTY_FOCUS)
    return 0;
  if (!real)
    real = (set_group_pv_fn)next_symbol("screen_set_group_property_pv");
  return real ? real(group, pname, value) : -1;
}

/* Touch positions. The engine maps SCREEN_PROPERTY_POSITION from window
 * pixels (QNX_SCREEN_OUTPUT_*) to its render size (QNX_SCREEN_*). As a child
 * window it may get them in another space, so BERRY_EMBED_TOUCH picks:
 *   source  (default) derive them from SOURCE_POSITION, the buffer position
 *   window  pass them through
 *   display subtract the child window's position */
static int env_int(const char* name, int fallback) {
  const char* v = env(name);
  int n = v ? atoi(v) : 0;
  return n > 0 ? n : fallback;
}

int screen_get_event_property_iv(screen_event_t ev, int pname, int* value) {
  static get_event_iv_fn real;
  if (!real)
    real = (get_event_iv_fn)next_symbol("screen_get_event_property_iv");
  if (!real)
    return -1;
  int rc = real(ev, pname, value);
  if (rc != 0 || pname != SCREEN_PROPERTY_POSITION || !g_window)
    return rc;
  screen_window_t win = NULL;
  if (screen_get_event_property_pv(ev, SCREEN_PROPERTY_WINDOW, (void**)&win) !=
          0 ||
      win != g_window)
    return rc;

  const char* mode = env("BERRY_EMBED_TOUCH");
  if (!mode || strcmp(mode, "source") == 0) {
    int src[2] = {0, 0};
    if (real(ev, SCREEN_PROPERTY_SOURCE_POSITION, src) == 0) {
      value[0] = src[0] * env_int("QNX_SCREEN_OUTPUT_WIDTH", 1) /
                 env_int("QNX_SCREEN_WIDTH", 1);
      value[1] = src[1] * env_int("QNX_SCREEN_OUTPUT_HEIGHT", 1) /
                 env_int("QNX_SCREEN_HEIGHT", 1);
    }
  } else if (strcmp(mode, "display") == 0) {
    int origin[2] = {0, 0};
    if (screen_get_window_property_iv(g_window, SCREEN_PROPERTY_POSITION,
                                      origin) == 0) {
      value[0] -= origin[0];
      value[1] -= origin[1];
    }
  }
  return rc;
}

static int navigator_shared(void) {
  const char* v = env("BERRY_EMBED_NAVIGATOR");
  return !embedding() || (v && v[0] == '1');
}

int navigator_request_events(int flags) {
  static request_events_fn real;
  if (!navigator_shared())
    return BPS_SUCCESS;
  if (!real)
    real = (request_events_fn)next_symbol("navigator_request_events");
  return real ? real(flags) : BPS_FAILURE;
}

int navigator_set_keyboard_tracking(bool track) {
  static keyboard_tracking_fn real;
  if (!navigator_shared())
    return BPS_SUCCESS;
  if (!real)
    real = (keyboard_tracking_fn)next_symbol("navigator_set_keyboard_tracking");
  return real ? real(track) : BPS_FAILURE;
}
