/*
 * onyx_muftmodule.h -- the FreeType modules of the PDF Viewer (mupdf.mk): the apps' TrueType text (truetype,
 * sfnt, smooth, autofit: user/ft/onyx_ftmodule.h) plus what PDF fonts need (Type 1, CFF, CID, their PostScript
 * helpers, the mono rasteriser): MuPDF's scripts/freetype/slimftmodules.h.
 */
FT_USE_MODULE( FT_Module_Class, autofit_module_class )
FT_USE_MODULE( FT_Driver_ClassRec, tt_driver_class )
FT_USE_MODULE( FT_Driver_ClassRec, t1_driver_class )
FT_USE_MODULE( FT_Driver_ClassRec, cff_driver_class )
FT_USE_MODULE( FT_Driver_ClassRec, t1cid_driver_class )
FT_USE_MODULE( FT_Module_Class, psaux_module_class )
FT_USE_MODULE( FT_Module_Class, psnames_module_class )
FT_USE_MODULE( FT_Module_Class, pshinter_module_class )
FT_USE_MODULE( FT_Renderer_Class, ft_raster1_renderer_class )
FT_USE_MODULE( FT_Module_Class, sfnt_module_class )
FT_USE_MODULE( FT_Renderer_Class, ft_smooth_renderer_class )
