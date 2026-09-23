/* The screen: a 320x200 indexed buffer and the 16-colour palette it is shown
 * through.  Built against desktop SDL2 it is a window with a streaming
 * texture; built against picosdl the buffer goes to the panel as it is and
 * the palette is written straight into the display's colour table. */
#ifndef VIDEO_H
#define VIDEO_H
#include "sp.h"

bool video_open(int scale, Image *screen);
void video_begin_frame(void);                     /* before drawing into screen */
void video_present(const Image *screen, const Palette *pal);
void video_close(void);

#endif
