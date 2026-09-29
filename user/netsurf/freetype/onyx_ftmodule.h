/*
 * onyx_ftmodule.h -- the FreeType modules built for Onyx (NetSurf's fonts): TrueType fonts
 * (the truetype driver + sfnt + psnames) and OpenType's CFF ones (cff + psaux + pshinter:
 * web fonts), anti-aliased (smooth), hinted by the fonts' bytecode or the auto-hinter
 * (autofit); WOFF / WOFF2 through sfnt (zlib, Brotli: onyx_ftoption.h). Selected by
 * -DFT_CONFIG_MODULES_H="<onyx_ftmodule.h>". The sources compiled: FT_FILES in
 * user/netsurf/Makefile.
 */
FT_USE_MODULE( FT_Module_Class, autofit_module_class )
FT_USE_MODULE( FT_Driver_ClassRec, tt_driver_class )
FT_USE_MODULE( FT_Driver_ClassRec, cff_driver_class )
FT_USE_MODULE( FT_Module_Class, psaux_module_class )
FT_USE_MODULE( FT_Module_Class, psnames_module_class )
FT_USE_MODULE( FT_Module_Class, pshinter_module_class )
FT_USE_MODULE( FT_Module_Class, sfnt_module_class )
FT_USE_MODULE( FT_Renderer_Class, ft_smooth_renderer_class )
