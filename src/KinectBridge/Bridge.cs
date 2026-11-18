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

        // Só cor: o emissor infravermelho não é ligado à toa.
        readonly KinectCapture capture = new KinectCapture { Sources = Microsoft.Kinect.FrameSourceTypes.Color };
        readonly SharedFrameWriter color = new SharedFrameWriter(@"Global\KinectVCam_Color", ColorName,
            KinectCapture.ColorWidth, KinectCapture.ColorHeight, SharedFrameWriter.FormatYuy2);

        // Igual a uma webcam: o Kinect só liga quando um app pede imagem e desliga logo depois que ele para.
        const int InUseWindowMs = 2000;   // a DLL marca a cada quadro pedido; sem marca há 2 s = ninguém usando
        const int IdleOffDelayMs = 3000;  // espera um pouco antes de desligar (troca de resolução, reabertura)
        bool armed, running;
        int lastUseMs;

        public KinectCapture Capture { get { return capture; } }
        /// <summary>true enquanto algum app está usando a câmera (e o Kinect está ligado).</summary>
        public bool InUse { get { return running; } }

        public Bridge()
        {
            capture.ColorFrameReady += color.Write;
            capture.SignalChanged += s => { if (!s) color.SetNoSignal(); };
        }

        /// <summary>Câmera disponível: o Kinect fica DESLIGADO até algum app abrir a câmera.</summary>
        public void StartCapture() { armed = true; }

        /// <summary>Câmera indisponível: desliga o Kinect se estiver ligado.</summary>
        public void StopCapture()
        {
            armed = false;
            SensorOff("câmera desativada");
        }

        /// <summary>Chamar periodicamente (~250 ms): liga/desliga o sensor conforme algum app usa a câmera.</summary>
        public void Poll()
        {
            if (!armed) return;
            int now = Environment.TickCount;
            if (color.IsInUse(InUseWindowMs))
            {
                lastUseMs = now;
                if (!running)
                {
                    running = true;
                    Log.Write("Um app abriu a câmera: ligando o Kinect.");
                    capture.Start();
                }
            }
            else if (running && now - lastUseMs > IdleOffDelayMs)
            {
                SensorOff("nenhum app usando a câmera");
            }
        }

        void SensorOff(string reason)
        {
            if (!running) return;
            running = false;
            capture.Stop();
            color.SetNoSignal();
            color.Disconnect();
            Log.Write("Kinect desligado (" + reason + ").");
        }

        public void Dispose()
        {
            StopCapture();
            color.Dispose();
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
                    if (value) k.SetValue(RunValue, "\"" + System.Reflection.Assembly.GetExecutingAssembly().Location + "\" --background");
                    else k.DeleteValue(RunValue, false);
                }
                Log.Write("Iniciar com o Windows: " + (value ? "ATIVADO" : "desativado"));
            }
        }
    }
}
