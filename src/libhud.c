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
 * Copyright 2019 Saso Kiselkov. All rights reserved.
 */


#include <XPLMDisplay.h>
#include <XPLMGraphics.h>

#include <acfutils/glew.h>
#include <acfutils/glutils.h>
#include <acfutils/helpers.h>
#include <acfutils/safe_alloc.h>
#include <acfutils/shader.h>

#include "libhud.h"

TEXSZ_MK_TOKEN(hud_glass_tex);

static const char *vert_shader =
    "#version 120\n"
    "uniform   mat4 pvm;\n"
    "attribute vec3 vtx_pos;\n"
    "attribute vec3 vtx_norm;\n"
    "attribute vec2 vtx_tex0;\n"
    "varying   vec2 tex_coord;\n"
    "void main() {\n"
    "   tex_coord = vtx_tex0;\n"
    "   gl_Position = pvm * vec4(vtx_pos, 1.0);\n"
    "}\n";

static const char *stencil_shader =
    "#version 120\n"
    "void main() {\n"
    "   gl_FragColor = vec4(1.0);\n"
    "}\n";

static const char *glass_shader =
    "#version 120\n"
    "uniform float opacity;"
    "void main() {\n"
    "   gl_FragColor = vec4(0.0, 0.0, 0.0, opacity);\n"
    "}\n";

static const char *proj_shader =
    "#version 120\n"
    "uniform sampler2D surf_tex;\n"
    "uniform vec2      surf_sz;\n"
    "uniform sampler2D stencil_tex;\n"
    "uniform vec2      stencil_sz;\n"
    "uniform vec4      vp;\n"
    "varying vec2      tex_coord;\n"
    "#define GAUSS_SIZE 5\n"
    "#define GAUSS_KERNEL float[25]( "
    "    0.01, 0.02, 0.04, 0.02, 0.01, "
    "    0.02, 0.04, 0.08, 0.04, 0.02, "
    "    0.04, 0.08, 0.16, 0.08, 0.04, "
    "    0.02, 0.04, 0.08, 0.04, 0.02, "
    "    0.01, 0.02, 0.04, 0.02, 0.01 "
    ")\n"
    "#define BLUR_I(x, y, row, col) "
    "    out_pixel += texture2D(surf_tex, tex_coord + vec2(x, y) / surf_sz) * "
    "    GAUSS_KERNEL[row * GAUSS_SIZE + col]\n"
    "void main() {\n"
    "    vec4 out_pixel = vec4(0.0);\n"
    "    vec4 stencil_pix = texture2D(stencil_tex,\n"
    "        (gl_FragCoord.xy - vp.xy) / stencil_sz);\n"
    "    if (stencil_pix.r == 0.0)\n"
    "        discard;\n"
    /* row 0 */
    "    BLUR_I(-2, -2, 0, 0);\n"
    "    BLUR_I(-1, -2, 0, 1);\n"
    "    BLUR_I(0, -2, 0, 2);\n"
    "    BLUR_I(1, -2, 0, 3);\n"
    "    BLUR_I(2, -2, 0, 4);\n"
    /* row 1 */
    "    BLUR_I(-2, -1, 1, 0);\n"
    "    BLUR_I(-1, -1, 1, 1);\n"
    "    BLUR_I(0, -1, 1, 2);\n"
    "    BLUR_I(1, -1, 1, 3);\n"
    "    BLUR_I(2, -1, 1, 4);\n"
    /* row 2 */
    "    BLUR_I(-2, 0, 2, 0);\n"
    "    BLUR_I(-1, 0, 2, 1);\n"
    "    BLUR_I(0, 0, 2, 2);\n"
    "    BLUR_I(1, 0, 2, 3);\n"
    "    BLUR_I(2, 0, 2, 4);\n"
    /* row 3 */
    "    BLUR_I(-2, 1, 3, 0);\n"
    "    BLUR_I(-1, 1, 3, 1);\n"
    "    BLUR_I(0, 1, 3, 2);\n"
    "    BLUR_I(1, 1, 3, 3);\n"
    "    BLUR_I(2, 1, 3, 4);\n"
    /* row 4 */
    "    BLUR_I(-2, 2, 4, 0);\n"
    "    BLUR_I(-1, 2, 4, 1);\n"
    "    BLUR_I(0, 2, 4, 2);\n"
    "    BLUR_I(1, 2, 4, 3);\n"
    "    BLUR_I(2, 2, 4, 4);\n"
	/*
	 * If the alpha channel sums to less than 1.0, that means we need
	 * to boost pixel brightness to avoid black borders around the pixel.
	 */
    "    gl_FragColor = vec4(out_pixel.rgb / out_pixel.a, out_pixel.a);\n"
    "}\n";

struct hud_s {
	mt_cairo_render_t	*mtcr;

	GLuint			stencil_shader;
	GLuint			glass_shader;
	GLuint			proj_shader;

	GLuint			stencil_fbo;
	GLuint			stencil_tex;
	int			stencil_w;
	int			stencil_h;

	double			glass_opacity;
	obj8_t			*glass;
	const char		*glass_group;
	obj8_t			*proj;
	const char		*proj_group;
};

hud_t *
hud_new(mt_cairo_render_t *mtcr, double glass_opacity,
    obj8_t *glass, const char *glass_group_id,
    obj8_t *proj, const char *proj_group_id)
{
	hud_t *hud = safe_calloc(1, sizeof (*hud));

	ASSERT(mtcr != NULL);
	ASSERT(glass != NULL);
	ASSERT(proj != NULL);

	hud->mtcr = mtcr;
	hud->glass_shader = shader_prog_from_text("libhud_glass_shader",
	    vert_shader, glass_shader, "vtx_pos", VTX_ATTRIB_POS,
	    "vtx_tex0", VTX_ATTRIB_TEX0, NULL);
	VERIFY(hud->glass_shader != 0);
	hud->stencil_shader = shader_prog_from_text("libhud_stencil_shader",
	    vert_shader, stencil_shader, "vtx_pos", VTX_ATTRIB_POS,
	    "vtx_tex0", VTX_ATTRIB_TEX0, NULL);
	VERIFY(hud->stencil_shader != 0);
	hud->proj_shader = shader_prog_from_text("libhud_proj_shader",
	    vert_shader, proj_shader, "vtx_pos", VTX_ATTRIB_POS,
	    "vtx_tex0", VTX_ATTRIB_TEX0, NULL);
	VERIFY(hud->proj_shader != 0);

	hud->glass_opacity = glass_opacity;
	hud->glass = glass;
	hud->glass_group = glass_group_id;
	hud->proj = proj;
	hud->proj_group = proj_group_id;

	return (hud);
}

void
hud_destroy(hud_t *hud)
{
	ASSERT(hud != NULL);

	glDeleteProgram(hud->stencil_shader);
	glDeleteProgram(hud->glass_shader);
	glDeleteProgram(hud->proj_shader);

	if (hud->stencil_fbo != 0)
		glDeleteFramebuffers(1, &hud->stencil_fbo);
	if (hud->stencil_tex != 0) {
		glDeleteTextures(1, &hud->stencil_tex);
		IF_TEXSZ(TEXSZ_FREE(hud_glass_tex, GL_RED, GL_UNSIGNED_BYTE,
		    hud->stencil_w, hud->stencil_h));
	}

	free(hud);
}

static void
update_fbo(hud_t *hud)
{
	vec4 vp;
	int vp_w, vp_h;

	librain_get_vp(vp);

	vp_w = vp[2];
	vp_h = vp[3];

	if (hud->stencil_w == vp_w && hud->stencil_h == vp_h) {
		ASSERT(hud->stencil_fbo != 0);
		return;
	}

	if (hud->stencil_fbo != 0)
		glDeleteFramebuffers(1, &hud->stencil_fbo);
	if (hud->stencil_tex != 0) {
		IF_TEXSZ(TEXSZ_FREE(hud_glass_tex, GL_RED, GL_UNSIGNED_BYTE,
		    hud->stencil_w, hud->stencil_h));
		glDeleteTextures(1, &hud->stencil_tex);
	}

	hud->stencil_w = vp_w;
	hud->stencil_h = vp_h;

	glGenTextures(1, &hud->stencil_tex);
	XPLMBindTexture2d(hud->stencil_tex, GL_TEXTURE_2D);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	IF_TEXSZ(TEXSZ_ALLOC(hud_glass_tex, GL_RED, GL_UNSIGNED_BYTE,
	    hud->stencil_w, hud->stencil_h));
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, hud->stencil_w, hud->stencil_h,
	    0, GL_RED, GL_UNSIGNED_BYTE, NULL);

	glGenFramebuffers(1, &hud->stencil_fbo);
	glBindFramebufferEXT(GL_FRAMEBUFFER, hud->stencil_fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
	    GL_TEXTURE_2D, hud->stencil_tex, 0);
	VERIFY3U(glCheckFramebufferStatus(GL_FRAMEBUFFER), ==,
	    GL_FRAMEBUFFER_COMPLETE);
}

static void
render_stencil(const hud_t *hud, const mat4 pvm)
{
	GLint old_fbo;
	vec4 vp;

	librain_get_vp(vp);

	glutils_debug_push(0, "hud_render_stencil");

	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &old_fbo);

	glBindFramebufferEXT(GL_FRAMEBUFFER, hud->stencil_fbo);
	glViewport(0, 0, hud->stencil_w, hud->stencil_h);
	glClear(GL_COLOR_BUFFER_BIT);

	glUseProgram(hud->stencil_shader);
	glUniformMatrix4fv(glGetUniformLocation(hud->stencil_shader, "pvm"),
	    1, GL_FALSE, (GLfloat *)pvm);
	obj8_draw_group(hud->glass, hud->glass_group, hud->stencil_shader, pvm);

	glBindFramebufferEXT(GL_FRAMEBUFFER, old_fbo);
	glViewport(vp[0], vp[1], vp[2], vp[3]);

	glutils_debug_pop();
}

static void
render_glass(const hud_t *hud, const mat4 pvm)
{
	if (hud->glass_opacity == 0)
		return;

	glutils_debug_push(0, "hud_render_glass");

	glUseProgram(hud->glass_shader);
	glUniformMatrix4fv(glGetUniformLocation(hud->glass_shader, "pvm"),
	    1, GL_FALSE, (GLfloat *)pvm);
	glUniform1f(glGetUniformLocation(hud->glass_shader, "opacity"),
	    hud->glass_opacity);
	obj8_draw_group(hud->glass, hud->glass_group, hud->glass_shader, pvm);

	glutils_debug_pop();
}

static void
render_projection(const hud_t *hud, const mat4 pvm)
{
	GLuint tex = mt_cairo_render_get_tex(hud->mtcr);
	GLuint prog = hud->proj_shader;
	vec4 vp;

	if (tex == 0)
		return;

	librain_get_vp(vp);

	glutils_debug_push(0, "hud_render_projection");

	glUseProgram(prog);

	glUniformMatrix4fv(glGetUniformLocation(prog, "pvm"),
	    1, GL_FALSE, (GLfloat *)pvm);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, tex);
	glUniform1i(glGetUniformLocation(prog, "surf_tex"), 0);
	glUniform2f(glGetUniformLocation(prog, "surf_sz"),
	    mt_cairo_render_get_width(hud->mtcr),
	    mt_cairo_render_get_height(hud->mtcr));

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, hud->stencil_tex);
	glUniform1i(glGetUniformLocation(prog, "stencil_tex"), 1);

	glUniform2f(glGetUniformLocation(prog, "stencil_sz"),
	    hud->stencil_w, hud->stencil_h);

	glUniform4f(glGetUniformLocation(prog, "vp"),
	    vp[0], vp[1], vp[2], vp[3]);

	obj8_draw_group(hud->proj, hud->proj_group, prog, pvm);

	glUseProgram(0);
	XPLMBindTexture2d(0, 1);
	XPLMBindTexture2d(0, 0);
	glActiveTexture(GL_TEXTURE0);

	glutils_debug_pop();
}

void
hud_render(hud_t *hud)
{
	mat4 pvm;

	ASSERT(hud != NULL);
	glutils_debug_push(0, "hud_render");

	glEnable(GL_BLEND);
	librain_get_pvm(pvm);

	update_fbo(hud);

	/* Draw the glass stencil layer */
	render_stencil(hud, pvm);

	/* Draw the opaque glass layer */
	glDepthMask(GL_FALSE);
	render_glass(hud, pvm);

	/* Draw the actual colimated projection */
	render_projection(hud, pvm);
	glDepthMask(GL_TRUE);

	glutils_debug_pop();
}
