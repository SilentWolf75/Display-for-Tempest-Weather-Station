"""Run the production icon state transitions with a checked LVGL test double."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def function(text, signature):
    start = text.index(signature)
    opening = text.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]

icons = (ROOT / "firmware/main/ui/wx_icons.c").read_text(encoding="utf-8")
ui = (ROOT / "firmware/main/ui/ui.c").read_text(encoding="utf-8")
ctx = icons[icons.index("typedef struct {"):icons.index("} icon_ctx_t;") + len("} icon_ctx_t;")]

harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define LV_OBJ_FLAG_HIDDEN 1
typedef struct { void *var; void (*exec_cb)(void *, int32_t); int end_value, current_value; } lv_anim_t;
typedef struct { void *user_data; bool hidden, alive; int frame, loads; lv_anim_t anim; } lv_obj_t;
static void render(void *v, int32_t frame) { ((lv_obj_t *)v)->frame = frame; }
static void *lv_obj_get_user_data(lv_obj_t *o) { return o->user_data; }
static lv_anim_t *lv_lottie_get_anim(lv_obj_t *o) { assert(o->alive); return &o->anim; }
static void lv_anim_delete(lv_obj_t *o, void *cb) { (void)cb; o->alive = false; }
static void lv_obj_add_flag(lv_obj_t *o, int f) { o->hidden = true; }
static void lv_obj_clear_flag(lv_obj_t *o, int f) { o->hidden = false; }
static void lv_anim_set_exec_cb(lv_anim_t *a, void (*cb)(void *, int32_t)) { a->exec_cb = cb; }
static void lv_lottie_set_src_data(lv_obj_t *o, const void *src, size_t len) {
    assert(o->alive); /* LVGL stores this pointer and writes it on every source change. */
    o->loads++; o->anim.end_value = 120; o->frame = 0;
}
static char *load_json(const char *slug, size_t *len) {
    char *p = malloc(3); assert(p); memcpy(p, "{}", 3); *len = 2; return p;
}
"""
harness += ctx + "\n"
harness += function(icons, "bool wx_icon_set(") + "\n"
harness += function(icons, "void wx_icon_set_paused(") + "\n"
harness += function(ui, "static void slug_for_time_of_day(") + "\n"
harness += r"""
int main(void) {
    icon_ctx_t c = {.animate = true};
    lv_obj_t o = {.user_data = &c, .alive = true};
    o.anim.var = &o; o.anim.exec_cb = render;
    assert(wx_icon_set(&o, "partly-cloudy-night"));
    for (int i = 0; i < 20; i++) {
        wx_icon_set_paused(&o, true);
        assert(o.alive && o.hidden);
        assert(wx_icon_set(&o, i % 2 ? "partly-cloudy-night" : "partly-cloudy-day"));
        wx_icon_set_paused(&o, false);
        assert(o.alive && !o.hidden && o.anim.exec_cb);
        o.anim.exec_cb(o.anim.var, 42);
        assert(o.frame == 42);
    }
    char slug[40];
    slug_for_time_of_day("partly-cloudy-night", false, slug, sizeof(slug));
    assert(!strcmp(slug, "partly-cloudy-day"));
    assert(wx_icon_set(&o, slug));
    int loads = o.loads;
    assert(wx_icon_set(&o, slug) && o.loads == loads);
    slug_for_time_of_day("partly-cloudy-day", true, slug, sizeof(slug));
    assert(!strcmp(slug, "partly-cloudy-night"));
    slug_for_time_of_day("rainy", false, slug, sizeof(slug));
    assert(!strcmp(slug, "rainy"));
    free(c.json);
    c.json = NULL; c.slug[0] = 0; c.animate = false; c.render_frame = render;
    assert(wx_icon_set(&o, "clear-day") && o.frame == 119 && o.alive);
    assert(!o.anim.exec_cb);
    assert(wx_icon_set(&o, "clear-night") && o.frame == 119 && o.alive);
    free(c.json);
    puts("PASS icon lifetime, 20 pause/resume cycles, source changes, static frames and day/night variants");
}
"""
with tempfile.TemporaryDirectory(prefix="tempest-icons-") as tmp:
    src = Path(tmp) / "test.c"
    exe = Path(tmp) / ("test.exe" if os.name == "nt" else "test")
    src.write_text(harness, encoding="utf-8")
    if os.name == "nt":
        cc = os.environ.get("CC") or shutil.which("cl")
        if not cc:
            raise SystemExit("Run from a Visual Studio developer shell (cl is required).")
        subprocess.run([cc, "/nologo", "/std:c11", "/D_CRT_SECURE_NO_WARNINGS", str(src),
                        "/Fe:" + str(exe), "/Fo:" + str(Path(tmp) / "test.obj")], check=True)
    else:
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
