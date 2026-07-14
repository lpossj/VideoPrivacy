#define MyAppName "视频自动打码"
#define MyAppVersion "0.0.1"
#define MyAppPublisher "lpossj"
#define MyAppExeName "video_privacy_gui.exe"

#define GpuPackageUrl "https://github.com/lpossj/VideoPrivacy/releases/download/v0.0.1/VideoPrivacy-GPU-Runtime-v0.0.1-CUDA12-cuDNN9-x64.zip"
#define GpuPackageHash "7EFAE8571F4E216E2C1153617247A09CF8F2437D73141DE55354D3E92AFF2083"

[Setup]
AppId={{3B8F6E64-4B2B-4E3C-9B5B-2F7597586B7E}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}

DefaultDirName={localappdata}\Programs\VideoPrivacy
DefaultGroupName={#MyAppName}

; 按当前用户安装，不要求管理员权限
PrivilegesRequired=lowest

ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

OutputDir=output
OutputBaseFilename=VideoPrivacy-Setup-v{#MyAppVersion}-x64

Compression=lzma2
SolidCompression=yes
WizardStyle=modern

; ZIP 解压需要 full
ArchiveExtraction=full

; 始终显示组件选择页面
AlwaysShowComponentsList=yes

; 只显示简体中文
ShowLanguageDialog=no

DisableDirPage=no

; 文件夹选择窗口显示“新建文件夹”
AppendDefaultDirName=yes

UninstallDisplayIcon={app}\{#MyAppExeName}

[Languages]
Name: "chinesesimp"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[Types]
Name: "cpu"; Description: "标准安装（仅 CPU，推荐）"
Name: "full"; Description: "完整安装（CPU + NVIDIA GPU）"
Name: "custom"; Description: "自定义安装"; Flags: iscustom

[Components]
; CPU 核心文件始终安装，用户不能取消
Name: "core"; \
    Description: "CPU 推理核心文件"; \
    Types: cpu full custom; \
    Flags: fixed

; GPU 默认不安装，完整安装时自动选中
Name: "gpu"; \
    Description: "NVIDIA GPU 加速组件（联网下载约 1.54 GB，安装后约占用 2.31 GB）"; \
    Types: full

[Files]
; CPU 基础文件直接打进安装程序
Source: "..\core\*"; \
    DestDir: "{app}"; \
    Flags: ignoreversion recursesubdirs createallsubdirs; \
    Components: core

; 用户勾选 GPU 后，从 GitHub 下载、校验并解压
Source: "{#GpuPackageUrl}"; \
    DestName: "VideoPrivacy-GPU-Runtime-v0.0.1-CUDA12-cuDNN9-x64.zip"; \
    DestDir: "{app}"; \
    Hash: "{#GpuPackageHash}"; \
    ExternalSize: 2_479_111_734; \
    Flags: external download extractarchive recursesubdirs createallsubdirs ignoreversion; \
    Components: gpu

[Dirs]
Name: "{app}\output"

[Tasks]
Name: "desktopicon"; \
    Description: "创建桌面快捷方式"; \
    Flags: unchecked

[Icons]
Name: "{group}\Video Privacy"; \
    Filename: "{app}\{#MyAppExeName}"; \
    WorkingDir: "{app}"

Name: "{userdesktop}\Video Privacy"; \
    Filename: "{app}\{#MyAppExeName}"; \
    WorkingDir: "{app}"; \
    Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; \
    Description: "启动 Video Privacy"; \
    WorkingDir: "{app}"; \
    Flags: nowait postinstall skipifsilent