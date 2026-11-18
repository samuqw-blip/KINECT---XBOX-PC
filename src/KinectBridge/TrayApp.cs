using System;
using System.Diagnostics;
using System.Drawing;
using System.Windows.Forms;

namespace KinectVCam
{
    /// <summary>Ícone na bandeja: ligar/desligar câmeras, iniciar com o Windows, abrir log, sair.</summary>
    public sealed class TrayApp : ApplicationContext
    {
        readonly Bridge bridge = new Bridge();
        readonly NotifyIcon icon = new NotifyIcon();
        readonly ToolStripMenuItem statusItem = new ToolStripMenuItem("Kinect: iniciando...") { Enabled = false };
        readonly ToolStripMenuItem camerasItem = new ToolStripMenuItem("Câmeras ligadas");
        readonly ToolStripMenuItem autoStartItem = new ToolStripMenuItem("Iniciar com o Windows");
        readonly Timer timer = new Timer { Interval = 1000 };
        readonly Icon iconOn = MakeIcon(Color.LimeGreen), iconOff = MakeIcon(Color.Gray), iconNoSignal = MakeIcon(Color.OrangeRed);
        bool applied;

        public TrayApp()
        {
            var menu = new ContextMenuStrip();
            menu.Items.Add(statusItem);
            menu.Items.Add(new ToolStripSeparator());
            menu.Items.Add(camerasItem);
            menu.Items.Add(autoStartItem);
            menu.Items.Add(new ToolStripSeparator());
            menu.Items.Add("Abrir log", null, (s, e) => { try { Process.Start("notepad.exe", "\"" + Log.FilePath + "\""); } catch { } });
            menu.Items.Add("Sair", null, (s, e) => ExitThread());
            camerasItem.Click += (s, e) => { Bridge.SetCameras(!Bridge.Enabled); Apply(); };
            autoStartItem.Click += (s, e) => { Bridge.AutoStart = !Bridge.AutoStart; Refresh(); };
            icon.ContextMenuStrip = menu;
            icon.Visible = true;
            timer.Tick += (s, e) => { if (Bridge.Enabled != applied) Apply(); Refresh(); };
            timer.Start();

            Log.Write("KinectBridge iniciado (bandeja).");
            // Garante que as câmeras estejam registradas conforme a preferência.
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
            bool on = Bridge.Enabled, sig = bridge.Capture.HasSignal;
            camerasItem.Checked = on;
            autoStartItem.Checked = Bridge.AutoStart;
            string st = !on ? "Câmeras desligadas" : sig ? "Kinect: com imagem" : "Kinect: SEM SINAL (aguardando)";
            statusItem.Text = st;
            icon.Text = "Kinect Webcam - " + st;
            icon.Icon = !on ? iconOff : sig ? iconOn : iconNoSignal;
        }

        protected override void ExitThreadCore()
        {
            timer.Stop();
            bridge.Dispose();
            icon.Visible = false;
            icon.Dispose();
            Log.Write("KinectBridge encerrado (câmeras continuam registradas e mostram SEM SINAL).");
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
