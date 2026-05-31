using Microsoft.Win32;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Text.Json;
using System.Threading;
using System.Windows.Forms;

namespace PassThruLoggerControl
{
    public partial class J2534LogController : Form
    {
        private List<J2534Driver> drivers = new List<J2534Driver>();
        private bool initDone = false;

        private Socket listener;
        BindingList<ConnectionInfo> connectionBindingList = new BindingList<ConnectionInfo>();

        Thread socketServerThread;
        // Thread signal.
        public static ManualResetEvent allDone = new ManualResetEvent(false);

        // Protocol items for the combo box
        private static readonly (string Name, uint Value)[] Protocols = new[]
        {
            ("CAN", (uint)0x05),
            ("ISO15765", (uint)0x06),
            ("J1850VPW", (uint)0x01),
            ("J1850PWM", (uint)0x02),
            ("ISO9141", (uint)0x03),
            ("ISO14230", (uint)0x04),
            ("SCI_A_ENGINE", (uint)0x07),
            ("SCI_A_TRANS", (uint)0x08),
            ("SCI_B_ENGINE", (uint)0x09),
            ("SCI_B_TRANS", (uint)0x0A),
        };

        // Common baud rates
        private static readonly string[] BaudRates = new[]
        {
            "500000", "250000", "125000", "1000000",
            "33333", "50000", "83333", "100000",
            "10400", "9600", "4800"
        };

        /////////////////////// Constructor/Destructor
        public J2534LogController()
        {
            InitializeComponent();

            socketServerThread = new Thread(new ThreadStart(socketServerFunction));

            connectionBindingSource.DataSource = connectionBindingList;

            // Populate protocol combo
            foreach (var p in Protocols)
                protocolCombo.Items.Add(p.Name);

            // Populate baud rate combo
            foreach (var b in BaudRates)
                baudRateCombo.Items.Add(b);

            // Populate log format combo
            logFormatCombo.Items.Add("Text");
            logFormatCombo.Items.Add("JSON");
            logFormatCombo.SelectedIndex = 0;

            RegistryHelper.ScanDrivers(drivers);
            defaultdriver.DataSource = drivers;

            using (var reg32 = RegistryKey.OpenBaseKey(RegistryHive.CurrentUser, RegistryView.Registry32))
            {
                using (var loggerRegEntry = reg32.CreateSubKey(@"Software\Passthru Logger"))
                {
                    var defaultkeypath = loggerRegEntry.GetValue("DefaultDriverKey");
                    if (defaultkeypath == null || loggerRegEntry.GetValueKind("DefaultDriverKey") != RegistryValueKind.String)
                    {
                        if (drivers.Count() > 0)
                            loggerRegEntry.SetValue("DefaultDriverKey", drivers[0].key, RegistryValueKind.String);
                    }
                    else
                    {
                        int index = 0;
                        bool entryfound = false;
                        foreach (var driver in drivers)
                        {
                            if (driver.key == (string)defaultkeypath)
                            {
                                defaultdriver.SelectedIndex = index;
                                entryfound = true;
                                break;
                            }

                            index++;
                        }

                        if (!entryfound && drivers.Count() > 0)
                        {
                            loggerRegEntry.SetValue("DefaultDriverKey", drivers[0].key, RegistryValueKind.String);
                        }
                    }

                    // Load auto-connect settings
                    LoadConnectSettings(loggerRegEntry);
                }
            }

            initDone = true;

            // Establish the local endpoint for the socket.
            IPAddress ipAddress = Dns.GetHostEntry("localhost").AddressList[0];
            IPEndPoint localEndPoint = new IPEndPoint(ipAddress, 2534);

            // Create a TCP/IP socket.
            listener = new Socket(ipAddress.AddressFamily, SocketType.Stream, ProtocolType.Tcp);

            // Bind the socket to the local endpoint and listen for incoming connections.
            try
            {
                listener.Bind(localEndPoint);
                listener.Listen(100);
            }
            catch (Exception e)
            {
                Console.WriteLine(e.ToString());
                System.Windows.Forms.MessageBox.Show("Error opening socket: " + e.ToString());
                Application.Exit();
            }

            socketServerThread.Start();
        }

        /////////////////////// Settings persistence
        private void LoadConnectSettings(RegistryKey regKey)
        {
            // Protocol
            var protocolVal = regKey.GetValue("ConnectProtocolID");
            uint protocolId = (protocolVal is int pv) ? (uint)pv : 0x05; // default CAN
            int protoIdx = Array.FindIndex(Protocols, p => p.Value == protocolId);
            protocolCombo.SelectedIndex = (protoIdx >= 0) ? protoIdx : 0;

            // Baud rate
            var baudVal = regKey.GetValue("ConnectBaudRate");
            uint baudRate = (baudVal is int bv) ? (uint)bv : 500000;
            baudRateCombo.Text = baudRate.ToString();

            // Flags
            var flagsVal = regKey.GetValue("ConnectFlags");
            uint flags = (flagsVal is int fv) ? (uint)fv : 0;
            flagsTextBox.Text = flags.ToString();

            // Device name
            var devName = regKey.GetValue("DeviceName") as string ?? "";
            deviceNameTextBox.Text = devName;

            // Auto-inject
            var autoVal = regKey.GetValue("AutoInjectConnect");
            bool autoInject = (autoVal is int av) ? av != 0 : true;
            autoInjectCheckBox.Checked = autoInject;

            // Mock VBATT (stored as millivolts, displayed as volts)
            var vbattVal = regKey.GetValue("MockVbattMv");
            uint vbattMv = (vbattVal is int vv) ? (uint)vv : 0;
            mockVbattTextBox.Text = (vbattMv / 1000.0).ToString("F1");

            // Log file path
            var logPath = regKey.GetValue("LogFilePath") as string ?? "";
            logFilePathTextBox.Text = logPath;

            // Log format
            var logFmt = regKey.GetValue("LogFormat");
            int format = (logFmt is int lf) ? lf : 0;
            logFormatCombo.SelectedIndex = (format >= 0 && format < logFormatCombo.Items.Count) ? format : 0;

            // Buffer size
            var bufVal = regKey.GetValue("EventBufferSize");
            uint bufSize = (bufVal is int bs) ? (uint)bs : 4096;
            bufferSizeTextBox.Text = bufSize.ToString();
        }

        /////////////////////// Server setup functions
        private void socketServerFunction()
        {
            // Bind the socket to the local endpoint and listen for incoming connections.
            try
            {
                while (true)
                {
                    // Set the event to nonsignaled state.
                    allDone.Reset();

                    // Start an asynchronous socket to listen for connections.
                    Console.WriteLine("Waiting for a connection...");
                    listener.BeginAccept(new AsyncCallback(AcceptCallback), listener);

                    // Wait until a connection is made before continuing.
                    allDone.WaitOne();
                }

            }
            catch (System.ObjectDisposedException)
            {
                //ObjectDisposedException will be raised if we close the connection.
                //Good way to break the blocking socket functions.
                Console.WriteLine("Connection closed, shutting down server thread.");
            }
            catch (Exception e)
            {
                Console.WriteLine(e.ToString());
                System.Windows.Forms.MessageBox.Show("Error accepting connection: " + e.ToString());
                Application.Exit();
            }

        }

        public void AcceptCallback(IAsyncResult ar)
        {
            // Signal the main thread to continue.
            allDone.Set();

            try
            {
                // Get the socket that handles the client request.
                Socket listener = (Socket)ar.AsyncState;
                Socket handler = listener.EndAccept(ar);

                // Create the state object.
                var conninfo = new ConnectionInfo(this, handler);
                conninfo.start();

                Invoke(new MethodInvoker(delegate () { connectionBindingList.Add(conninfo); }));

                Console.WriteLine("Connection established...");
            }
            catch (System.ObjectDisposedException)
            {
                Console.WriteLine("Connectionclosed, unable to do stuff.");
            }
        }

        /////////////////////// Extra functions
        public void updateConnectionListEntry(ConnectionInfo conninfo)
        {
            Invoke(new MethodInvoker(delegate () { connectionBindingList.ResetItem(connectionBindingList.IndexOf(conninfo)); }));
        }

        public void addLinesToLogPreview(ConnectionInfo conninfo, string[] lines)
        {
            Invoke(new MethodInvoker(delegate () {
                if (connectionBindingList.Count == 0 || loggerconnections.CurrentCell == null) return;
                if (connectionBindingList[loggerconnections.CurrentCell.RowIndex] != conninfo) return;

                int visibleItems = logpreview.ClientSize.Height / logpreview.ItemHeight;
                var tmp = Math.Max(logpreview.Items.Count - visibleItems, 0);
                bool doscroll = (logpreview.TopIndex == tmp);

                logpreview.Items.AddRange(lines);
                while (logpreview.Items.Count > conninfo.maxLogPreviewEntryCount)
                    logpreview.Items.RemoveAt(0);

                if(doscroll)
                    logpreview.TopIndex = Math.Max(logpreview.Items.Count - visibleItems + 1, 0);
            }));
        }

        /////////////////////// UI Events
        private void savelog_Click(object sender, EventArgs e)
        {
            logsavewindow.Filter = "Plain Text Log (*.txt)|*.txt|JSON Log (*.json)|*.json|All Files (*.*)|*.*";
            logsavewindow.DefaultExt = "txt";
            logsavewindow.ShowDialog();
        }

        private void defaultdriver_SelectedIndexChanged(object sender, EventArgs e)
        {
            if (!initDone) return;
            using (var reg32 = RegistryKey.OpenBaseKey(RegistryHive.CurrentUser, RegistryView.Registry32))
            {
                using (var loggerRegEntry = reg32.CreateSubKey(@"Software\Passthru Logger"))
                {
                    if (drivers.Count() > defaultdriver.SelectedIndex)
                        loggerRegEntry.SetValue("DefaultDriverKey", drivers[defaultdriver.SelectedIndex].key, RegistryValueKind.String);
                }
            }
        }

        private void saveFileDialog1_FileOk(object sender, CancelEventArgs e)
        {
            if (e.Cancel) return;
            if (loggerconnections.CurrentCell == null) return;

            var conn = connectionBindingList[loggerconnections.CurrentCell.RowIndex];
            string fileName = logsavewindow.FileName;

            if (fileName.EndsWith(".json", StringComparison.OrdinalIgnoreCase))
            {
                conn.saveLogJson(fileName);
            }
            else
            {
                conn.saveLog(fileName);
            }
        }

        private void Form1_FormClosing(object sender, EventArgs e)
        {
            Console.WriteLine("CLOSING OBJECT");
            listener.Close();

            foreach(var conn in connectionBindingList)
            {
                conn.close();
            }
        }

        private void loggerconnections_RowEnter(object sender, DataGridViewCellEventArgs e)
        {
            //Rows can not be removed, so there is no reason to disable this button after a row appears.
            savelog.Enabled = true;
            removeConnectionButton.Enabled = true;

            logpreview.Items.Clear();
            foreach (var row in connectionBindingList[e.RowIndex].logPreviewEntries)
                logpreview.Items.Add(row);
        }

        private void loggerconnections_RowLeave(object sender, DataGridViewCellEventArgs e)
        {
            Console.WriteLine("CellLeave");
        }

        /////////////////////// Clear / Remove buttons
        private void clearAllButton_Click(object sender, EventArgs e)
        {
            // Close all connections and clear the list
            foreach (var conn in connectionBindingList)
                conn.close();

            connectionBindingList.Clear();
            logpreview.Items.Clear();
            savelog.Enabled = false;
            removeConnectionButton.Enabled = false;
        }

        private void removeConnectionButton_Click(object sender, EventArgs e)
        {
            if (loggerconnections.CurrentCell == null) return;
            int idx = loggerconnections.CurrentCell.RowIndex;
            if (idx < 0 || idx >= connectionBindingList.Count) return;

            var conn = connectionBindingList[idx];
            conn.close();
            connectionBindingList.RemoveAt(idx);
            logpreview.Items.Clear();

            if (connectionBindingList.Count == 0)
            {
                savelog.Enabled = false;
                removeConnectionButton.Enabled = false;
            }
        }

        /////////////////////// Settings — explicit Save
        private void saveSettingsButton_Click(object sender, EventArgs e)
        {
            using (var reg32 = RegistryKey.OpenBaseKey(RegistryHive.CurrentUser, RegistryView.Registry32))
            using (var entry = reg32.CreateSubKey(@"Software\Passthru Logger"))
            {
                // Protocol
                if (protocolCombo.SelectedIndex >= 0)
                    entry.SetValue("ConnectProtocolID", (int)Protocols[protocolCombo.SelectedIndex].Value, RegistryValueKind.DWord);

                // Baud rate
                if (uint.TryParse(baudRateCombo.Text, out uint baudVal))
                    entry.SetValue("ConnectBaudRate", (int)baudVal, RegistryValueKind.DWord);

                // Flags
                if (uint.TryParse(flagsTextBox.Text, out uint flagsVal))
                    entry.SetValue("ConnectFlags", (int)flagsVal, RegistryValueKind.DWord);

                // Device name
                entry.SetValue("DeviceName", deviceNameTextBox.Text, RegistryValueKind.String);

                // Auto-inject
                entry.SetValue("AutoInjectConnect", autoInjectCheckBox.Checked ? 1 : 0, RegistryValueKind.DWord);

                // Mock VBATT
                if (float.TryParse(mockVbattTextBox.Text, System.Globalization.NumberStyles.Float,
                    System.Globalization.CultureInfo.InvariantCulture, out float volts))
                {
                    int millivolts = (int)(volts * 1000.0f);
                    if (millivolts >= 0)
                        entry.SetValue("MockVbattMv", millivolts, RegistryValueKind.DWord);
                }

                // Log file path
                entry.SetValue("LogFilePath", logFilePathTextBox.Text, RegistryValueKind.String);

                // Log format (0=text, 1=json)
                entry.SetValue("LogFormat", logFormatCombo.SelectedIndex >= 0 ? logFormatCombo.SelectedIndex : 0, RegistryValueKind.DWord);

                // Event buffer size
                if (uint.TryParse(bufferSizeTextBox.Text, out uint bufSize) && bufSize > 0)
                    entry.SetValue("EventBufferSize", (int)bufSize, RegistryValueKind.DWord);
            }

            MessageBox.Show("Settings saved. DLL will use new settings on next load.",
                "Settings Saved", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }
    }
}
