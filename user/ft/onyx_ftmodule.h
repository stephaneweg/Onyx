/*
 * onyx_ftmodule.h -- the FreeType modules of the apps' text (user/ft/): TrueType fonts (the
 * truetype driver + sfnt), anti-aliased (smooth), hinted by the auto-hinter (autofit). Selected
 * by -DFT_CONFIG_MODULES_H="<onyx_ftmodule.h>"; the sources compiled: FT_FILES in user/Makefile.
 */
FT_USE_MODULE( FT_Module_Class, autofit_module_class )
FT_USE_MODULE( FT_Driver_ClassRec, tt_driver_class )
FT_USE_MODULE( FT_Module_Class, sfnt_module_class )
FT_USE_MODULE( FT_Renderer_Class, ft_smooth_renderer_class )
