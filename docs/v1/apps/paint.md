# Images and Paint: plan (GLOS v1, phase A)

**Today: Images, a viewer.**
- **Formats:** PNG, JPEG, BMP, GIF (its first frame), TGA, PNM (stb_image), PCX (its own).
- **Viewing:** fitted or at a zoom, the folder's next and previous picture, dithered on an 8-bit desktop.

**In v1 (the owner's choice, 2026-11-06): two programs.**
- **Images stays the viewer:** light and quick to open, grown as below.
- **Paint is a new editor** after Paint.NET: layers, tools, adjustments, effects, undo. Images opens the picture in
  it with Edit (E, or a toolbar button); Paint can also be started on its own or from Files' "Edit".
- **They share their code:** the decoders, the tiled image, the filmstrip and the view are one library in both, so
  a picture looks the same in each.

## What it does

- **Images, the viewer, as today and more:**
  - full screen and a slideshow;
  - rotate by 90 degrees without loss (JPEG too, by re-encoding at the same quality, or a lossless path where the
    decoder allows: settled in C);
  - a picture's details (size, depth, format, date);
  - a filmstrip of the folder's pictures, loaded as they come.
- **Images:** any size memory allows, in 32-bit RGBA internally, kept as tiles so that big ones don't need one huge
  block; on an 8-bit or 16-bit desktop shown dithered.
- **Layers:** add, delete, duplicate, merge down, reorder, hide, opacity, the blend modes Paint.NET has (normal,
  multiply, additive, screen, overlay, difference, lighten, darken).
- **Tools:**
  - **Selecting:** rectangle, ellipse, lasso, magic wand (tolerance, global); move a selection or its pixels.
  - **Drawing:** pencil, brush (size, hardness, antialiasing), eraser, bucket fill (tolerance), gradient (linear,
    radial), line and curve, rectangle, ellipse, rounded rectangle (outline, fill, both).
  - **The rest:** text (the system's fonts, and fonts from files where loaded), colour picker, clone stamp, recolour,
    zoom and pan.
- **Colours:** a palette, primary and secondary colours, a colour dialog (RGB, HSV, hex, alpha), recent colours.
- **The image:** resize (nearest, bilinear, bicubic), canvas size, rotate and flip, crop to the selection.
- **Adjustments:** auto level, brightness and contrast, hue and saturation, levels, curves, invert, black and white,
  sepia, posterize.
- **Effects:** blur (gaussian, motion, zoom), sharpen, noise (add, reduce, median), artistic (oil paint, pencil
  sketch, ink), distort (pixelate, tile reflection, twist), edge detect, emboss, relief.
- **History:** undo and redo of every step, as a list to click back through, kept in memory and spilled to disk for
  big images.
- **Files:**
  - **In:** PNG, JPEG, BMP, GIF, TGA, PNM, PCX.
  - **Out:** PNG, BMP, PCX, and JPEG (quality chosen).
  - **Layers:** Paint's own layered format (PNG pieces in a ZIP, as OpenRaster does), so layers survive saving.
- **The clipboard:** copy and paste pictures (CLIP's image format), paste as a new layer or a new picture.
- **DOS-era touches:** a picture reduced to 256 or 16 colours, with a palette chosen (an optimised palette, the VGA's
  default, a file's), dithered or not, for DOS games and VGA work.

## How it looks

```
+------------------------------------------------------------------+
| File Edit View Image Layers Adjustments Effects Help             |
| [New][Open][Save] | [Undo][Redo] | [Crop][Resize] | zoom [100% v] |
+----+----------------------------------------------+--------------+
|Sel |                                              | Layers       |
|Lso |                                              | [x] Text     |
|Wnd |               the picture                    | [x] Layer 2  |
|Brs |                                              | [x] Background|
|Fil |                                              +--------------+
|Txt |                                              | History      |
|... |                                              | Brush        |
+----+----------------------------------------------+ Fill         |
| [prim][sec] palette .....................        |              |
| 640 x 480 | 100% | x 312 y 140 | 1 layer selected                 |
+------------------------------------------------------------------+
```

## From toolkit 2

- **Panels:** dockable panels (tools, layers, history, colours, the IDE's docking too), splitters, the toolbar with
  drop-downs.
- **The canvas:** a canvas widget with scrolling, zoom, and a pointer of the tool's (the API's cursors), with the
  mouse captured during a stroke.
- **Dialogs:** a colour dialog (RGB, HSV, hex, alpha), a font dialog (for the text tool), a preview in each
  adjustment's and effect's dialog, the progress dialog.
- **Helpers:** the undo framework (a history list), background work on the app's threads (effects, saving).

## From API 2.0

- **DRAW:** pixels shown in the desktop's format, fast blits of the visible part.
- **INPUT:** the mouse with its buttons, wheel (zoom with Ctrl), capture while dragging; graphics tablets aren't
  planned.
- **WIN:** cursors (the tool's own: crosshair, brush outline, fill bucket, hand).
- **CLIP:** an image format (32-bit RGBA, with a size), and PNG bytes for apps that want a file format.
- **DND:** pictures dropped on the window open; a layer dragged to Files saves it.
- **THREAD:** effects, decoding and saving off the window's thread, with progress.
- **FS:** long file names, the folder's pictures listed as they load (the filmstrip).

## Libraries

- **Decoding:** stb_image (public domain / MIT), as today.
- **Encoding:** stb_image_write (public domain / MIT) for PNG, BMP and JPEG (PCX GLOS's own).
- **The layered format:** miniz (MIT) for its ZIP.
- **The tools and effects:** GLOS's own.

## CPU floor

- **A 486DX2** for viewing, drawing and the simple adjustments on pictures up to 640x480.
- **A Pentium II** for effects on big pictures, gaussian blur and resampling at large sizes (they run in the
  background with progress, and work, slower, on a 486).
- **MMX inner loops** where they matter (blur, blend modes, resampling): measured on the P4.

## Tests

- **Host:** each tool and effect on test pictures against stored results (pixel exact for the deterministic ones);
  the formats read and written, round-tripped; layers and the layered format; GUI scripts of a drawing session at 8,
  16 and 32 bits.
- **Loop A smoke:** open a 2,000x2,000 JPEG and show it at once (decoded in the background, the first screen
  first); a blur with progress; copy a picture to the clipboard and paste it into a new one.
- **The P4:** effects' timings at real speed; the 486 floor on 86Box's 486.

## Open points

- **Tablets:** none planned; a serial or USB tablet would be a later device class.
