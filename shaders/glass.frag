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

layout(location = 10) uniform float	opacity;

layout(location = 0) out vec4		color_out;

void
main(void)
{
	color_out = vec4(0.0, 0.0, 0.0, opacity);
}
