using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;

namespace KinectVCam
{
    static class Program
    {
        [STAThread]
        static int Main(string[] args)
        {
            string cmd = args.Length > 0 ? args[0].ToLowerInvariant() : "--tray";
            switch (cmd)
            {
                case "--test": return TestCapture(args.Length > 1 ? int.Parse(args[1]) : 10);
                case "--on": return Bridge.SetCameras(true) ? 0 : 1;
                case "--off": return Bridge.SetCameras(false) ? 0 : 1;
                case "--status":
                    int st = Bridge.CameraStatus();
                    Console.WriteLine("Preferência: câmeras " + (Bridge.Enabled ? "LIGADAS" : "DESLIGADAS"));
                    Console.WriteLine(Bridge.ColorName + ": " + ((st & 1) != 0 ? "listada no Windows" : "não listada"));
                    Console.WriteLine(Bridge.IrName + ": " + ((st & 2) != 0 ? "listada no Windows" : "não listada"));
                    Console.WriteLine("Iniciar com o Windows: " + (Bridge.AutoStart ? "sim" : "não"));
                    return 0;
                case "--autostart":
                    Bridge.AutoStart = args.Length > 1 && args[1].ToLowerInvariant() == "on";
                    return 0;
                case "--tray":
                    bool created;
                    using (var mutex = new Mutex(true, @"Local\KinectVCamTray", out created))
                    {
                        if (!created) { Log.Write("KinectBridge já está rodando."); return 0; }
                        System.Windows.Forms.Application.EnableVisualStyles();
                        System.Windows.Forms.Application.Run(new TrayApp());
                    }
                    return 0;
            }
            Console.WriteLine("Uso: KinectBridge.exe [--tray | --on | --off | --status | --autostart on|off | --test [segundos]]");
            return 1;
        }

        /// <summary>Passo 2: lê cor + IR por N segundos, mostra FPS e salva um PNG de cada.</summary>
        static int TestCapture(int seconds)
        {
            Log.Write("=== Teste de captura (" + seconds + " s) ===");
            byte[] lastColor = null, lastIr = null;
            using (var cap = new KinectCapture())
            {
                cap.ColorFrameReady += b => { lastColor = (byte[])b.Clone(); };
                cap.InfraredFrameReady += b => { lastIr = (byte[])b.Clone(); };
                cap.Start();
                long c0 = 0, i0 = 0;
                for (int s = 1; s <= seconds; s++)
                {
                    Thread.Sleep(1000);
                    long c = cap.ColorFrames, i = cap.InfraredFrames;
                    Log.Write(string.Format("t={0,2}s  cor {1,2} fps  IR {2,2} fps  (total cor={3} IR={4})", s, c - c0, i - i0, c, i));
                    c0 = c; i0 = i;
                }
                cap.Stop();
                Log.Write(string.Format("Média: cor {0:0.0} fps, IR {1:0.0} fps", cap.ColorFrames / (double)seconds, cap.InfraredFrames / (double)seconds));
            }
            string dir = Path.Combine(Log.Folder, "test");
            Directory.CreateDirectory(dir);
            if (lastColor != null) SaveYuy2(lastColor, 1920, 1080, Path.Combine(dir, "cor.png"));
            if (lastIr != null) SaveGray(lastIr, 512, 424, Path.Combine(dir, "ir.png"));
            Log.Write("Imagens salvas em " + dir);
            return (lastColor != null && lastIr != null) ? 0 : 2;
        }

        static byte Clamp(int v) { return (byte)(v < 0 ? 0 : v > 255 ? 255 : v); }

        static void SaveYuy2(byte[] yuy2, int w, int h, string path)
        {
            var bgr = new byte[w * h * 3];
            for (int p = 0; p < w * h / 2; p++)
            {
                int s = p * 4;
                int u = yuy2[s + 1] - 128, v = yuy2[s + 3] - 128;
                for (int k = 0; k < 2; k++)
                {
                    int c = 298 * (yuy2[s + k * 2] - 16);
                    int o = (p * 2 + k) * 3;
                    bgr[o] = Clamp((c + 516 * u + 128) >> 8);
                    bgr[o + 1] = Clamp((c - 100 * u - 208 * v + 128) >> 8);
                    bgr[o + 2] = Clamp((c + 409 * v + 128) >> 8);
                }
            }
            SaveRaw(bgr, w, h, w * 3, PixelFormat.Format24bppRgb, path);
        }

        static void SaveGray(byte[] gray, int w, int h, string path)
        {
            var bgr = new byte[w * h * 3];
            for (int i = 0; i < w * h; i++) bgr[i * 3] = bgr[i * 3 + 1] = bgr[i * 3 + 2] = gray[i];
            SaveRaw(bgr, w, h, w * 3, PixelFormat.Format24bppRgb, path);
        }

        static void SaveRaw(byte[] data, int w, int h, int srcStride, PixelFormat fmt, string path)
        {
            using (var bmp = new Bitmap(w, h, fmt))
            {
                var bd = bmp.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.WriteOnly, fmt);
                for (int y = 0; y < h; y++)
                    Marshal.Copy(data, y * srcStride, bd.Scan0 + y * bd.Stride, srcStride);
                bmp.UnlockBits(bd);
                bmp.Save(path, ImageFormat.Png);
            }
        }
    }
}
