# Kinect Webcam

Transforma o **Kinect v2 (Xbox One / Kinect for Windows v2)** em **webcam no Windows 11**.
Cria a câmera **Kinect Cor** (1920x1080, também 1280x720, 30 fps), que aparece em qualquer programa.

Funciona **igual a uma webcam comum**:
- O **Kinect fica desligado** (luz apagada) até algum programa abrir a câmera. Ele liga na hora e desliga
  uns 3 segundos depois que o programa fecha a câmera.
- **Não fica ícone perto do relógio.** Um pequeno ajudante roda escondido em segundo plano, como um driver.
- Passa pela **privacidade de câmera do Windows** (Configurações → Privacidade → Câmera): dá para bloquear,
  ver quais programas usaram a câmera, e o Windows mostra o aviso de "câmera em uso" como em qualquer webcam.

Funciona no app Câmera do Windows, Teams, OBS, navegadores (Chrome/Edge) e outros programas de vídeo.

## ⚠️ Aviso: o Kinect foi descontinuado pela Microsoft

A Microsoft **encerrou o Kinect**:
- a fabricação do Kinect parou em **2017**, e o adaptador do Kinect para PC deixou de ser vendido em **2018**;
- o **Kinect for Windows SDK/Runtime 2.0** é de **2014** e não recebe mais atualizações nem suporte;
- o sucessor (Azure Kinect DK) também foi descontinuado.

Por isso, **a Microsoft não tem driver nem webcam oficial do Kinect para o Windows 11**. Este projeto usa o
runtime antigo (que ainda instala e funciona no Windows 11) e cria as câmeras por conta própria.
Como o driver é antigo, o Kinect pode ficar instável em PCs atuais. Veja os
[ajustes necessários](#ajustes-necessários-para-o-kinect-ficar-estável) abaixo.

## Requisitos

| O que | Detalhe |
|---|---|
| **Windows 11** | Versão 21H2 ou mais nova (build 22000+). **Não funciona no Windows 10**, que não tem a API de câmera virtual. |
| **Kinect v2** | Sensor do Xbox One ou Kinect for Windows v2 |
| **Adaptador do Kinect para PC** | O Kinect do Xbox One precisa do adaptador oficial (fonte + USB 3.0) |
| **Porta USB 3.0** | De preferência ligada direto na placa-mãe |
| **Kinect for Windows Runtime 2.0** | Driver gratuito da Microsoft: [baixar aqui](https://www.microsoft.com/download/details.aspx?id=44559). O instalador avisa se estiver faltando. |
| .NET Framework 4.8 | Já vem com o Windows 11 |

Para conferir se o Kinect está funcionando antes de instalar: o **Gerenciador de Dispositivos** deve mostrar
"Xbox NUI Sensor" e "WDF KinectSensor Interface 0" sem erro.

## Ajustes necessários para o Kinect ficar estável

### O problema
Sem estes ajustes, o Kinect pode **cair sozinho**: a luz branca apaga, ele **some do Gerenciador de
Dispositivos** e volta segundos depois, ou não volta até ser desconectado. Isso acontece por dois motivos:

1. **Economia de energia da USB:** o Windows desliga a porta USB para economizar energia, e o driver antigo do Kinect não se recupera direito.
2. **Aprimoramentos de áudio do microfone do Kinect:** o Windows 11 aplica efeitos de áudio ao "Grupo de microfones (Xbox NUI Sensor)".
   Com o driver antigo, isso pode travar o dispositivo inteiro, inclusive as câmeras.

Depois de desconectar e reconectar, o microfone do Kinect às vezes aparece com **status "Erro"** no Gerenciador de
Dispositivos. **Isso não afeta as câmeras.** Se precisar do microfone, desconecte e reconecte o Kinect de novo.

### Como ajustar (faça uma vez)
1. **Desligar os aprimoramentos de áudio do microfone do Kinect**
   Configurações → Sistema → Som → Entrada → **Grupo de microfones (Xbox NUI Sensor)** →
   **Aprimoramentos de áudio: Desativado**.
2. **Desligar a suspensão seletiva da USB**
   Painel de Controle → Opções de Energia → *Alterar configurações do plano* → *Alterar configurações de energia avançadas* →
   **Configurações de USB → Configuração de suspensão seletiva USB: Desabilitado** (na bateria e na tomada).
3. **Impedir o Windows de desligar a USB do Kinect**
   Gerenciador de Dispositivos → *Controladores USB (barramento serial universal)* → para cada **Hub raiz USB (USB 3.0)** e
   **Controlador de host extensível USB (xHCI)**: Propriedades → *Gerenciamento de energia* → desmarque
   **"O computador pode desligar o dispositivo para economizar energia"**.
4. Use uma **porta USB 3.0 direto na placa-mãe** (atrás do PC), não um hub nem uma porta frontal.

O Kinect Webcam também se protege: se o Kinect cair, as câmeras continuam registradas, mostram **"SEM SINAL"**,
a queda fica registrada no log e a imagem **volta sozinha** quando o Kinect reconecta.

## Instalação

1. Instale o **Kinect for Windows Runtime 2.0** (link acima), conecte o Kinect e faça os [ajustes acima](#ajustes-necessários-para-o-kinect-ficar-estável).
2. Baixe o `KinectWebcam-Setup-x.y.z.exe` na página **[Releases](../../releases)** deste repositório.
3. Execute e siga os passos. O instalador pede permissão de administrador uma vez.
   - O Windows pode mostrar "O Windows protegeu o computador" porque o instalador não tem assinatura digital.
     Clique em **Mais informações → Executar assim mesmo**.
4. Pronto: a câmera **"Kinect Cor"** já aparece nos programas. O Kinect Webcam inicia com o Windows em segundo plano,
   sem ícone, e só liga o Kinect quando algum programa usa a câmera.

Atualizando da versão 1.0: basta instalar por cima. A antiga câmera "Kinect Infravermelho" é removida.

## Como usar

No programa de vídeo, escolha a câmera **"Kinect Cor"**:

- **Teams:** Configurações → Dispositivos → Câmera
- **OBS:** Fontes → + → Dispositivo de captura de vídeo → Dispositivo
- **Navegador:** o site pede permissão da câmera; escolha a câmera do Kinect na lista
- **App Câmera do Windows:** botão "Alterar câmera"

### Configurações
Abra **"Kinect Webcam (configurações)"** no Menu Iniciar. Aparece um ícone perto do relógio; clique com o botão direito:
- **Câmera ativada:** mostra ou esconde a câmera "Kinect Cor" nos programas
- **Iniciar com o Windows:** ligado por padrão (necessário para a câmera funcionar depois de reiniciar)
- **Abrir log**, **Ocultar ícone** (continua funcionando) e **Encerrar**

Cor do ícone: 🔵 Kinect desligado (nenhum programa usando) · 🟢 em uso, com imagem · 🟠 ligando / sem sinal · ⚪ câmera desativada.

No Menu Iniciar também há atalhos para **Ativar** e **Desativar** a câmera.

### Linha de comando (opcional)
```
"C:\Program Files\KinectVCam\KinectBridge.exe" --on          ativa a câmera
"C:\Program Files\KinectVCam\KinectBridge.exe" --off         desativa a câmera
"C:\Program Files\KinectVCam\KinectBridge.exe" --background  roda em segundo plano, sem ícone (padrão)
"C:\Program Files\KinectVCam\KinectBridge.exe" --tray        mostra o ícone de configurações
"C:\Program Files\KinectVCam\KinectBridge.exe" --status      mostra o estado
"C:\Program Files\KinectVCam\KinectBridge.exe" --autostart on|off
```

## Limitações

- ❌ **Windows Hello não funciona.** O reconhecimento facial do Windows não aceita câmera virtual.
- ❌ **A câmera só pode ser usada por um programa de cada vez.** Se a imagem ficar preta, feche a câmera no outro programa.
- Por ser um ajudante e não um driver de kernel, o Kinect leva 1 a 2 segundos para ligar; nesse tempo aparece "SEM SINAL".
- Não tem áudio: o microfone do Kinect continua funcionando como microfone normal do Windows.
- Com pouca luz, o próprio Kinect reduz a câmera colorida para 15 fps.
- O Kinect só fica ocupado enquanto algum programa usa a câmera; fora isso, o Kinect Studio e outros apps do Kinect podem usá-lo normalmente.

## Solução de problemas

| Problema | O que fazer |
|---|---|
| Aparece "SEM SINAL" | Normal por 1 a 2 s enquanto o Kinect liga. Se continuar: o Kinect está desconectado, sem energia ou o Kinect Webcam não está rodando (abra "Kinect Webcam (configurações)" no Menu Iniciar e veja o status). Quando o Kinect volta, a imagem volta sozinha. |
| Imagem preta | Outro programa está usando a mesma câmera. Feche-a no outro programa. |
| A câmera não aparece | Abra "Kinect Webcam (configurações)" no Menu Iniciar e confira se "Câmera ativada" está marcado. Confira também Configurações → Privacidade → Câmera. |
| Imagem muito clara ou estourada no OBS | Propriedades da fonte → **Intervalo de cores**: teste "Parcial" e "Completo". |
| Kinect cai sozinho (luz apaga, some do Gerenciador de Dispositivos) | Faça os [ajustes necessários](#ajustes-necessários-para-o-kinect-ficar-estável): aprimoramentos de áudio desligados e economia de energia da USB desligada. |
| Microfone do Kinect com status "Erro" | Não afeta as câmeras. Desconecte e reconecte o Kinect se precisar do microfone. |

Logs (úteis para relatar problemas):
- `%LOCALAPPDATA%\KinectVCam\kinectvcam.log`: Kinect conectado/desconectado, reconexões
- `C:\ProgramData\KinectVCam\vcam.log`: lado da câmera virtual

## Desinstalar
**Configurações → Aplicativos → Aplicativos instalados → Kinect Webcam → Desinstalar.**
O desinstalador remove as câmeras, o "iniciar com o Windows" e os arquivos.

---

## Para desenvolvedores

### Como funciona
```
Kinect v2 ──► KinectBridge.exe (C#, .NET Framework 4.8)
              roda escondido; abre o sensor SÓ enquanto um app pede quadros (a DLL marca
              readerTick no cabeçalho a cada quadro pedido), lê a cor, desespelha e grava
              em memória compartilhada (Global\KinectVCam_Color)
                         │
                         ▼
              KinectVCam.dll (C++, Media Foundation)
              fonte de mídia carregada pelo serviço "Windows Camera Frame Server";
              entrega NV12/YUY2 aos programas ou o quadro "SEM SINAL"
                         │
                         ▼
              Câmeras virtuais registradas com MFCreateVirtualCamera
              ("Kinect Cor"; passa pela privacidade de câmera do Windows)
```

A DLL precisa ser nativa (C++) porque é carregada dentro de um serviço do Windows.

### Requisitos para compilar
- **Visual Studio 2022 Build Tools** (ou Visual Studio 2022) com:
  - *Desenvolvimento para desktop com C++* (MSVC v143)
  - *Windows 11 SDK* (10.0.22000 ou mais novo)
  - *.NET Framework 4.8 targeting pack*
- **Kinect for Windows SDK 2.0**: [baixar](https://www.microsoft.com/download/details.aspx?id=44561) (fornece o `Microsoft.Kinect.dll`)
- **Inno Setup 6**, só para gerar o instalador: `winget install JRSoftware.InnoSetup`

### Compilar
```
build.cmd                          compila tudo em bin\
installer\build-installer.cmd      compila e gera o instalador em dist\
```

Para testar sem instalador (desenvolvimento): `scripts\install.cmd` e `scripts\uninstall.cmd`.

### Estrutura
```
src/KinectBridge/     C#: leitura do Kinect, memória compartilhada, liga/desliga sob demanda, ícone opcional
src/KinectVCam/       C++: fonte de mídia da câmera virtual (DLL COM)
tools/VCamTest/       ferramenta de teste: lista câmeras e lê quadros via Media Foundation
installer/            script do Inno Setup e build do instalador
scripts/              instalar/desinstalar manualmente (desenvolvimento)
build.cmd             compila tudo
```

### Testes realizados
Versão 1.0 (com as câmeras Cor e IR), Windows 11 25H2 (build 26200) com Kinect v2 real.
A versão 1.1 (Kinect sob demanda, só a câmera Cor, sem ícone) foi compilada, mas ainda não foi testada no hardware:

| Teste | Resultado |
|---|---|
| Leitura do Kinect (cor + IR juntos) | ✅ 30 fps nos dois (15 fps com pouca luz) |
| App Câmera do Windows | ✅ As duas câmeras com imagem |
| Chrome (webcamtests.com) | ✅ Cor 1920x1080 a 30 fps, IR 512x424 a 30 fps |
| Microsoft Teams | ✅ |
| OBS Studio | ✅ |
| Cor e IR abertas ao mesmo tempo | ✅ |
| Desconectar/reconectar o Kinect com a câmera aberta | ✅ "SEM SINAL" em cerca de 1,5 s; a imagem volta sozinha cerca de 5 s depois de reconectar, sem reabrir o programa |
| Ligar/desligar câmeras sem reiniciar | ✅ |
| Instalar, desinstalar, reinstalar e atualizar (instalador) | ✅ |
| Dois programas na mesma câmera | ❌ Não suportado (limitação) |
| Discord, Zoom | Não testados |

## Licença
Código aberto, sob a [licença MIT](LICENSE): pode usar, modificar e redistribuir, mantendo o aviso de licença.

O `Microsoft.Kinect.dll` incluído no instalador e o Kinect for Windows Runtime são da Microsoft e seguem a licença
do Kinect for Windows SDK 2.0.
