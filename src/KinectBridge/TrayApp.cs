using System;
using System.Diagnostics;
using System.Drawing;
using System.Threading;
using System.Windows.Forms;

namespace KinectVCam
{
    /// <summary>
    /// Processo em segundo plano da webcam. Por padrão roda SEM ícone (igual ao driver de uma webcam comum):
    /// o Kinect fica desligado e só liga quando algum app abre a câmera "Kinect Cor".
    /// O ícone na bandeja só aparece quando o usuário abre "Kinect Webcam" pelo menu Iniciar (para configurar).
    /// </summary>
    public sealed class TrayApp : ApplicationContext
    {
        /// <summary>Outro "KinectBridge.exe --tray" sinaliza isto para mostrar o ícone do processo que já roda.</summary>
        public const string ShowIconEventName = @"Local\KinectVCamShowIcon";

        readonly Bridge bridge = new Bridge();
        readonly NotifyIcon icon = new NotifyIcon();
        readonly ToolStripMenuItem statusItem = new ToolStripMenuItem("Kinect: iniciando...") { Enabled = false };
        readonly ToolStripMenuItem camerasItem = new ToolStripMenuItem("Câmera ativada");
        readonly ToolStripMenuItem autoStartItem = new ToolStripMenuItem("Iniciar com o Windows");
        readonly System.Windows.Forms.Timer timer = new System.Windows.Forms.Timer { Interval = 250 };
        readonly Icon iconOn = MakeIcon(Color.LimeGreen), iconIdle = MakeIcon(Color.SteelBlue),
                      iconOff = MakeIcon(Color.Gray), iconNoSignal = MakeIcon(Color.OrangeRed);
        readonly EventWaitHandle showIconEvent;
        bool applied;

        public TrayApp(bool showIcon)
        {
            var menu = new ContextMenuStrip();
            menu.Items.Add(statusItem);
            menu.Items.Add(new ToolStripSeparator());
            menu.Items.Add(camerasItem);
            menu.Items.Add(autoStartItem);
            menu.Items.Add(new ToolStripSeparator());
            menu.Items.Add("Abrir log", null, (s, e) => { try { Process.Start("notepad.exe", "\"" + Log.FilePath + "\""); } catch { } });
            menu.Items.Add("Ocultar ícone (continua funcionando)", null, (s, e) => icon.Visible = false);
            menu.Items.Add("Encerrar (a câmera para de funcionar)", null, (s, e) => ExitThread());
            camerasItem.Click += (s, e) => { Bridge.SetCameras(!Bridge.Enabled); Apply(); };
            autoStartItem.Click += (s, e) => { Bridge.AutoStart = !Bridge.AutoStart; Refresh(); };
            icon.ContextMenuStrip = menu;
            icon.Visible = showIcon;

            showIconEvent = new EventWaitHandle(false, EventResetMode.AutoReset, ShowIconEventName);

            timer.Tick += (s, e) =>
            {
                if (showIconEvent.WaitOne(0)) icon.Visible = true;
                if (Bridge.Enabled != applied) Apply();
                bridge.Poll();
                Refresh();
            };
            timer.Start();

            Log.Write("KinectBridge iniciado (" + (showIcon ? "com ícone" : "segundo plano, sem ícone") +
                      "). O Kinect fica desligado até um app abrir a câmera.");
            // Garante que a câmera esteja registrada conforme a preferência (e remove a IR antiga).
            if (Bridge.Enabled) Bridge.SetCameras(true);
            Apply();
        }

        void Apply()
        {
            applied = Bridge.Enabled;
            if (applied) bridge.StartCapture(); else bridge.StopCapture();
            Refresh();
        }

        void Refresh()
        {
            if (!icon.Visible) return;
            bool on = Bridge.Enabled, inUse = bridge.InUse, sig = bridge.Capture.HasSignal;
            camerasItem.Checked = on;
            autoStartItem.Checked = Bridge.AutoStart;
            string st = !on ? "Câmera desativada"
                      : !inUse ? "Kinect desligado (nenhum app usando)"
                      : sig ? "Kinect em uso por um app"
                      : "Kinect ligando / SEM SINAL";
            statusItem.Text = st;
            icon.Text = "Kinect Webcam - " + st;
            icon.Icon = !on ? iconOff : !inUse ? iconIdle : sig ? iconOn : iconNoSignal;
        }

        protected override void ExitThreadCore()
        {
            timer.Stop();
            bridge.Dispose();
            showIconEvent.Dispose();
            icon.Visible = false;
            icon.Dispose();
            Log.Write("KinectBridge encerrado (a câmera continua listada e mostra SEM SINAL).");
            base.ExitThreadCore();
        }

        static Icon MakeIcon(Color c)
        {
            using (var bmp = new Bitmap(16, 16))
            {
                using (var g = Graphics.FromImage(bmp))
                {
                    g.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
                    g.Clear(Color.Transparent);
                    g.FillRectangle(Brushes.Black, 1, 5, 14, 7);
                    using (var b = new SolidBrush(c)) g.FillEllipse(b, 5, 6, 5, 5);
                }
                return Icon.FromHandle(bmp.GetHicon());
            }
        }
    }
}
