#pragma once
#include "lvgl.h"
extern const lv_image_dsc_t img_wx_clear_day;
extern const lv_image_dsc_t img_wx_clear_night;
extern const lv_image_dsc_t img_wx_cloudy;
extern const lv_image_dsc_t img_wx_partly_cloudy_day;
extern const lv_image_dsc_t img_wx_partly_cloudy_night;
extern const lv_image_dsc_t img_wx_rainy;
extern const lv_image_dsc_t img_wx_thunderstorm;
extern const lv_image_dsc_t img_wx_snow;
extern const lv_image_dsc_t img_wx_fog;
extern const lv_image_dsc_t img_wx_windy;

const lv_image_dsc_t *wx_icon_get_image_dsc(const char *slug);
