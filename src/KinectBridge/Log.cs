using System;
using System.IO;

namespace KinectVCam
{
    /// <summary>Log simples em %LOCALAPPDATA%\KinectVCam\kinectvcam.log (e no console, se houver).</summary>
    public static class Log
    {
        static readonly object Gate = new object();

        public static readonly string Folder =
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "KinectVCam");

        public static readonly string FilePath = Path.Combine(Folder, "kinectvcam.log");

        public static void Write(string message)
        {
            string line = DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff") + "  " + message;
            lock (Gate)
            {
                try
                {
                    Directory.CreateDirectory(Folder);
                    var fi = new FileInfo(FilePath);
                    if (fi.Exists && fi.Length > 5 * 1024 * 1024)
                    {
                        File.Copy(FilePath, FilePath + ".old", true);
                        File.Delete(FilePath);
                    }
                    File.AppendAllText(FilePath, line + Environment.NewLine);
                }
                catch { /* log nunca derruba o app */ }
                try { Console.WriteLine(line); } catch { }
            }
        }
    }
}
