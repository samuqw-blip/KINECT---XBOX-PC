using System;
using System.Runtime.InteropServices;
using System.Threading;

namespace KinectVCam
{
    /// <summary>
    /// Escreve quadros na memória compartilhada lida pela KinectVCam.dll (dentro do serviço Frame Server).
    /// A memória é criada pela DLL quando algum app abre a câmera; aqui só abrimos (tentando a cada 250 ms).
    /// Layout igual ao KvcHeader de src\KinectVCam\KinectVCam.h: cabeçalho de 64 bytes + 3 quadros.
    /// Ao copiar, desespelha a imagem na horizontal (o Kinect entrega espelhado; webcams normais não).
    /// </summary>
    public sealed class SharedFrameWriter : IDisposable
    {
        public const int FormatYuy2 = 1, FormatGray8 = 2;
        const uint Magic = 0x3143564B;
        const int HeaderSize = 64, Slots = 3;

        readonly string name, label;
        readonly int width, height, format, frameBytes;
        IntPtr map = IntPtr.Zero, view = IntPtr.Zero;
        int lastTry = Environment.TickCount - 10000;

        public SharedFrameWriter(string name, string label, int width, int height, int format)
        {
            this.name = name; this.label = label;
            this.width = width; this.height = height; this.format = format;
            frameBytes = format == FormatYuy2 ? width * height * 2 : width * height;
        }

        public bool Connected { get { return view != IntPtr.Zero; } }

        void EnsureOpen()
        {
            if (view != IntPtr.Zero) return;
            int now = Environment.TickCount;
            if (now - lastTry < 250) return;
            lastTry = now;
            IntPtr h = OpenFileMapping(FILE_MAP_WRITE | FILE_MAP_READ, false, name);
            if (h == IntPtr.Zero) return; // nenhum app abriu a câmera ainda
            IntPtr v = MapViewOfFile(h, FILE_MAP_WRITE | FILE_MAP_READ, 0, 0, (UIntPtr)(uint)(HeaderSize + Slots * frameBytes));
            if (v == IntPtr.Zero)
            {
                Log.Write(label + ": MapViewOfFile falhou (" + Marshal.GetLastWin32Error() + ").");
                CloseHandle(h);
                return;
            }
            map = h; view = v;
            Log.Write(label + ": conectado à câmera virtual (um app abriu a câmera).");
        }

        public unsafe void Write(byte[] data)
        {
            EnsureOpen();
            if (view == IntPtr.Zero) return;
            byte* b = (byte*)view;
            int latest = *(int*)(b + 20);
            int slot = (latest < 0 || latest >= Slots) ? 0 : (latest + 1) % Slots;
            byte* dst = b + HeaderSize + (long)slot * frameBytes;
            fixed (byte* src = data)
            {
                if (format == FormatYuy2) MirrorYuy2(src, dst, width, height);
                else MirrorGray(src, dst, width, height);
            }
            *(uint*)(b + 0) = Magic;
            *(uint*)(b + 4) = (uint)width;
            *(uint*)(b + 8) = (uint)height;
            *(uint*)(b + 12) = (uint)format;
            *(uint*)(b + 16) = (uint)frameBytes;
            Thread.MemoryBarrier();
            *(int*)(b + 20) = slot;
            *(long*)(b + 24) = *(long*)(b + 24) + 1;
            *(long*)(b + 32) = (long)GetTickCount64();
            *(uint*)(b + 40) = 1;
        }

        /// <summary>
        /// true se algum app está pedindo quadros agora (a DLL atualiza readerTick, offset 44, a cada quadro pedido).
        /// É isto que decide ligar/desligar o Kinect, como uma webcam de verdade.
        /// </summary>
        public unsafe bool IsInUse(int windowMs)
        {
            EnsureOpen();
            if (view == IntPtr.Zero) return false;
            uint tick = *(uint*)((byte*)view + 44);
            return tick != 0 && unchecked((uint)GetTickCount64() - tick) < (uint)windowMs;
        }

        /// <summary>Solta a memória compartilhada (ela some quando o app que abriu a câmera fechar).</summary>
        public void Disconnect()
        {
            if (view == IntPtr.Zero) return;
            Dispose();
            Log.Write(label + ": desconectado da câmera virtual (nenhum app usando).");
        }

        public unsafe void SetNoSignal()
        {
            if (view == IntPtr.Zero) return;
            *(uint*)((byte*)view + 40) = 0;
        }

        static unsafe void MirrorYuy2(byte* src, byte* dst, int w, int h)
        {
            int rowBytes = w * 2, macros = w / 2;
            for (int y = 0; y < h; y++)
            {
                byte* s = src + (long)y * rowBytes;
                byte* d = dst + (long)y * rowBytes + (macros - 1) * 4;
                for (int m = 0; m < macros; m++, s += 4, d -= 4)
                {
                    d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = s[3];
                }
            }
        }

        static unsafe void MirrorGray(byte* src, byte* dst, int w, int h)
        {
            for (int y = 0; y < h; y++)
            {
                byte* s = src + (long)y * w;
                byte* d = dst + (long)y * w + w - 1;
                for (int x = 0; x < w; x++) *d-- = *s++;
            }
        }

        public void Dispose()
        {
            if (view != IntPtr.Zero) { SetNoSignal(); UnmapViewOfFile(view); view = IntPtr.Zero; }
            if (map != IntPtr.Zero) { CloseHandle(map); map = IntPtr.Zero; }
        }

        const uint FILE_MAP_WRITE = 0x2, FILE_MAP_READ = 0x4;
        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        static extern IntPtr OpenFileMapping(uint access, bool inherit, string name);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern IntPtr MapViewOfFile(IntPtr h, uint access, uint offHigh, uint offLow, UIntPtr bytes);
        [DllImport("kernel32.dll")]
        static extern bool UnmapViewOfFile(IntPtr p);
        [DllImport("kernel32.dll")]
        static extern bool CloseHandle(IntPtr h);
        [DllImport("kernel32.dll")]
        static extern ulong GetTickCount64();
    }
}
