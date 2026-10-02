# Photos for Onyx — study, first mock-ups

> **Status (2026-10-02): mock-ups, to validate with the user.** A photo library "as in a phone or a modern
> desktop": all the pictures of the card in one place, by day and by place, favourites, albums, a slideshow, simple
> editing. Proposed name: **Photos** (app folder `photos`). The Image Viewer stays the quick viewer of one file.

The mock-ups are made by `python3 tools/screenshot/mockup_photos.py` → `docs/photos/mockups/*.png` (1024 × 768, the
real desktop behind; the drawing helpers are `mockup_archiver.py`'s; the photos are drawn, the places and the
camera are made up).

| | |
|---|---|
| ![](mockups/photos-library.png) | **The library.** At the left: **All photos**, **Favourites**, **Recently added**; the **albums** (+ New album); the **folders** watched (Pictures, Camera — where Import puts them —, a volume such as `SD1:`). In the middle the photos **by day**, with the place when the photo knows it (GPS in its EXIF), a circle to select a whole day; a selected photo has a tick, a favourite a heart, a video its length. At the right a **timeline** of the years to jump through thousands of photos. The toolbar: **Import** (a USB stick, a camera's card, a folder), **Slideshow**, the actions on the selection (favourite, add to an album, share: Mail, the Clipboard…, delete), the size of the thumbnails, the **search** (a place, a date, a name, an album). |
| ![](mockups/photos-viewer.png) | **A photo.** Dark around it, full size, the previous / next (arrows, wheel, the keys), the zoom (Fit, +, −, the wheel, a drag when zoomed), the **film strip** of the day underneath. At the top: favourite, rotate, edit, add to an album, share, delete, the **details**: the file (size, weight), the date and time, the camera (f/, speed, ISO, focal length), the place, the folder, its albums, a **description** one can write. |
| ![](mockups/photos-edit.png) | **Editing.** **Crop** (free, original, 1:1, 4:3, 3:2, 16:9, the thirds grid, straighten), **Adjust** (Enhance automatic, exposure, contrast, highlights, shadows, saturation, warmth, sharpness), **Filters** (a few: black and white, warm, cold, vintage, vivid). Before / after; **Save a copy** (the default; or "Save over the original", which keeps the original aside so it can be undone). |
| ![](mockups/photos-albums.png) | **The albums**: their cover, their name and count; right-click: open, slideshow, **send by Mail** (the Mail app, the photos made smaller), set as the wallpaper, **export as a PDF** (a contact sheet / a small photo book, with the MIT PDF writer), rename, delete (the album only, never the photos). |

## How it would be built

| Need | Onyx today | To add |
|---|---|---|
| Decoding | `user/img/imgload.hpp` (`img_load`: JPEG, PNG, GIF, BMP, WebP, PCX) in libwtk | JPEG's **downscaled decode** for the thumbnails (a 12 MP JPEG is 48 MB as pixels): decode then shrink in strips, or the EXIF thumbnail when there is one |
| EXIF | — | our own reader (MIT): date, camera, exposure, GPS, orientation (the photo turned the right way), the embedded thumbnail |
| The library | the File Viewer's walking of folders | `SD:/var/photos/library.db` (one line per photo: path, size, date, w×h, place, favourite, description) + `thumbs/` (256 px JPEG or raw cache), built by a **worker thread** in the background, refreshed when a folder changes |
| Places | — | the GPS → a name with a small offline list of towns (GeoNames cities15000, CC-BY) — no network needed |
| Albums | — | `SD:/var/photos/albums/<name>.txt` (the paths): an album never copies a photo |
| Editing | Paint's filters | crop / rotate / the adjustments on the full picture in a worker thread, saved as JPEG (stb_image_write, public domain) |
| Videos | the Media Player (FFmpeg) | show them with their first frame; open them in the Media Player |
| Sharing | Mail (`mailto` with attachments), Clipboard, wallpaper setting | — |

## To decide with the user

1. The **name**: "Photos"?
2. The **folders watched** by default: `SD:/Pictures` and `SD:/Pictures/Camera`, plus every mounted volume's `DCIM`?
3. **Editing**: save as a copy by default (as drawn), or over the original (the original kept aside)?
4. **Import** from a USB stick / a camera's card: copy into `SD:/Pictures/<year>/<month>`, then offer to delete them on the stick?
5. **Videos** in the library (with the Media Player for playing), or photos only?
6. The **places** from the GPS (an offline list of towns, ~3 MB on the card), or not?
