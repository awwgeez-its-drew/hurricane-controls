'use strict';
// Windows credential check: shows the standard "Windows Security" dialog
// (CredUIPromptForWindowsCredentials, restricted to the signed-in user) and
// validates what was typed with LogonUser. No native Node module and no
// installs: it runs the built-in Windows PowerShell and P/Invokes the OS.
//
// NOT exercised in this repo's Linux CI - the dialog needs a real Windows
// desktop. See dashboard/README.md ("Windows sign-in") for the bench check.
//
// Known limit: the check validates a *password*. If your account signs in with
// a PIN / fingerprint / face, the dialog's default tile is that method and
// Windows hands back a buffer that cannot be password-verified; we report that
// and ask you to pick "Sign-in options -> Password" in the same dialog.

const { spawn } = require('child_process');
const path = require('path');

// C# 5 only (Windows PowerShell 5.1's Add-Type compiler). Single-quoted
// here-string: nothing in it is expanded by PowerShell.
const CSHARP = String.raw`
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class HcAuth {
  [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
  struct CREDUI_INFO { public int cbSize; public IntPtr hwndParent; public string pszMessageText; public string pszCaptionText; public IntPtr hbmBanner; }
  [DllImport("credui.dll", CharSet=CharSet.Unicode)]
  static extern int CredUIPromptForWindowsCredentials(ref CREDUI_INFO uiInfo, int authError, ref uint authPackage, IntPtr inAuthBuffer, uint inAuthBufferSize, out IntPtr outAuthBuffer, out uint outAuthBufferSize, ref bool save, int flags);
  [DllImport("credui.dll", CharSet=CharSet.Unicode, SetLastError=true)]
  static extern bool CredUnPackAuthenticationBuffer(int dwFlags, IntPtr pAuthBuffer, uint cbAuthBuffer, StringBuilder pszUserName, ref int pcchMaxUserName, StringBuilder pszDomainName, ref int pcchMaxDomainName, StringBuilder pszPassword, ref int pcchMaxPassword);
  [DllImport("advapi32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
  static extern bool LogonUser(string user, string domain, string password, int logonType, int provider, out IntPtr token);
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
  [DllImport("ole32.dll")] static extern void CoTaskMemFree(IntPtr p);

  public static string Run(long hwnd, string caption, string message) {
    int authError = 0;
    for (int attempt = 0; attempt < 3; attempt++) {
      CREDUI_INFO ui = new CREDUI_INFO();
      ui.cbSize = Marshal.SizeOf(typeof(CREDUI_INFO));
      ui.hwndParent = new IntPtr(hwnd);
      ui.pszCaptionText = caption;
      ui.pszMessageText = message;
      uint pkg = 0; IntPtr outBuf; uint outSize; bool save = false;
      // 0x200 = CREDUIWIN_ENUMERATE_CURRENT_USER: only the signed-in user's tile
      int rc = CredUIPromptForWindowsCredentials(ref ui, authError, ref pkg, IntPtr.Zero, 0, out outBuf, out outSize, ref save, 0x200);
      if (rc == 1223) return "CANCEL";
      if (rc != 0) return "ERR:prompt failed (" + rc + ")";
      try {
        int u = 513, d = 257, p = 513;
        StringBuilder user = new StringBuilder(u), dom = new StringBuilder(d), pass = new StringBuilder(p);
        if (!CredUnPackAuthenticationBuffer(0, outBuf, outSize, user, ref u, dom, ref d, pass, ref p)) return "UNSUPPORTED";
        string un = user.ToString(), dn = dom.ToString();
        int bs = un.IndexOf('\\');
        if (dn.Length == 0 && bs > 0) { dn = un.Substring(0, bs); un = un.Substring(bs + 1); }
        string pw = pass.ToString();
        pass.Clear();
        IntPtr tok;
        string dArg = dn.Length == 0 ? null : dn;
        bool ok = LogonUser(un, dArg, pw, 3, 0, out tok);      // LOGON32_LOGON_NETWORK
        if (!ok) ok = LogonUser(un, dArg, pw, 2, 0, out tok);  // LOGON32_LOGON_INTERACTIVE
        pw = null;
        if (ok) { CloseHandle(tok); return "OK"; }
      } finally {
        if (outBuf != IntPtr.Zero) {
          byte[] zero = new byte[outSize];
          Marshal.Copy(zero, 0, outBuf, (int)outSize);
          CoTaskMemFree(outBuf);
        }
      }
      authError = 1326; // ERROR_LOGON_FAILURE: dialog shows "user name or password is incorrect"
    }
    return "FAIL";
  }
}
`;

const psq = (s) => "'" + String(s).replace(/'/g, "''") + "'";

function buildScript(hwnd, caption, message) {
  // hwnd is a number we produce ourselves; caption/message are quoted.
  const h = Number.isFinite(Number(hwnd)) ? String(Math.trunc(Number(hwnd))) : '0';
  return [
    "$ErrorActionPreference = 'Stop'",
    "$src = @'", CSHARP.trim(), "'@",
    'try {',
    '  Add-Type -TypeDefinition $src',
    `  [Console]::Out.WriteLine([HcAuth]::Run([long]${h}, ${psq(caption)}, ${psq(message)}))`,
    '} catch { [Console]::Out.WriteLine("ERR:" + $_.Exception.Message) }',
  ].join('\r\n');
}

function interpret(out) {
  const line = String(out || '').split(/\r?\n/).map((l) => l.trim()).filter(Boolean).pop() || '';
  if (line === 'OK') return { ok: true };
  if (line === 'CANCEL') return { ok: false, reason: 'cancelled', message: 'Windows sign-in was cancelled - nothing was sent' };
  if (line === 'FAIL') return { ok: false, reason: 'denied', message: 'Windows rejected the credentials - nothing was sent' };
  if (line === 'UNSUPPORTED') {
    return { ok: false, reason: 'unsupported', message: 'That sign-in method (PIN / fingerprint / face) cannot be verified by this check. In the Windows Security box choose "Sign-in options" and use your password.' };
  }
  return { ok: false, reason: 'unavailable', message: 'Windows sign-in is unavailable' + (line ? ': ' + line.replace(/^ERR:/, '') : '') };
}

/**
 * @param {{hwnd:number, caption:string, message:string, timeoutMs?:number}} o
 */
function verify(o) {
  return new Promise((resolve) => {
    if (process.platform !== 'win32') return resolve({ ok: false, reason: 'unavailable', message: 'Windows sign-in is only available on Windows' });
    const ps = path.join(process.env.SystemRoot || 'C:\\Windows', 'System32', 'WindowsPowerShell', 'v1.0', 'powershell.exe');
    const enc = Buffer.from(buildScript(o.hwnd, o.caption, o.message), 'utf16le').toString('base64');
    let out = '', done = false;
    const finish = (r) => { if (!done) { done = true; clearTimeout(timer); resolve(r); } };
    let child;
    try {
      child = spawn(ps, ['-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-EncodedCommand', enc], { windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
    } catch (e) { return resolve({ ok: false, reason: 'unavailable', message: 'Windows sign-in is unavailable: ' + e.message }); }
    const timer = setTimeout(() => { try { child.kill(); } catch (_) { /* ignore */ } finish({ ok: false, reason: 'unavailable', message: 'Windows sign-in timed out' }); }, o.timeoutMs || 180000);
    child.stdout.on('data', (d) => { out += d; });
    child.on('error', (e) => finish({ ok: false, reason: 'unavailable', message: 'Windows sign-in is unavailable: ' + e.message }));
    child.on('close', () => finish(interpret(out)));
  });
}

module.exports = { verify, buildScript, interpret, psq };
