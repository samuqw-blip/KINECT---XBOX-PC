using System;
using System.Threading;
using Microsoft.Kinect;

namespace KinectVCam
{
    /// <summary>
    /// Lê cor (YUY2 bruto, 1920x1080) e infravermelho (16 bits -> 8 bits cinza, 512x424)
    /// do Kinect v2 com um único MultiSourceFrameReader, numa thread dedicada.
    /// Registra no log quando o sensor some/volta e tenta reabrir sozinho.
    /// </summary>
    public sealed class KinectCapture : IDisposable
    {
        public const int ColorWidth = 1920, ColorHeight = 1080;
        public const int IrWidth = 512, IrHeight = 424;

        /// <summary>Quadro de cor YUY2 (1920*1080*2 bytes). O buffer é reutilizado: copie se precisar guardar.</summary>
        public event Action<byte[]> ColorFrameReady;
        /// <summary>Quadro IR em cinza 8 bits (512*424 bytes). O buffer é reutilizado.</summary>
        public event Action<byte[]> InfraredFrameReady;
        /// <summary>true = recebendo quadros; false = sem sinal.</summary>
        public event Action<bool> SignalChanged;

        public long ColorFrames, InfraredFrames;
        public bool HasSignal { get; private set; }

        readonly byte[] colorBuf = new byte[ColorWidth * ColorHeight * 2];
        readonly byte[] colorTmp = new byte[ColorWidth * ColorHeight * 4];
        readonly ushort[] irRaw = new ushort[IrWidth * IrHeight];
        readonly byte[] irBuf = new byte[IrWidth * IrHeight];
        static readonly byte[] IrLut = BuildIrLut();

        Thread worker;
        volatile bool stop;

        /// <summary>
        /// Normalização do IR igual à dos exemplos do SDK: valor/65535 dividido por (média da cena 0,08 x 3 desvios),
        /// limitado a [0,01; 1], depois 0..255.
        /// </summary>
        static byte[] BuildIrLut()
        {
            var lut = new byte[65536];
            for (int i = 0; i < lut.Length; i++)
            {
                double v = (i / 65535.0) / (0.08 * 3.0);
                if (v < 0.01) v = 0.01;
                if (v > 1.0) v = 1.0;
                lut[i] = (byte)(v * 255.0 + 0.5);
            }
            return lut;
        }

        public void Start()
        {
            if (worker != null) return;
            stop = false;
            worker = new Thread(Run) { IsBackground = true, Name = "KinectCapture" };
            worker.Start();
        }

        public void Stop()
        {
            stop = true;
            var t = worker;
            worker = null;
            if (t != null && !t.Join(5000)) Log.Write("Aviso: thread do Kinect não terminou em 5 s.");
        }

        public void Dispose() { Stop(); }

        void SetSignal(bool value, string reason)
        {
            if (HasSignal == value) return;
            HasSignal = value;
            Log.Write(value ? "Kinect: recebendo imagem (" + reason + ")." : "Kinect: SEM SINAL (" + reason + ").");
            var h = SignalChanged;
            if (h != null) h(value);
        }

        void Run()
        {
            KinectSensor sensor = null;
            MultiSourceFrameReader reader = null;
            long lastFrameMs = Environment.TickCount;
            long lastReopenMs = Environment.TickCount;
            bool lastAvailable = false;

            try
            {
                sensor = KinectSensor.GetDefault();
                if (sensor == null)
                {
                    Log.Write("ERRO: KinectSensor.GetDefault() retornou null (runtime do Kinect instalado?).");
                    return;
                }
                sensor.Open();
                reader = sensor.OpenMultiSourceFrameReader(FrameSourceTypes.Color | FrameSourceTypes.Infrared);
                Log.Write("Kinect: sensor aberto, aguardando dispositivo...");

                while (!stop)
                {
                    bool available = sensor.IsAvailable;
                    if (available != lastAvailable)
                    {
                        Log.Write(available
                            ? "Kinect: dispositivo CONECTADO (IsAvailable=true)."
                            : "Kinect: dispositivo DESCONECTADO (IsAvailable=false) - a luz apagou ou ele sumiu do Gerenciador de Dispositivos.");
                        lastAvailable = available;
                        lastFrameMs = Environment.TickCount;
                    }

                    bool got = false;
                    MultiSourceFrame multi = null;
                    try { multi = reader.AcquireLatestFrame(); }
                    catch (Exception ex) { Log.Write("Kinect: erro ao adquirir quadro: " + ex.Message); }

                    if (multi != null)
                    {
                        got |= HandleColor(multi);
                        got |= HandleInfrared(multi);
                    }

                    int now = Environment.TickCount;
                    if (got)
                    {
                        lastFrameMs = now;
                        SetSignal(true, "quadros chegando");
                    }
                    else
                    {
                        if (now - lastFrameMs > 1500) SetSignal(false, available ? "sem quadros há 1,5 s" : "dispositivo ausente");

                        // Reconexão: sem quadros por 5 s -> reabre leitor e sensor (no máximo a cada 5 s).
                        if (now - lastFrameMs > 5000 && now - lastReopenMs > 5000)
                        {
                            lastReopenMs = now;
                            Log.Write("Kinect: sem quadros há " + ((now - lastFrameMs) / 1000) + " s, reabrindo sensor e leitor...");
                            try
                            {
                                reader.Dispose();
                                sensor.Close();
                                Thread.Sleep(500);
                                sensor.Open();
                                reader = sensor.OpenMultiSourceFrameReader(FrameSourceTypes.Color | FrameSourceTypes.Infrared);
                            }
                            catch (Exception ex) { Log.Write("Kinect: falha ao reabrir: " + ex.Message); }
                        }
                        Thread.Sleep(5);
                    }
                }
            }
            catch (Exception ex)
            {
                Log.Write("ERRO fatal na thread do Kinect: " + ex);
            }
            finally
            {
                try { if (reader != null) reader.Dispose(); } catch { }
                try { if (sensor != null) sensor.Close(); } catch { }
                SetSignal(false, "captura parada");
                Log.Write("Kinect: captura parada.");
            }
        }

        bool HandleColor(MultiSourceFrame multi)
        {
            using (ColorFrame f = multi.ColorFrameReference.AcquireFrame())
            {
                if (f == null) return false;
                var d = f.FrameDescription;
                if (d.Width != ColorWidth || d.Height != ColorHeight) return false;
                if (f.RawColorImageFormat == ColorImageFormat.Yuy2)
                {
                    f.CopyRawFrameDataToArray(colorBuf);
                }
                else
                {
                    // Não esperado no Kinect v2, mas por segurança converte para YUY2.
                    f.CopyConvertedFrameDataToArray(colorTmp, ColorImageFormat.Bgra);
                    BgraToYuy2(colorTmp, colorBuf, ColorWidth, ColorHeight);
                }
                ColorFrames++;
                var h = ColorFrameReady;
                if (h != null) h(colorBuf);
                return true;
            }
        }

        bool HandleInfrared(MultiSourceFrame multi)
        {
            using (InfraredFrame f = multi.InfraredFrameReference.AcquireFrame())
            {
                if (f == null) return false;
                f.CopyFrameDataToArray(irRaw);
                for (int i = 0; i < irRaw.Length; i++) irBuf[i] = IrLut[irRaw[i]];
                InfraredFrames++;
                var h = InfraredFrameReady;
                if (h != null) h(irBuf);
                return true;
            }
        }

        static void BgraToYuy2(byte[] bgra, byte[] yuy2, int w, int h)
        {
            int n = w * h / 2;
            for (int p = 0; p < n; p++)
            {
                int s = p * 8, o = p * 4;
                int b0 = bgra[s], g0 = bgra[s + 1], r0 = bgra[s + 2];
                int b1 = bgra[s + 4], g1 = bgra[s + 5], r1 = bgra[s + 6];
                int y0 = ((66 * r0 + 129 * g0 + 25 * b0 + 128) >> 8) + 16;
                int y1 = ((66 * r1 + 129 * g1 + 25 * b1 + 128) >> 8) + 16;
                int r = (r0 + r1) >> 1, g = (g0 + g1) >> 1, b = (b0 + b1) >> 1;
                int u = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
                int v = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
                yuy2[o] = (byte)y0; yuy2[o + 1] = (byte)u; yuy2[o + 2] = (byte)y1; yuy2[o + 3] = (byte)v;
            }
        }
    }
}
