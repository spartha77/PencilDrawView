// =============================================================================
// CadRenderer.WPF / NoPickCursor.cs
//
// The "cannot pick this" mouse pointer: a red disc with a white minus bar.
// It is shown while the mouse is over an entity whose type is filtered out.
//
// The picture is computed here pixel by pixel (no image file to ship) and
// turned into a Win32 cursor with CreateIconIndirect.
// =============================================================================
using System;
using System.Runtime.InteropServices;

namespace CadRenderer.WPF
{
    internal static class NoPickCursor
    {
        public const int Size = 32;                 // cursor bitmap is Size x Size pixels

        private static IntPtr s_handle;

        /// <summary>Win32 cursor handle (HCURSOR), created on first use.</summary>
        public static IntPtr Handle
        {
            get
            {
                if (s_handle == IntPtr.Zero) s_handle = Create();
                return s_handle;
            }
        }

        /// <summary>
        /// The cursor image: 4 bytes per pixel in B, G, R, A order, top row first.
        /// Each pixel is tested at 4 x 4 sample positions so the round edge is smooth.
        /// </summary>
        internal static byte[] BuildPixels()
        {
            const float center = Size / 2f;         // disc centre = cursor hot spot
            const float discRadius = 10f;           // red disc
            const float rimRadius = 11.5f;          // thin white rim so it shows on any background
            const float barHalfWidth = 6f, barHalfHeight = 2f;   // the white "minus"
            const int samples = 4;

            var pixels = new byte[Size * Size * 4];
            for (int y = 0; y < Size; y++)
            {
                for (int x = 0; x < Size; x++)
                {
                    float red = 0, white = 0;       // number of samples of each colour
                    for (int sy = 0; sy < samples; sy++)
                    {
                        for (int sx = 0; sx < samples; sx++)
                        {
                            float px = x + (sx + 0.5f) / samples - center;
                            float py = y + (sy + 0.5f) / samples - center;
                            float distance = MathF.Sqrt(px * px + py * py);
                            bool onBar = MathF.Abs(px) <= barHalfWidth && MathF.Abs(py) <= barHalfHeight;
                            if (distance > rimRadius) continue;                 // outside: transparent
                            if (onBar || distance > discRadius) white++;
                            else red++;
                        }
                    }

                    float total = samples * samples;
                    float covered = red + white;
                    if (covered == 0) continue;

                    // Average colour of the covered samples; alpha = covered share.
                    int i = (y * Size + x) * 4;
                    pixels[i]     = (byte)((white * 255 + red * 35) / covered);    // blue
                    pixels[i + 1] = (byte)((white * 255 + red * 30) / covered);    // green
                    pixels[i + 2] = (byte)((white * 255 + red * 215) / covered);   // red
                    pixels[i + 3] = (byte)(255 * covered / total);                 // alpha
                }
            }
            return pixels;
        }

        private static IntPtr Create()
        {
            // A cursor is made of a colour bitmap (with alpha) and a 1-bit mask.
            // With an alpha channel present the mask is not used for drawing, but
            // it must exist; all zeros is the conventional content.
            IntPtr color = CreateBitmap(Size, Size, 1, 32, BuildPixels());
            IntPtr mask = CreateBitmap(Size, Size, 1, 1, new byte[Size * Size / 8]);

            var info = new ICONINFO
            {
                fIcon = false,                      // false = cursor (has a hot spot)
                xHotspot = Size / 2,
                yHotspot = Size / 2,
                hbmMask = mask,
                hbmColor = color
            };
            IntPtr cursor = CreateIconIndirect(ref info);

            // The cursor keeps its own copies of the bitmaps.
            DeleteObject(color);
            DeleteObject(mask);

            // Should creation ever fail, fall back to Windows' standard "no" cursor.
            const int IDC_NO = 32648;
            return cursor != IntPtr.Zero ? cursor : LoadCursor(IntPtr.Zero, new IntPtr(IDC_NO));
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct ICONINFO
        {
            [MarshalAs(UnmanagedType.Bool)] public bool fIcon;
            public int xHotspot;
            public int yHotspot;
            public IntPtr hbmMask;
            public IntPtr hbmColor;
        }

        [DllImport("gdi32.dll")]
        private static extern IntPtr CreateBitmap(int width, int height, uint planes, uint bitsPerPixel, byte[] bits);

        [DllImport("gdi32.dll")]
        private static extern bool DeleteObject(IntPtr obj);

        [DllImport("user32.dll")]
        private static extern IntPtr CreateIconIndirect(ref ICONINFO info);

        [DllImport("user32.dll", EntryPoint = "LoadCursorW")]
        private static extern IntPtr LoadCursor(IntPtr instance, IntPtr cursorId);
    }
}
