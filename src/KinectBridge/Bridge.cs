using System;
using System.Runtime.InteropServices;
using Microsoft.Win32;

namespace KinectVCam
{
    /// <summary>Liga Kinect -> memória compartilhada, e as funções de ligar/desligar câmeras e "iniciar com o Windows".</summary>
    public sealed class Bridge : IDisposable
    {
        public const string ColorName = "Kinect Cor", IrName = "Kinect Infravermelho";
        const string SettingsKey = @"Software\KinectVCam";
        const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";
        const string RunValue = "KinectVCam";

        readonly KinectCapture capture = new KinectCapture();
        readonly SharedFrameWriter color = new SharedFrameWriter(@"Global\KinectVCam_Color", ColorName,
            KinectCapture.ColorWidth, KinectCapture.ColorHeight, SharedFrameWriter.FormatYuy2);
        readonly SharedFrameWriter ir = new SharedFrameWriter(@"Global\KinectVCam_IR", IrName,
            KinectCapture.IrWidth, KinectCapture.IrHeight, SharedFrameWriter.FormatGray8);
        bool running;

        public KinectCapture Capture { get { return capture; } }

        public Bridge()
        {
            capture.ColorFrameReady += color.Write;
            capture.InfraredFrameReady += ir.Write;
            capture.SignalChanged += s => { if (!s) { color.SetNoSignal(); ir.SetNoSignal(); } };
        }

        public void StartCapture()
        {
            if (running) return;
            running = true;
            capture.Start();
        }

        public void StopCapture()
        {
            if (!running) return;
            running = false;
            capture.Stop();
            color.SetNoSignal();
            ir.SetNoSignal();
        }

        public void Dispose()
        {
            StopCapture();
            color.Dispose();
            ir.Dispose();
        }

        // ---------- câmeras virtuais (KinectVCam.dll) ----------
        [DllImport("KinectVCam.dll")] static extern int KvcSetCameras(int on);
        [DllImport("KinectVCam.dll")] static extern int KvcGetCameraStatus();

        /// <summary>Registra (on) ou remove (off) as duas câmeras e grava a preferência.</summary>
        public static bool SetCameras(bool on)
        {
            int hr = KvcSetCameras(on ? 1 : 0);
            Log.Write((on ? "Ligar" : "Desligar") + " câmeras: " + (hr >= 0 ? "OK" : "ERRO 0x" + hr.ToString("X8")));
            Enabled = on;
            return hr >= 0;
        }

        /// <summary>bit 0 = Kinect Cor listada, bit 1 = Kinect Infravermelho listada.</summary>
        public static int CameraStatus() { return KvcGetCameraStatus(); }

        /// <summary>Preferência "câmeras ligadas" (padrão: ligadas).</summary>
        public static bool Enabled
        {
            get
            {
                using (var k = Registry.CurrentUser.OpenSubKey(SettingsKey))
                    return k == null || Convert.ToInt32(k.GetValue("Enabled", 1)) != 0;
            }
            set
            {
                using (var k = Registry.CurrentUser.CreateSubKey(SettingsKey))
                    k.SetValue("Enabled", value ? 1 : 0, RegistryValueKind.DWord);
            }
        }

        /// <summary>"Iniciar com o Windows" (HKCU\...\Run). Desligado por padrão.</summary>
        public static bool AutoStart
        {
            get
            {
                using (var k = Registry.CurrentUser.OpenSubKey(RunKey))
                    return k != null && k.GetValue(RunValue) != null;
            }
            set
            {
                using (var k = Registry.CurrentUser.CreateSubKey(RunKey))
                {
                    if (value) k.SetValue(RunValue, "\"" + System.Reflection.Assembly.GetExecutingAssembly().Location + "\" --tray");
                    else k.DeleteValue(RunValue, false);
                }
                Log.Write("Iniciar com o Windows: " + (value ? "ATIVADO" : "desativado"));
            }
        }
    }
}
