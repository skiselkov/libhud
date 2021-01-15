/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2021 Saso Kiselkov. All rights reserved.
 */

#ifndef	_LIBHUD_H_
#define	_LIBHUD_H_

#include <stdbool.h>

#include <acfutils/mt_cairo_render.h>
#include <librain.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hud_s hud_t;

hud_t *hud_new(const char *shader_dir, mt_cairo_render_t *mtcr,
    double glass_opacity, obj8_t *glass, const char *glass_group_id,
    obj8_t *proj, const char *proj_group_id);
void hud_destroy(hud_t *hud);

void hud_set_enabled(hud_t *hud, bool flag);
bool hud_get_enabled(const hud_t *hud);

void hud_set_brightness(hud_t *hud, float brt);
float hud_get_brightness(const hud_t *hud);

void hud_set_glow(hud_t *hud, bool flag);
bool hud_get_glow(const hud_t *hud);

void hud_set_monochrome(hud_t *hud, vect3_t color);
vect3_t hud_get_monochrome(const hud_t *hud);

void hud_set_mtcr(hud_t *hud, mt_cairo_render_t *mtcr);
mt_cairo_render_t *hud_get_mtcr(const hud_t *hud);

void hud_render_eye(hud_t *hud, const mat4 pvm, const vec4 vp);

#ifdef __cplusplus
}
#endif

#endif	/* _LIBHUD_H_ */
