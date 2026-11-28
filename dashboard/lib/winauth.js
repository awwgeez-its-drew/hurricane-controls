'use strict';
// Windows credential check: shows the standard "Windows Security" dialog
// (CredUIPromptForWindowsCredentials, restricted to the signed-in user) and
// validates what was typed with LogonUser. No native Node module and no
// installs: it runs the built-in Windows PowerShell and P/Invokes the OS.
//
// NOT exercised in this repo's Linux CI - the dialog needs a real Windows
// desktop. See dashboard/README.md ("Windows sign-in") for the bench check.
//
// Two methods, tried in this order (method "auto", the default):
//   1. Windows Hello (UserConsentVerifier): the Windows Security dialog that takes
//      your PIN, fingerprint or face. Used when Hello is set up on this PC.
//   2. Password (CredUIPromptForWindowsCredentials + LogonUser): used when Hello is
//      unavailable or errors. A PIN cannot be verified by this path.
// method "password" skips Hello. A cancelled or failed Hello prompt is final: it
// never silently falls through to the weaker-to-type password box.

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

const HELLO_PS = String.raw`
$ErrorActionPreference = 'Stop'
try {
  Add-Type -AssemblyName System.Runtime.WindowsRuntime
  $null = [Windows.Security.Credentials.UI.UserConsentVerifier, Windows.Security.Credentials.UI, ContentType = WindowsRuntime]
  $null = [Windows.Security.Credentials.UI.UserConsentVerifierAvailability, Windows.Security.Credentials.UI, ContentType = WindowsRuntime]
  $null = [Windows.Security.Credentials.UI.UserConsentVerificationResult, Windows.Security.Credentials.UI, ContentType = WindowsRuntime]
  $asTask = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
    $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation` + "`" + String.raw`1' })[0]
  function Await($op, $type) { $t = $asTask.MakeGenericMethod($type).Invoke($null, @($op)); $t.Wait(-1) | Out-Null; $t.Result }
  $avail = Await ([Windows.Security.Credentials.UI.UserConsentVerifier]::CheckAvailabilityAsync()) ([Windows.Security.Credentials.UI.UserConsentVerifierAvailability])
  if ("$avail" -ne 'Available') { [Console]::Out.WriteLine('HELLO:NA:' + $avail); exit 0 }
  $res = Await ([Windows.Security.Credentials.UI.UserConsentVerifier]::RequestVerificationAsync(MESSAGE)) ([Windows.Security.Credentials.UI.UserConsentVerificationResult])
  [Console]::Out.WriteLine('HELLO:' + $res)
} catch { [Console]::Out.WriteLine('HELLO:ERR:' + $_.Exception.Message) }
`;

function buildHelloScript(message) { return HELLO_PS.replace('MESSAGE', psq(message)).replace(/\n/g, '\r\n'); }

function interpretHello(out) {
  const line = String(out || '').split(/\r?\n/).map((l) => l.trim()).filter(Boolean).pop() || '';
  const m = /^HELLO:(.*)$/.exec(line);
  const v = m ? m[1] : '';
  if (v === 'Verified') return { ok: true, method: 'hello' };
  if (v === 'Canceled') return { ok: false, final: true, reason: 'cancelled', message: 'Windows sign-in was cancelled - nothing was sent' };
  if (v === 'RetriesExhausted') return { ok: false, final: true, reason: 'denied', message: 'Too many failed attempts - nothing was sent' };
  if (v === 'DeviceBusy') return { ok: false, final: true, reason: 'unavailable', message: 'Windows Hello is busy - try again in a moment' };
  // NA:<why>, ERR:<why>, DeviceNotPresent, NotConfiguredForUser, DisabledByPolicy, or no output at all:
  // Hello can't be used here - fall back to the password dialog.
  return { ok: false, final: false, reason: 'no-hello', message: v || 'no response' };
}

function runPowerShell(script, timeoutMs) {
  return new Promise((resolve, reject) => {
    const ps = path.join(process.env.SystemRoot || 'C:\\Windows', 'System32', 'WindowsPowerShell', 'v1.0', 'powershell.exe');
    const enc = Buffer.from(script, 'utf16le').toString('base64');
    let out = '', done = false, child;
    const finish = (fn, v) => { if (!done) { done = true; clearTimeout(timer); fn(v); } };
    try { child = spawn(ps, ['-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-EncodedCommand', enc], { windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] }); }
    catch (e) { return reject(e); }
    const timer = setTimeout(() => { try { child.kill(); } catch (_) { /* ignore */ } finish(reject, new Error('timed out')); }, timeoutMs || 180000);
    child.stdout.on('data', (d) => { out += d; });
    child.on('error', (e) => finish(reject, e));
    child.on('close', () => finish(resolve, out));
  });
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
 * @param {{hwnd:number, caption:string, message:string, method?:'auto'|'password', timeoutMs?:number}} o
 * @param {(script:string, timeoutMs?:number)=>Promise<string>} [run] injectable for tests
 */
async function verify(o, run) {
  if (!run && process.platform !== 'win32') return { ok: false, reason: 'unavailable', message: 'Windows sign-in is only available on Windows' };
  const exec = run || runPowerShell;
  const fail = (e) => ({ ok: false, reason: 'unavailable', message: 'Windows sign-in is unavailable: ' + e.message });
  if ((o.method || 'auto') !== 'password') {
    let h;
    try { h = interpretHello(await exec(buildHelloScript(o.message), o.timeoutMs)); } catch (e) { h = { ok: false, final: false, reason: 'no-hello', message: e.message }; }
    if (h.ok) return { ok: true };
    if (h.final) return h;
    // else: Hello not usable on this PC -> password dialog below
  }
  try { return interpret(await exec(buildScript(o.hwnd, o.caption, o.message), o.timeoutMs)); } catch (e) { return fail(e); }
}

module.exports = { verify, buildScript, buildHelloScript, interpret, interpretHello, psq };
