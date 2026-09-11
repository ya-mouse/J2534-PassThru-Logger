using System;
using System.IO;
using System.Windows.Forms;

namespace PassThruLoggerControl
{
    static class Program
    {
        /// <summary>
        /// Crash log location: %TEMP%\PassThruLoggerControl_crash.log
        /// </summary>
        internal static readonly string CrashLogPath =
            Path.Combine(Path.GetTempPath(), "PassThruLoggerControl_crash.log");

        /// <summary>
        /// The main entry point for the application.
        /// </summary>
        [STAThread]
        static void Main()
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);

            // Catch unhandled exceptions on every thread so a silent
            // background-thread crash no longer tears down the process
            // without a trace. Requires PDBs for file/line info.
            Application.SetUnhandledExceptionMode(UnhandledExceptionMode.CatchException);
            Application.ThreadException += (s, e) =>
            {
                CrashLog("UI-thread", e.Exception);
                Application.Exit();
            };
            AppDomain.CurrentDomain.UnhandledException += (s, e) =>
            {
                // ExceptionObject is object, not Exception — a non-Exception
                // throwable must still leave a trace, that is the whole point.
                CrashLog("worker-thread", e.ExceptionObject, e.IsTerminating);
            };

            try
            {
                Application.Run(new J2534LogController());
            }
            catch (System.Exception e)
            {
                CrashLog("main", e);
            }
        }

        /// <summary>
        /// Append the full throwable (with stack trace / file:line when PDBs
        /// are present) to the crash log and surface a MessageBox. Safe to
        /// call from any thread with any object; never throws.
        /// </summary>
        /// <param name="isTerminating">
        /// From UnhandledExceptionEventArgs.IsTerminating for AppDomain
        /// crashes; null (logged as "n/a") for UI/main sources. Note
        /// AppDomain.IsFinalizingForUnload is NOT this — it reads False
        /// exactly when the process is crash-terminating.
        /// </param>
        internal static void CrashLog(string source, object ex, bool? isTerminating = null)
        {
            if (ex == null) return;

            // Everything inside one try: even ex.ToString() can throw on a
            // pathological exception, and escaping from the AppDomain handler
            // would FailFast with no log written at all.
            try
            {
                string text =
                    $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}] === {source} unhandled exception ===\n" +
                    $"{ex}\n" +
                    $"AppDomain: {AppDomain.CurrentDomain.FriendlyName}\n" +
                    $"IsTerminating: {(isTerminating.HasValue ? isTerminating.Value.ToString() : "n/a")}\n\n";

                try { File.AppendAllText(CrashLogPath, text); }
                catch { /* never let logging itself throw */ }

                try
                {
                    string terminatingNote = isTerminating == true
                        ? "\n\nThe process is terminating."
                        : "";
                    MessageBox.Show(
                        $"{source} crash (logged to {CrashLogPath}):{terminatingNote}\n\n{ex}",
                        "PassThruLoggerControl Crash",
                        MessageBoxButtons.OK, MessageBoxIcon.Error);
                }
                catch { /* no UI available (e.g. during shutdown) */ }
            }
            catch { /* never throw from the crash handler */ }
        }
    }
}
