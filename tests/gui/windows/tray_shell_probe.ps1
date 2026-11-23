Add-Type -AssemblyName System.Windows.Forms
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class TrayShellProbe {
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct NotifyIconData {
        public uint cbSize; public IntPtr hWnd; public uint uID; public uint uFlags; public uint uCallbackMessage; public IntPtr hIcon;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string szTip;
        public uint dwState, dwStateMask;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string szInfo;
        public uint uTimeoutOrVersion;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string szInfoTitle;
        public uint dwInfoFlags; public Guid guidItem; public IntPtr hBalloonIcon;
    }
    [DllImport("shell32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern bool Shell_NotifyIconW(uint message, ref NotifyIconData data);
}
'@
$form = New-Object System.Windows.Forms.Form
$form.Text = 'Agent Redactor ARM64 shell probe'
$form.Show()
$data = New-Object TrayShellProbe+NotifyIconData
$data.cbSize = [Runtime.InteropServices.Marshal]::SizeOf($data)
$data.hWnd = $form.Handle
$data.uID = 77
$data.uFlags = 1 -bor 2 -bor 4
$data.uCallbackMessage = 0x0401
$data.hIcon = $form.Icon.Handle
$data.szTip = 'AgentRedactorShellProbe'
$ok = [TrayShellProbe]::Shell_NotifyIconW(0, [ref] $data)
$shellError = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
Write-Output "NATIVE_SHELL_PROBE result=$ok error=$shellError"
[TrayShellProbe]::Shell_NotifyIconW(2, [ref] $data) | Out-Null
$form.Close()
if (-not $ok) { exit 1 }
