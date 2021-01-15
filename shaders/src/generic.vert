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

layout(location = 0) uniform mat4	pvm;

layout(location = 0) in vec3		vtx_pos;
layout(location = 1) in vec2		vtx_norm;
layout(location = 2) in vec2		vtx_tex0;

layout(location = 0) out vec2		tex_coord;

void
main()
{
	tex_coord = vtx_tex0;
	gl_Position = pvm * vec4(vtx_pos, 1.0);
}
