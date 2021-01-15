/*
 * CONFIDENTIAL
 *
 * Copyright 2021 Saso Kiselkov. All rights reserved.
 *
 * NOTICE:  All information contained herein is, and remains the property
 * of Saso Kiselkov. The intellectual and technical concepts contained
 * herein are proprietary to Saso Kiselkov and may be covered by U.S. and
 * Foreign Patents, patents in process, and are protected by trade secret
 * or copyright law. Dissemination of this information or reproduction of
 * this material is strictly forbidden unless prior written permission is
 * obtained from Saso Kiselkov.
 */

#version 460

layout(location = 10) uniform sampler2D	surf_tex;
layout(location = 11) uniform vec2	surf_sz;
layout(location = 12) uniform sampler2D	stencil_tex;
layout(location = 13) uniform vec2	stencil_sz;
layout(location = 14) uniform vec4	vp;
layout(location = 15) uniform float	brt;

#if	MONOCHROME
layout(location = 16) uniform vec3	beam_color;
#endif

layout(location = 0) in vec2		tex_coord;

layout(location = 0) out vec4		color_out;

#define GAUSS_SIZE 5
const float gauss_kernel[25] = float[25](
    0.01, 0.02, 0.04, 0.02, 0.01,
    0.02, 0.04, 0.08, 0.04, 0.02,
    0.04, 0.08, 0.16, 0.08, 0.04,
    0.02, 0.04, 0.08, 0.04, 0.02,
    0.01, 0.02, 0.04, 0.02, 0.01
);

#define BLUR_I(x, y, _row, _col) \
	out_pixel += texture(surf_tex, tex_coord + vec2(x, y) / surf_sz) * \
	    gauss_kernel[(_row) * GAUSS_SIZE + (_col)]

void
main(void)
{
	vec4 stencil_pix = texture(stencil_tex,
	    (gl_FragCoord.xy - vp.xy) / stencil_sz);
#if	GLOW
	vec4 out_pixel = vec4(0.0);
	/* row 0 */
	BLUR_I(-2, -2, 0, 0);
	BLUR_I(-1, -2, 0, 1);
	BLUR_I(0, -2, 0, 2);
	BLUR_I(1, -2, 0, 3);
	BLUR_I(2, -2, 0, 4);
	/* row 1 */
	BLUR_I(-2, -1, 1, 0);
	BLUR_I(-1, -1, 1, 1);
	BLUR_I(0, -1, 1, 2);
	BLUR_I(1, -1, 1, 3);
	BLUR_I(2, -1, 1, 4);
	/* row 2 */
	BLUR_I(-2, 0, 2, 0);
	BLUR_I(-1, 0, 2, 1);
	BLUR_I(0, 0, 2, 2);;
	BLUR_I(1, 0, 2, 3);
	BLUR_I(2, 0, 2, 4);
	/* row 3 */
	BLUR_I(-2, 1, 3, 0);
	BLUR_I(-1, 1, 3, 1);
	BLUR_I(0, 1, 3, 2);
	BLUR_I(1, 1, 3, 3);
	BLUR_I(2, 1, 3, 4);
	/* row 4 */
	BLUR_I(-2, 2, 4, 0);
	BLUR_I(-1, 2, 4, 1);
	BLUR_I(0, 2, 4, 2);
	BLUR_I(1, 2, 4, 3);
	BLUR_I(2, 2, 4, 4);
#else	/* !GLOW */
	vec4 out_pixel = texture(surf_tex, tex_coord);
#endif	/* !GLOW */
#if	MONOCHROME
	color_out = vec4(beam_color, out_pixel.r * stencil_pix.r * brt);
#else	/* !MONOCHROME */
	out_pixel.a *= brt;
	/*
	 * If the alpha channel sums to less than 1.0, that means we need
	 * to boost pixel brightness to avoid black borders around the pixel.
	 */
	color_out = vec4(out_pixel.rgb / max(out_pixel.a, 0.01),
	    out_pixel.a * stencil_pix.r);
#endif	/* !MONOCHROME */
}
