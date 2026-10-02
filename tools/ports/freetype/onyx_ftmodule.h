/*
 * onyx_ftmodule.h -- the FreeType modules of the POSIX ports' FreeType (tools/ports/freetype: the one HarfBuzz,
 * Skia and later WebKit link; the apps' lean one is user/ft). What web fonts need: TrueType and OpenType
 * (TrueType and CFF / CFF2 outlines, variable fonts), the auto-hinter and the PostScript hinter, the
 * anti-aliased and the monochrome rasterizers, OT-SVG glyphs (rendered through the hooks a client sets).
 * Not: Type 1, CID, Type 42, PFR, Windows FNT, PCF, BDF, SDF.
 *
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (this file).
 */
FT_USE_MODULE( FT_Module_Class, autofit_module_class )
FT_USE_MODULE( FT_Driver_ClassRec, tt_driver_class )
FT_USE_MODULE( FT_Driver_ClassRec, cff_driver_class )
FT_USE_MODULE( FT_Module_Class, psaux_module_class )
FT_USE_MODULE( FT_Module_Class, psnames_module_class )
FT_USE_MODULE( FT_Module_Class, pshinter_module_class )
FT_USE_MODULE( FT_Module_Class, sfnt_module_class )
FT_USE_MODULE( FT_Renderer_Class, ft_smooth_renderer_class )
FT_USE_MODULE( FT_Renderer_Class, ft_raster1_renderer_class )
FT_USE_MODULE( FT_Renderer_Class, ft_svg_renderer_class )
