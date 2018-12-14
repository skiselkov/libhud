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
 * Copyright 2018 Saso Kiselkov. All rights reserved.
 */

#ifndef	_LIBHUD_H_
#define	_LIBHUD_H_

#include <acfutils/mt_cairo_render.h>
#include <librain.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hud_s hud_t;

hud_t *hud_new(mt_cairo_render_t *mtcr, double glass_opacity,
    obj8_t *glass, const char *glass_group_id,
    obj8_t *proj, const char *proj_group_id);
void hud_destroy(hud_t *hud);

void hud_render(hud_t *hud);

#ifdef __cplusplus
}
#endif

#endif	/* _LIBHUD_H_ */
