# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#
# Copyright 2021 Saso Kiselkov. All rights reserved.

SPVS = \
    generic.vert.spv \
    glass.frag.spv \
    proj_glow.frag.spv \
    proj_noglow.frag.spv \
    proj_mono.frag.spv \
    stencil.frag.spv

OUTDIR=build
SPIRVX_TGT_VERSION=120
SPIRVX_420_VERSION=420
SPIRVX_430_VERSION=430
SPIRVX_450_VERSION=450

GLSLANG=glslangValidator
SPIRVX=spirv-cross
GLSL_CLEANUP=./glsl_cleanup.sh
ECHO=/bin/echo

ifeq ($(V),1)
	VERB=
define logMsg
endef
else	# Not Verbose
	VERB=@
define logMsg
	@$(ECHO) $(1)
endef
endif

define BUILD_SHADER
	$(call logMsg,-n \	[GLSLANG]\	)
	$(VERB) $(GLSLANG) $(2) -G -o $@ $^

	$(call logMsg,\	[SPIRVX]\	$@)
	$(VERB) $(SPIRVX) --version $(SPIRVX_TGT_VERSION) \
	    --extension GL_EXT_gpu_shader4 \
	    --output $(@:%.$(1).spv=%.$(1).glsl) $@
	$(VERB) $(GLSL_CLEANUP) $(@:%.$(1).spv=%.$(1).glsl)

	$(call logMsg,\	[SPIRVX 4.20]\	$(@:%.$(1).spv=%.$(1).glsl420))
	$(VERB) $(SPIRVX) --version $(SPIRVX_420_VERSION) \
	    --output $(@:%.$(1).spv=%.$(1).glsl420) $@
	$(VERB) $(GLSL_CLEANUP) $(@:%.$(1).spv=%.$(1).glsl420)
endef

define BUILD_SHADER_MODERN
	$(call logMsg,-n \	[GLSLANG]\	)
	$(VERB) $(GLSLANG) $(2) -G -o $@ $^

	$(call logMsg,\	[SPIRVX 4.50]\	$(@:%.$(1).spv=%.$(1).glsl450))
	$(VERB) $(SPIRVX) --version $(SPIRVX_450_VERSION) \
	    --output $(@:%.$(1).spv=%.$(1).glsl450) $@
endef

define BUILD_COMP_SHADER
	$(call logMsg,-n \	[GLSLANG]\	)
	$(VERB) $(GLSLANG) $(2) -G -S comp -o $@ $^

	$(call logMsg,\	[SPIRVX 4.30]\	$(@:%.$(1).spv=%.$(1).glsl430))
	$(VERB) $(SPIRVX) --version $(SPIRVX_430_VERSION) \
	    --output $(@:%.$(1).spv=%.$(1).glsl430) $@
endef

SPVS_OUT=$(addprefix $(OUTDIR)/,$(SPVS))
all : $(SPVS_OUT)

.PHONY: clean
clean :
	rm -f $(SPVS_OUT) $(patsubst %.spv,%.glsl,$(SPVS_OUT)) \
	    $(patsubst %.spv,%.glsl420,$(SPVS_OUT))

$(OUTDIR)/proj_glow.frag.spv : proj.frag
	$(call BUILD_SHADER,frag,-DGLOW=1 -DMONOCHROME=0)
$(OUTDIR)/proj_noglow.frag.spv : proj.frag
	$(call BUILD_SHADER,frag,-DGLOW=0 -DMONOCHROME=0)
$(OUTDIR)/proj_mono.frag.spv : proj.frag
	$(call BUILD_SHADER,frag,-DGLOW=0 -DMONOCHROME=1)

$(OUTDIR)/%.vert.spv : %.vert
	$(call BUILD_SHADER,vert)

$(OUTDIR)/%.frag.spv : %.frag
	$(call BUILD_SHADER,frag)

$(OUTDIR)/%.comp.spv : %.glsl
	$(call BUILD_COMP_SHADER,comp)

$(addprefix $(OUTDIR)/,$(SPVS)) : | $(OUTDIR)

$(OUTDIR) :
	mkdir -p $(OUTDIR)
