using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media.Imaging;
using Avalonia.Platform;

namespace NoorEditorSpike;

/// <summary>
/// Captures one of this app's own windows, including the native Vulkan child window that
/// Avalonia's renderer cannot see. Uses PrintWindow(PW_RENDERFULLCONTENT), which asks the
/// window to render itself, so it never copies pixels from other applications that happen
/// to overlap it (a plain screen BitBlt would). Windows-only.
/// </summary>
internal static class ScreenCapture
{
    private const uint PwRenderFullContent = 0x00000002;

    public static bool CaptureWindow(Window window, string path)
    {
        var hwnd = window.TryGetPlatformHandle()?.Handle ?? IntPtr.Zero;
        if (hwnd == IntPtr.Zero || !GetWindowRect(hwnd, out var r)) return false;

        int w = r.Right - r.Left, h = r.Bottom - r.Top;
        if (w <= 0 || h <= 0) return false;

        var screenDc = GetDC(IntPtr.Zero);
        var memDc = CreateCompatibleDC(screenDc);
        var bmi = new BITMAPINFO { biSize = Marshal.SizeOf<BITMAPINFO>(), biWidth = w, biHeight = -h, biPlanes = 1, biBitCount = 32 };
        var dib = CreateDIBSection(screenDc, ref bmi, 0, out var bits, IntPtr.Zero, 0);
        var old = SelectObject(memDc, dib);
        var ok = PrintWindow(hwnd, memDc, PwRenderFullContent);

        if (ok)
        {
            using var bitmap = new WriteableBitmap(new PixelSize(w, h), new Vector(96, 96), PixelFormat.Bgra8888, AlphaFormat.Opaque);
            using (var fb = bitmap.Lock())
            {
                var row = new byte[w * 4];
                for (var y = 0; y < h; y++)
                {
                    Marshal.Copy(bits + y * w * 4, row, 0, row.Length);
                    for (var x = 3; x < row.Length; x += 4) row[x] = 255;  // GDI leaves alpha undefined
                    Marshal.Copy(row, 0, fb.Address + y * fb.RowBytes, row.Length);
                }
            }
#pragma warning disable CS0618 // Save(string) is obsolete in Avalonia 12; PNG default is what we want
            bitmap.Save(path);
#pragma warning restore CS0618
        }

        SelectObject(memDc, old);
        DeleteObject(dib);
        DeleteDC(memDc);
        ReleaseDC(IntPtr.Zero, screenDc);
        return ok;
    }

    [StructLayout(LayoutKind.Sequential)] private struct RECT { public int Left, Top, Right, Bottom; }

    [StructLayout(LayoutKind.Sequential)]
    private struct BITMAPINFO
    {
        public int biSize, biWidth, biHeight;
        public short biPlanes, biBitCount;
        public int biCompression, biSizeImage, biXPelsPerMeter, biYPelsPerMeter, biClrUsed, biClrImportant;
        public int bmiColors;
    }

    [DllImport("user32.dll")] private static extern bool PrintWindow(IntPtr hwnd, IntPtr dc, uint flags);
    [DllImport("user32.dll")] private static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")] private static extern IntPtr GetDC(IntPtr hwnd);
    [DllImport("user32.dll")] private static extern int ReleaseDC(IntPtr hwnd, IntPtr dc);
    [DllImport("gdi32.dll")] private static extern IntPtr CreateCompatibleDC(IntPtr dc);
    [DllImport("gdi32.dll")] private static extern IntPtr CreateDIBSection(IntPtr dc, ref BITMAPINFO bmi, uint usage, out IntPtr bits, IntPtr section, uint offset);
    [DllImport("gdi32.dll")] private static extern IntPtr SelectObject(IntPtr dc, IntPtr obj);
    [DllImport("gdi32.dll")] private static extern bool DeleteObject(IntPtr obj);
    [DllImport("gdi32.dll")] private static extern bool DeleteDC(IntPtr dc);
}
