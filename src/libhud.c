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

#include <XPLMDisplay.h>
#include <XPLMGraphics.h>

#include <acfutils/dr.h>
#include <acfutils/glew.h>
#include <acfutils/glutils.h>
#include <acfutils/helpers.h>
#include <acfutils/safe_alloc.h>
#include <acfutils/shader.h>
#include <acfutils/time.h>

#include "libhud.h"

#define	CAPTURE_PHASE		xplm_Phase_Modern3D
#define	CAPTURE_PHASE_BEFORE	false
#define	DRAW_PHASE		xplm_Phase_Window
#define	DRAW_PHASE_BEFORE	true

/*
 * Values of sim/graphics/view/draw_call_type - this is important
 * when capturing graphics matrices & viewport to support VR.
 */
typedef enum {
    DRAW_CALL_NONE = 0,
    DRAW_CALL_MONO = 1,
    DRAW_CALL_DCT_STEREO = 2,
    DRAW_CALL_LEFT_EYE = 3,
    DRAW_CALL_RIGHT_EYE = 4
} draw_call_type_t;

/*
 * Values of sim/graphics/view/plane_render_type
 */
typedef enum {
    PLANE_RENDER_NONE = 0,
    PLANE_RENDER_SOLID = 1,
    PLANE_RENDER_BLEND = 2
} plane_render_type_t;

/*
 * Values of sim/graphics/view/world_render_type
 */
typedef enum {
    WORLD_RENDER_TYPE_NORM = 0,
    WORLD_RENDER_TYPE_REFLECT = 1,
    WORLD_RENDER_TYPE_INVALID = 6
} world_render_type_t;

TEXSZ_MK_TOKEN(hud_glass_tex);

enum {
    PROJ_SHADER_GLOW,
    PROJ_SHADER_NOGLOW,
    PROJ_SHADER_MONO_GLOW,
    PROJ_SHADER_MONO_NOGLOW,
    NUM_PROJ_SHADERS
};

static shader_info_t generic_vert_info = { .filename = "generic.vert.spv" };
static shader_info_t stencil_frag_info = { .filename = "stencil.frag.spv" };
static shader_info_t glass_frag_info = { .filename = "glass.frag.spv" };
static shader_info_t proj_frag_info[NUM_PROJ_SHADERS] = {
    [PROJ_SHADER_GLOW] = { .filename = "proj_glow.frag.spv" },
    [PROJ_SHADER_NOGLOW] = { .filename = "proj_noglow.frag.spv" },
    [PROJ_SHADER_MONO_GLOW] = { .filename = "proj_mono_glow.frag.spv" },
    [PROJ_SHADER_MONO_NOGLOW] = { .filename = "proj_mono_noglow.frag.spv" }
};

static shader_prog_info_t glass_prog_info = {
    .progname = "libhud_glass",
    .vert = &generic_vert_info,
    .frag = &glass_frag_info
};

static shader_prog_info_t stencil_prog_info = {
    .progname = "libhud_stencil",
    .vert = &generic_vert_info,
    .frag = &stencil_frag_info
};

static shader_prog_info_t proj_prog_info[NUM_PROJ_SHADERS] = {
    [PROJ_SHADER_GLOW] = {
	.progname = "libhud_proj_glow",
	.vert = &generic_vert_info,
	.frag = &proj_frag_info[PROJ_SHADER_GLOW]
    },
    [PROJ_SHADER_NOGLOW] = {
	.progname = "libhud_proj_noglow",
	.vert = &generic_vert_info,
	.frag = &proj_frag_info[PROJ_SHADER_NOGLOW]
    },
    [PROJ_SHADER_MONO_GLOW] = {
	.progname = "libhud_proj_mono_glow",
	.vert = &generic_vert_info,
	.frag = &proj_frag_info[PROJ_SHADER_MONO_GLOW]
    },
    [PROJ_SHADER_MONO_NOGLOW] = {
	.progname = "libhud_proj_mono_noglow",
	.vert = &generic_vert_info,
	.frag = &proj_frag_info[PROJ_SHADER_MONO_NOGLOW]
    }
};

struct hud_s {
	char			*shader_dir;
	mt_cairo_render_t	*mtcr;
	bool			enabled;
	bool			rev_y;
	bool			rev_float_z;
	float			brt;
	bool			glow;
	float			blur_radius;
	bool			depth_test;

	struct {
		GLuint		prog;
		GLint		pvm;
	} stencil_shader;
	struct {
		GLuint		prog;
		GLint		pvm;
		GLint		opacity;
	} glass_shader;
	struct {
		GLuint		prog;
		GLint		pvm;
		GLint		surf_tex;
		GLint		surf_sz;
		GLint		stencil_tex;
		GLint		stencil_sz;
		GLint		vp;
		GLint		brt;
		GLint		blur_radius;
		GLint		beam_color;
	} proj_shader[NUM_PROJ_SHADERS];

	GLuint			stencil_fbo;
	GLuint			stencil_tex;
	int			stencil_w;
	int			stencil_h;

	double			glass_opacity;
	obj8_t			*glass;
	char			*glass_group;
	obj8_t			*proj;
	char			*proj_group;

	unsigned		num_eyes;
	mat4			proj_mtx[2];
	mat4			acf_mtx[2];
	vec4			vp[2];
	struct {
		dr_t		old_fbo;
		dr_t		world_render_type;
		dr_t		plane_render_type;
		dr_t		draw_call_type;
		dr_t		acf_mtx;
		dr_t		proj_mtx;
		dr_t		vp;
		dr_t		rev_y;
		dr_t		rev_float_z;
		bool		aa_ratio_avail;
		dr_t		fsaa_ratio_x;
		dr_t		fsaa_ratio_y;
	} drs;
};

static int
capture_cb(XPLMDrawingPhase phase, int before, void *refcon)
{
	hud_t *hud;
	int idx;
	int vp[4];

	UNUSED(phase);
	UNUSED(before);
	ASSERT(refcon != NULL);
	hud = refcon;
	/*
	 * No GL calls take place here, so no need for GLUTILS_RESET_ERRORS()
	 */
	switch (dr_geti(&hud->drs.draw_call_type)) {
	case DRAW_CALL_RIGHT_EYE:
		idx = 1;
		hud->num_eyes = 2;
		break;
	case DRAW_CALL_LEFT_EYE:
		idx = 0;
		hud->num_eyes = 2;
		break;
	default:
		idx = 0;
		hud->num_eyes = 1;
		break;
	}
	VERIFY3S(dr_getvf32(&hud->drs.acf_mtx,
	    (float *)hud->acf_mtx[idx], 0, 16), ==, 16);
	VERIFY3S(dr_getvf32(&hud->drs.proj_mtx,
	    (float *)hud->proj_mtx[idx], 0, 16), ==, 16);
	VERIFY3S(dr_getvi(&hud->drs.vp, vp, 0, 4), ==, 4);
	for (int i = 0; i < 4; i++)
		hud->vp[idx][i] = vp[i];
	hud->rev_y = (dr_geti(&hud->drs.rev_y) != 0);
	hud->rev_float_z = (dr_geti(&hud->drs.rev_float_z) != 0);
	/*
	 * When not using reverse float Z (which only happens in
	 * OpenGL non-VR rendering), the projection matrix X-Plane
	 * uses has the near clipping plane very far (about 1
	 * meter), so we bring it much closer to get it fixed up.
	 */
	if (!hud->rev_float_z) {
		for (int i = 0; i < 4; i++)
			hud->proj_mtx[idx][3][i] /= 100;
	}
	if (hud->drs.aa_ratio_avail) {
		vect2_t fsaa_ratio = VECT2(dr_getf(&hud->drs.fsaa_ratio_x),
		    dr_getf(&hud->drs.fsaa_ratio_y));
		if (fsaa_ratio.x >= 1 && fsaa_ratio.y >= 1) {
			hud->vp[idx][0] /= fsaa_ratio.x;
			hud->vp[idx][1] /= fsaa_ratio.y;
			hud->vp[idx][2] /= fsaa_ratio.x;
			hud->vp[idx][3] /= fsaa_ratio.y;
		}
	}

	return (1);
}

static int
draw_cb(XPLMDrawingPhase phase, int before, void *refcon)
{
	hud_t *hud;
	int old_vp[4];
	GLint saved_clip_origin, saved_depth_mode, saved_front_face;

	UNUSED(phase);
	UNUSED(before);
	ASSERT(refcon != NULL);
	hud = refcon;
	/*
	 * X-Plane tends to run in reverse-Y when drawing 3D. So in that
	 * case, our projection is reversed. It's easiest to just swap
	 * the render state back over to reverse-Y.
	 */
	if (hud->rev_y) {
		glGetIntegerv(GL_CLIP_ORIGIN, (GLint *)&saved_clip_origin);
		glGetIntegerv(GL_CLIP_DEPTH_MODE, &saved_depth_mode);
		glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE);
		glGetIntegerv(GL_FRONT_FACE, &saved_front_face);
		glFrontFace(GL_CCW);
	}
	VERIFY3S(dr_getvi(&hud->drs.vp, old_vp, 0, 4), ==, 4);
	for (unsigned i = 0; i < hud->num_eyes; i++) {
		mat4 pvm;

		glm_mat4_mul(hud->proj_mtx[i], hud->acf_mtx[i], pvm);
		glViewport(hud->vp[i][0], hud->vp[i][1],
		    hud->vp[i][2], hud->vp[i][3]);
		hud_render_eye(hud, pvm, hud->vp[i]);
	}
	/*
	 * Restore original state
	 */
	glViewport(old_vp[0], old_vp[1], old_vp[2], old_vp[3]);
	if (hud->rev_y) {
		glClipControl(saved_clip_origin, saved_depth_mode);
		glFrontFace(saved_front_face);
	}
	GLUTILS_ASSERT_NO_ERROR();

	return (1);
}

static bool
hud_reload_shader(hud_t *hud, GLuint *prog, const shader_prog_info_t *info)
{
	GLuint new_prog;

	ASSERT(hud != NULL);
	new_prog = shader_prog_from_info(hud->shader_dir, info);
	if (new_prog == 0)
		return (false);
	if (*prog != 0 && new_prog != *prog)
		glDeleteProgram(*prog);
	*prog = new_prog;

	return (true);
}

static bool
reload_shaders(hud_t *hud)
{
	ASSERT(hud != NULL);

	if (!hud_reload_shader(hud, &hud->glass_shader.prog, &glass_prog_info))
		return (false);
	hud->glass_shader.pvm =
	    glGetUniformLocation(hud->glass_shader.prog, "pvm");
	hud->glass_shader.opacity =
	    glGetUniformLocation(hud->glass_shader.prog, "opacity");

	if (!hud_reload_shader(hud, &hud->stencil_shader.prog,
	    &stencil_prog_info)) {
		return (false);
	}
	hud->stencil_shader.pvm =
	    glGetUniformLocation(hud->stencil_shader.prog, "pvm");

	for (int i = 0; i < NUM_PROJ_SHADERS; i++) {
		if (!hud_reload_shader(hud, &hud->proj_shader[i].prog,
		    &proj_prog_info[i])) {
			return (false);
		}
		hud->proj_shader[i].pvm = glGetUniformLocation(
		    hud->proj_shader[i].prog, "pvm");
		hud->proj_shader[i].surf_tex = glGetUniformLocation(
		    hud->proj_shader[i].prog, "surf_tex");
		hud->proj_shader[i].surf_sz = glGetUniformLocation(
		    hud->proj_shader[i].prog, "surf_sz");
		hud->proj_shader[i].stencil_tex = glGetUniformLocation(
		    hud->proj_shader[i].prog, "stencil_tex");
		hud->proj_shader[i].stencil_sz = glGetUniformLocation(
		    hud->proj_shader[i].prog, "stencil_sz");
		hud->proj_shader[i].vp = glGetUniformLocation(
		    hud->proj_shader[i].prog, "vp");
		hud->proj_shader[i].brt = glGetUniformLocation(
		    hud->proj_shader[i].prog, "brt");
		hud->proj_shader[i].blur_radius = glGetUniformLocation(
		    hud->proj_shader[i].prog, "blur_radius");
		hud->proj_shader[i].beam_color = glGetUniformLocation(
		    hud->proj_shader[i].prog, "beam_color");
	}

	return (true);
}

/**
 * Constructs and initializes a new HUD instance. The HUD is initially
 * set to disabled.
 *
 * @param shader_dir A path to the directory containing the compiled
 *	libhud shaders in SPIR-V and GLSL format.
 * @param mtcr The mt_cairo_render_t instance that should be used as
 *	the HUD projection. This is texture-mapped onto the projection
 *	object during rendering.
 * @param glass_opacity Allows specifying a darkening factor (0-1) that
 *	is applied to the HUD combiner glass render. Set to 0 to disable
 *	rendering the glass opacity. The glass will still be used as the
 *	stencil mask for the projection.
 * @param glass Glass object that will be rendered in 3D space. Please
 *	note that the render is done with depth masking disabled, so you
 *	must make sure to disable rendering when the glass could be
 *	occluded by other objects.
 * @param glass_group_id An optional value when you are using group-IDs
 *	in your OBJs. Pass NULL to render everything from the glass OBJ.
 * @param proj Projection object that will be rendered in 3D space. This
 *	is the render that will be texture-mapped with your mtcr's
 *	surface. Be sure to assign UV coordinates correctly on this OBJ.
 * @param proj_group_id An optional value when you are using group-IDs
 *	in your OBJs. Pass NULL to render everything in the `proj' OBJ.
 */
hud_t *
hud_new(const char *shader_dir, mt_cairo_render_t *mtcr, double glass_opacity,
    obj8_t *glass, const char *glass_group_id,
    obj8_t *proj, const char *proj_group_id)
{
	hud_t *hud = safe_calloc(1, sizeof (*hud));

	ASSERT(shader_dir != NULL);
	ASSERT(mtcr != NULL);
	ASSERT(glass != NULL);
	ASSERT(proj != NULL);

	hud->shader_dir = safe_strdup(shader_dir);
	hud->mtcr = mtcr;
	hud->brt = 1;

	if (!reload_shaders(hud))
		goto errout;

	hud->glass_opacity = glass_opacity;
	hud->glass = glass;
	if (glass_group_id != NULL)
		hud->glass_group = safe_strdup(glass_group_id);
	hud->proj = proj;
	if (proj_group_id != NULL)
		hud->proj_group = safe_strdup(proj_group_id);

	fdr_find(&hud->drs.old_fbo, "sim/graphics/view/current_gl_fbo");
	fdr_find(&hud->drs.draw_call_type, "sim/graphics/view/draw_call_type");
	fdr_find(&hud->drs.plane_render_type,
	    "sim/graphics/view/plane_render_type");
	fdr_find(&hud->drs.world_render_type,
	    "sim/graphics/view/world_render_type");
	fdr_find(&hud->drs.proj_mtx, "sim/graphics/view/projection_matrix");
	fdr_find(&hud->drs.acf_mtx, "sim/graphics/view/acf_matrix");
	fdr_find(&hud->drs.vp, "sim/graphics/view/viewport");
	fdr_find(&hud->drs.rev_y, "sim/graphics/view/is_reverse_y");
	fdr_find(&hud->drs.rev_float_z,
	    "sim/graphics/view/is_reverse_float_z");
	hud->drs.aa_ratio_avail = (dr_find(&hud->drs.fsaa_ratio_x,
	    "sim/private/controls/hdr/fsaa_ratio_x") &&
	    dr_find(&hud->drs.fsaa_ratio_y,
	    "sim/private/controls/hdr/fsaa_ratio_y"));

	return (hud);
errout:
	hud_destroy(hud);
	return (NULL);
}

/**
 * Destroys a HUD object.
 */
void
hud_destroy(hud_t *hud)
{
	ASSERT(hud != NULL);

	if (hud->stencil_shader.prog != 0)
		glDeleteProgram(hud->stencil_shader.prog);
	if (hud->glass_shader.prog != 0)
		glDeleteProgram(hud->glass_shader.prog);
	for (int i = 0; i < NUM_PROJ_SHADERS; i++) {
		if (hud->proj_shader[i].prog != 0)
			glDeleteProgram(hud->proj_shader[i].prog);
	}
	if (hud->stencil_fbo != 0)
		glDeleteFramebuffers(1, &hud->stencil_fbo);
	if (hud->stencil_tex != 0) {
		glDeleteTextures(1, &hud->stencil_tex);
		IF_TEXSZ(TEXSZ_FREE(hud_glass_tex, GL_RED, GL_UNSIGNED_BYTE,
		    hud->stencil_w, hud->stencil_h));
	}

	free(hud->shader_dir);
	free(hud->glass_group);
	free(hud->proj_group);

	if (hud->enabled) {
		VERIFY(XPLMUnregisterDrawCallback(capture_cb,
		    CAPTURE_PHASE, CAPTURE_PHASE_BEFORE, hud));
		VERIFY(XPLMUnregisterDrawCallback(draw_cb,
		    DRAW_PHASE, DRAW_PHASE_BEFORE, hud));
	}

	free(hud);
}

/**
 * Turns rendering of the HUD on or off. Since the HUD isn't depth-masked,
 * you must disable its rendering when the camera is in a location where
 * it can't be visible anyway (e.g. out of the line of sight of the HUD).
 * By default, the HUD is disabled for rendering. You can call
 * hud_set_enabled as many times with the same argument as you want, the
 * function makes sure not to exercise any costly algorithms if the state
 * hasn't changed.
 */
void
hud_set_enabled(hud_t *hud, bool flag)
{
	ASSERT(hud != NULL);

	if (hud->enabled == flag)
		return;

	hud->enabled = flag;
	if (flag) {
		VERIFY(XPLMRegisterDrawCallback(capture_cb,
		    CAPTURE_PHASE, CAPTURE_PHASE_BEFORE, hud));
		VERIFY(XPLMRegisterDrawCallback(draw_cb,
		    DRAW_PHASE, DRAW_PHASE_BEFORE, hud));
	} else {
		VERIFY(XPLMUnregisterDrawCallback(capture_cb,
		    CAPTURE_PHASE, CAPTURE_PHASE_BEFORE, hud));
		VERIFY(XPLMUnregisterDrawCallback(draw_cb,
		    DRAW_PHASE, DRAW_PHASE_BEFORE, hud));
	}
}

/**
 * Returns true if the HUD is enabled for rendering, FALSE if not.
 */
bool
hud_get_enabled(const hud_t *hud)
{
	ASSERT(hud != NULL);
	return (hud);
}

/**
 * Sets an additional brightness multiplier for the projection rendering.
 * This defaults to 1.0 after creation (maximum brightness). This multiplier
 * is applied to the alpha component of the fragment shader, not the color.
 * So at lower brightness levels, the HUD's symbology will appear to be more
 * transparent, rather than darker.
 */
void
hud_set_brightness(hud_t *hud, float brt)
{
	ASSERT(hud != NULL);
	hud->brt = brt;
}

/**
 * Returns the HUD's current brightness setting. Right after creation,
 * this defaults to 1.0.
 */
float
hud_get_brightness(const hud_t *hud)
{
	ASSERT(hud != NULL);
	return (hud->brt);
}

/**
 * Changes the mt_cairo_render instance used by the HUD object.
 * DO NOT pass NULL here, or you will see sparks flying!
 */
void
hud_set_mtcr(hud_t *hud, mt_cairo_render_t *mtcr)
{
	ASSERT(hud != NULL);
	ASSERT(mtcr != NULL);
	hud->mtcr = mtcr;
}

/**
 * Returns the mt_cairo_render instance being used by the HUD object.
 */
mt_cairo_render_t *
hud_get_mtcr(const hud_t *hud)
{
	ASSERT(hud != NULL);
	return (hud->mtcr);
}

/**
 * Controls whether the fragment shader applies a slight blur shader to
 * the projected image. By default, this is `false'. When you set flag
 * to `true', you should also pass a `blur_radius' value, which controls
 * how far the fragment shader blurs the image. This should be between
 * 0 and around 2. Any higher, and there's a chance of visual artifacts.
 */
void
hud_set_glow(hud_t *hud, bool flag, float blur_radius)
{
	ASSERT(hud != NULL);
	hud->glow = flag;
	hud->blur_radius = blur_radius;
}

/**
 * Returns true if the HUD is using a glow/blur filter in the final
 * projection render, or false if not. If the `blur_radius' argument
 * is provided, it is filled with the blur radius of the glow effect.
 */
bool
hud_get_glow(const hud_t *hud, float *blur_radius)
{
	ASSERT(hud != NULL);
	if (blur_radius != NULL)
		*blur_radius = hud->blur_radius;
	return (hud->glow);
}

/**
 * Configures whether the projection is rendered with depth testing enabled.
 * The default is depth testing disabled, because the projection is rendered
 * in window-space without a proper 3D depth buffer. However, when using
 * librain z-objets, there is usable depth buffer information, so setting
 * this flag enables proper z-masking of the HUD projection.
 */
void
hud_set_depth_test(hud_t *hud, bool flag)
{
	ASSERT(hud != NULL);
	hud->depth_test = flag;
}

/*
 * Returns true when depth testing is enabled, false if it isn't.
 * The default is false.
 */
bool
hud_get_depth_test(const hud_t *hud)
{
	return (hud->depth_test);
}

static void
update_fbo(hud_t *hud, const vec4 vp)
{
	int vp_w, vp_h;

	ASSERT(hud != NULL);
	ASSERT(vp != NULL);

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
render_stencil(const hud_t *hud, const mat4 pvm, const vec4 vp)
{
	GLint old_fbo;

	ASSERT(hud != NULL);
	ASSERT(pvm != NULL);
	ASSERT(vp != NULL);

	glutils_debug_push(0, "hud_render_stencil");

	old_fbo = dr_geti(&hud->drs.old_fbo);

	glBindFramebufferEXT(GL_FRAMEBUFFER, hud->stencil_fbo);
	glViewport(0, 0, hud->stencil_w, hud->stencil_h);
	glClear(GL_COLOR_BUFFER_BIT);

	glUseProgram(hud->stencil_shader.prog);
	glUniformMatrix4fv(hud->stencil_shader.pvm, 1, GL_FALSE,
	    (GLfloat *)pvm);
	obj8_draw_group(hud->glass, hud->glass_group,
	    hud->stencil_shader.prog, pvm);

	glBindFramebufferEXT(GL_FRAMEBUFFER, old_fbo);
	glViewport(vp[0], vp[1], vp[2], vp[3]);

	glutils_debug_pop();
}

static void
render_glass(const hud_t *hud, const mat4 pvm)
{
	ASSERT(hud != NULL);
	ASSERT(pvm != NULL);

	if (hud->glass_opacity == 0)
		return;

	glutils_debug_push(0, "hud_render_glass");

	glUseProgram(hud->glass_shader.prog);
	glUniformMatrix4fv(hud->glass_shader.pvm, 1, GL_FALSE, (GLfloat *)pvm);
	glUniform1f(hud->glass_shader.opacity, hud->glass_opacity);
	obj8_draw_group(hud->glass, hud->glass_group,
	    hud->glass_shader.prog, pvm);

	glutils_debug_pop();
}

static void
render_projection(const hud_t *hud, const mat4 pvm, const vec4 vp,
    unsigned prog)
{
	vect3_t monochrome;
	GLuint tex;

	ASSERT(hud != NULL);
	ASSERT(pvm != NULL);
	ASSERT(vp != NULL);
	ASSERT3U(prog, <, ARRAY_NUM_ELEM(hud->proj_shader));

	tex = mt_cairo_render_get_tex(hud->mtcr);
	if (tex == 0)
		return;
	monochrome = mt_cairo_render_get_monochrome(hud->mtcr);

	glutils_debug_push(0, "hud_render_projection");

	if (!hud->depth_test)
		glDisable(GL_DEPTH_TEST);
	glUseProgram(hud->proj_shader[prog].prog);

	glUniformMatrix4fv(hud->proj_shader[prog].pvm,
	    1, GL_FALSE, (GLfloat *)pvm);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, tex);
	glUniform1i(hud->proj_shader[prog].surf_tex, 0);
	glUniform2f(hud->proj_shader[prog].surf_sz,
	    mt_cairo_render_get_width(hud->mtcr),
	    mt_cairo_render_get_height(hud->mtcr));

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, hud->stencil_tex);
	glUniform1i(hud->proj_shader[prog].stencil_tex, 1);

	glUniform2f(hud->proj_shader[prog].stencil_sz,
	    hud->stencil_w, hud->stencil_h);

	glUniform4f(hud->proj_shader[prog].vp, vp[0], vp[1], vp[2], vp[3]);
	glUniform1f(hud->proj_shader[prog].brt, hud->brt);

	glUniform1f(hud->proj_shader[prog].blur_radius, hud->blur_radius);
	glUniform1f(hud->proj_shader[prog].brt, hud->brt);
	if (!IS_NULL_VECT(monochrome)) {
		glUniform3f(hud->proj_shader[prog].beam_color,
		    monochrome.x, monochrome.y, monochrome.z);
	}
	obj8_draw_group(hud->proj, hud->proj_group,
	    hud->proj_shader[prog].prog, pvm);

	glUseProgram(0);
	XPLMBindTexture2d(0, 1);
	XPLMBindTexture2d(0, 0);
	glActiveTexture(GL_TEXTURE0);
	if (!hud->depth_test)
		glEnable(GL_DEPTH_TEST);

	glutils_debug_pop();
}

/**
 * Allows invoking the HUD renderer with a custom projection-modelview
 * matrix and viewport. If you are using `hud_set_enabled', you don't
 * need to call this.
 */
void
hud_render_eye(hud_t *hud, const mat4 pvm, const vec4 vp)
{
	vect3_t monochrome;

	ASSERT(hud != NULL);
	ASSERT(pvm != NULL);
	ASSERT(vp != NULL);

	monochrome = mt_cairo_render_get_monochrome(hud->mtcr);

	glutils_debug_push(0, "hud_render");

	glEnable(GL_BLEND);

	update_fbo(hud, vp);

	/* Draw the glass stencil layer */
	render_stencil(hud, pvm, vp);

	/* Draw the opaque glass layer */
	glDepthMask(GL_FALSE);
	render_glass(hud, pvm);

	/* Draw the actual collimated projection */
	if (hud->glow) {
		if (IS_NULL_VECT(monochrome))
			render_projection(hud, pvm, vp, PROJ_SHADER_GLOW);
		else
			render_projection(hud, pvm, vp, PROJ_SHADER_MONO_GLOW);
	}
	if (IS_NULL_VECT(monochrome))
		render_projection(hud, pvm, vp, PROJ_SHADER_NOGLOW);
	else
		render_projection(hud, pvm, vp, PROJ_SHADER_MONO_NOGLOW);
	glDepthMask(GL_TRUE);

	glutils_debug_pop();
}
