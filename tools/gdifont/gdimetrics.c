/* gdimetrics: print the GDI text metrics stock DreamSeeker lays maptext out with (render/dsmaptext.h).
 *
 * DreamSeeker measures MeasureText()/maptext with CreateFont(lfHeight = -em) + GetTextMetrics + the advance widths.
 * It prints, per face and em, what GDI resolved: tmHeight/tmAscent/tmDescent/tmInternalLeading, the face selected, and
 * the extent of a probe string. -widths also prints the advance widths that render/dsmaptext.cpp infers for the
 * Small Fonts strikes (5pt, 6pt).
 *
 *   i686-w64-mingw32-gcc gdimetrics.c -o gdimetrics.exe -lgdi32
 *   gdimetrics.exe [-widths] > gdi.txt        (on Windows, or under Wine)
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char** argv) {
    int widths = argc > 1 && strcmp(argv[1], "-widths") == 0;
    HDC dc = CreateCompatibleDC(NULL);
    const char* fams[] = {"Small Fonts", "MS Sans Serif", "Courier",         "Arial",
                          "Verdana",     "Tahoma",        "Times New Roman", "Courier New"};
    for (int fi = 0; fi < (int)(sizeof fams / sizeof fams[0]); fi++) {
        for (int em = 4; em <= 48; em++) {
            HFONT f = CreateFontA(-em, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, fams[fi]);
            HGDIOBJ old = SelectObject(dc, f);
            TEXTMETRICA tm;
            char face[64];
            SIZE sz;
            GetTextMetricsA(dc, &tm);
            GetTextFaceA(dc, sizeof face, face);
            GetTextExtentPoint32A(dc, "The quick brown fox", 19, &sz);
            printf("%s\t%d\tH=%ld\tasc=%ld\tdesc=%ld\til=%ld\tel=%ld\tw=%ld\tface=%s\n", fams[fi], em, tm.tmHeight,
                   tm.tmAscent, tm.tmDescent, tm.tmInternalLeading, tm.tmExternalLeading, sz.cx, face);
            if (widths) {
                INT w[224];
                GetCharWidth32A(dc, 32, 255, w);
                printf("  adv32-255:");
                for (int i = 0; i < 224; i++) printf(" %d", w[i]);
                printf("\n");
            }
            SelectObject(dc, old);
            DeleteObject(f);
        }
    }
    return 0;
}
