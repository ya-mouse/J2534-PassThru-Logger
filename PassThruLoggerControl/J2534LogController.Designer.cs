using System;
using System.Windows.Forms;

namespace PassThruLoggerControl
{
    partial class J2534LogController
    {
        /// <summary>
        /// Required designer variable.
        /// </summary>
        private System.ComponentModel.IContainer components = null;

        /// <summary>
        /// Clean up any resources being used.
        /// </summary>
        /// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
        protected override void Dispose(bool disposing)
        {
            if (disposing && (components != null))
            {
                components.Dispose();
            }
            base.Dispose(disposing);
        }

        #region Windows Form Designer generated code

        /// <summary>
        /// Required method for Designer support - do not modify
        /// the contents of this method with the code editor.
        /// </summary>
        private void InitializeComponent()
        {
            this.components = new System.ComponentModel.Container();
            System.ComponentModel.ComponentResourceManager resources = new System.ComponentModel.ComponentResourceManager(typeof(J2534LogController));
            this.logsavewindow = new System.Windows.Forms.SaveFileDialog();
            this.tableLayoutPanel1 = new System.Windows.Forms.TableLayoutPanel();
            this.splitContainer1 = new System.Windows.Forms.SplitContainer();
            this.loggerconnections = new System.Windows.Forms.DataGridView();
            this.logpreview = new System.Windows.Forms.ListBox();
            this.bottomPanel = new System.Windows.Forms.TableLayoutPanel();
            this.label1 = new System.Windows.Forms.Label();
            this.defaultdriver = new System.Windows.Forms.ComboBox();
            this.savelog = new System.Windows.Forms.Button();
            this.clearAllButton = new System.Windows.Forms.Button();
            this.removeConnectionButton = new System.Windows.Forms.Button();
            this.settingsGroup = new System.Windows.Forms.GroupBox();
            this.settingsLayout = new System.Windows.Forms.TableLayoutPanel();
            this.labelProtocol = new System.Windows.Forms.Label();
            this.protocolCombo = new System.Windows.Forms.ComboBox();
            this.labelBaudRate = new System.Windows.Forms.Label();
            this.baudRateCombo = new System.Windows.Forms.ComboBox();
            this.labelFlags = new System.Windows.Forms.Label();
            this.flagsTextBox = new System.Windows.Forms.TextBox();
            this.labelDeviceName = new System.Windows.Forms.Label();
            this.deviceNameTextBox = new System.Windows.Forms.TextBox();
            this.autoInjectCheckBox = new System.Windows.Forms.CheckBox();
            this.labelMockVbatt = new System.Windows.Forms.Label();
            this.mockVbattTextBox = new System.Windows.Forms.TextBox();
            this.iDDataGridViewTextBoxColumn = new System.Windows.Forms.DataGridViewTextBoxColumn();
            this.statusDataGridViewTextBoxColumn = new System.Windows.Forms.DataGridViewTextBoxColumn();
            this.eventCountDataGridViewTextBoxColumn = new System.Windows.Forms.DataGridViewTextBoxColumn();
            this.driverDataGridViewTextBoxColumn = new System.Windows.Forms.DataGridViewTextBoxColumn();
            this.clientDataGridViewTextBoxColumn = new System.Windows.Forms.DataGridViewTextBoxColumn();
            this.connectionBindingSource = new System.Windows.Forms.BindingSource(this.components);
            this.tableLayoutPanel1.SuspendLayout();
            ((System.ComponentModel.ISupportInitialize)(this.splitContainer1)).BeginInit();
            this.splitContainer1.Panel1.SuspendLayout();
            this.splitContainer1.Panel2.SuspendLayout();
            this.splitContainer1.SuspendLayout();
            ((System.ComponentModel.ISupportInitialize)(this.loggerconnections)).BeginInit();
            ((System.ComponentModel.ISupportInitialize)(this.connectionBindingSource)).BeginInit();
            this.settingsGroup.SuspendLayout();
            this.settingsLayout.SuspendLayout();
            this.bottomPanel.SuspendLayout();
            this.SuspendLayout();
            // 
            // logsavewindow
            // 
            this.logsavewindow.Title = "Save the J2534 Log file";
            this.logsavewindow.FileOk += new System.ComponentModel.CancelEventHandler(this.saveFileDialog1_FileOk);
            // 
            // tableLayoutPanel1
            // 
            this.tableLayoutPanel1.ColumnCount = 1;
            this.tableLayoutPanel1.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle(System.Windows.Forms.SizeType.Percent, 100F));
            this.tableLayoutPanel1.Controls.Add(this.splitContainer1, 0, 0);
            this.tableLayoutPanel1.Controls.Add(this.bottomPanel, 0, 1);
            this.tableLayoutPanel1.Controls.Add(this.settingsGroup, 0, 2);
            this.tableLayoutPanel1.Dock = System.Windows.Forms.DockStyle.Fill;
            this.tableLayoutPanel1.Location = new System.Drawing.Point(0, 0);
            this.tableLayoutPanel1.Name = "tableLayoutPanel1";
            this.tableLayoutPanel1.RowCount = 3;
            this.tableLayoutPanel1.RowStyles.Add(new System.Windows.Forms.RowStyle(System.Windows.Forms.SizeType.Percent, 100F));
            this.tableLayoutPanel1.RowStyles.Add(new System.Windows.Forms.RowStyle());
            this.tableLayoutPanel1.RowStyles.Add(new System.Windows.Forms.RowStyle());
            this.tableLayoutPanel1.Size = new System.Drawing.Size(870, 500);
            this.tableLayoutPanel1.TabIndex = 7;
            // 
            // splitContainer1
            // 
            this.splitContainer1.Dock = System.Windows.Forms.DockStyle.Fill;
            this.splitContainer1.Location = new System.Drawing.Point(3, 3);
            this.splitContainer1.Name = "splitContainer1";
            this.splitContainer1.Orientation = System.Windows.Forms.Orientation.Horizontal;
            // 
            // splitContainer1.Panel1
            // 
            this.splitContainer1.Panel1.Controls.Add(this.loggerconnections);
            // 
            // splitContainer1.Panel2
            // 
            this.splitContainer1.Panel2.Controls.Add(this.logpreview);
            this.splitContainer1.Size = new System.Drawing.Size(864, 292);
            this.splitContainer1.SplitterDistance = 125;
            this.splitContainer1.TabIndex = 7;
            // 
            // loggerconnections
            // 
            this.loggerconnections.AllowUserToAddRows = false;
            this.loggerconnections.AllowUserToDeleteRows = false;
            this.loggerconnections.AllowUserToOrderColumns = true;
            this.loggerconnections.AllowUserToResizeRows = false;
            this.loggerconnections.AutoGenerateColumns = false;
            this.loggerconnections.BackgroundColor = System.Drawing.SystemColors.ButtonShadow;
            this.loggerconnections.ColumnHeadersHeightSizeMode = System.Windows.Forms.DataGridViewColumnHeadersHeightSizeMode.AutoSize;
            this.loggerconnections.Columns.AddRange(new System.Windows.Forms.DataGridViewColumn[] {
            this.iDDataGridViewTextBoxColumn,
            this.statusDataGridViewTextBoxColumn,
            this.eventCountDataGridViewTextBoxColumn,
            this.driverDataGridViewTextBoxColumn,
            this.clientDataGridViewTextBoxColumn});
            this.loggerconnections.DataSource = this.connectionBindingSource;
            this.loggerconnections.Dock = System.Windows.Forms.DockStyle.Fill;
            this.loggerconnections.ImeMode = System.Windows.Forms.ImeMode.NoControl;
            this.loggerconnections.Location = new System.Drawing.Point(0, 0);
            this.loggerconnections.MultiSelect = false;
            this.loggerconnections.Name = "loggerconnections";
            this.loggerconnections.ReadOnly = true;
            this.loggerconnections.RowHeadersVisible = false;
            this.loggerconnections.RowTemplate.Height = 24;
            this.loggerconnections.SelectionMode = System.Windows.Forms.DataGridViewSelectionMode.FullRowSelect;
            this.loggerconnections.Size = new System.Drawing.Size(864, 125);
            this.loggerconnections.TabIndex = 5;
            this.loggerconnections.RowEnter += new System.Windows.Forms.DataGridViewCellEventHandler(this.loggerconnections_RowEnter);
            this.loggerconnections.RowLeave += new System.Windows.Forms.DataGridViewCellEventHandler(this.loggerconnections_RowLeave);
            // 
            // logpreview
            // 
            this.logpreview.Dock = System.Windows.Forms.DockStyle.Fill;
            this.logpreview.FormattingEnabled = true;
            this.logpreview.HorizontalScrollbar = true;
            this.logpreview.ItemHeight = 16;
            this.logpreview.Location = new System.Drawing.Point(0, 0);
            this.logpreview.Name = "logpreview";
            this.logpreview.Size = new System.Drawing.Size(864, 163);
            this.logpreview.TabIndex = 0;
            // 
            // bottomPanel
            // 
            this.bottomPanel.ColumnCount = 5;
            this.bottomPanel.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle());
            this.bottomPanel.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle(System.Windows.Forms.SizeType.Percent, 100F));
            this.bottomPanel.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle());
            this.bottomPanel.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle());
            this.bottomPanel.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle());
            this.bottomPanel.Controls.Add(this.label1, 0, 0);
            this.bottomPanel.Controls.Add(this.defaultdriver, 1, 0);
            this.bottomPanel.Controls.Add(this.savelog, 2, 0);
            this.bottomPanel.Controls.Add(this.removeConnectionButton, 3, 0);
            this.bottomPanel.Controls.Add(this.clearAllButton, 4, 0);
            this.bottomPanel.Dock = System.Windows.Forms.DockStyle.Fill;
            this.bottomPanel.Location = new System.Drawing.Point(3, 301);
            this.bottomPanel.Name = "bottomPanel";
            this.bottomPanel.RowCount = 1;
            this.bottomPanel.RowStyles.Add(new System.Windows.Forms.RowStyle());
            this.bottomPanel.Size = new System.Drawing.Size(864, 34);
            this.bottomPanel.TabIndex = 10;
            // 
            // label1
            // 
            this.label1.AutoSize = true;
            this.label1.Dock = System.Windows.Forms.DockStyle.Fill;
            this.label1.Location = new System.Drawing.Point(3, 7);
            this.label1.Margin = new System.Windows.Forms.Padding(3, 7, 3, 0);
            this.label1.Name = "label1";
            this.label1.Size = new System.Drawing.Size(95, 27);
            this.label1.TabIndex = 8;
            this.label1.Text = "Default Driver";
            // 
            // defaultdriver
            // 
            this.defaultdriver.Dock = System.Windows.Forms.DockStyle.Fill;
            this.defaultdriver.DropDownStyle = System.Windows.Forms.ComboBoxStyle.DropDownList;
            this.defaultdriver.FormattingEnabled = true;
            this.defaultdriver.ImeMode = System.Windows.Forms.ImeMode.Off;
            this.defaultdriver.Location = new System.Drawing.Point(104, 3);
            this.defaultdriver.MinimumSize = new System.Drawing.Size(114, 0);
            this.defaultdriver.Name = "defaultdriver";
            this.defaultdriver.Size = new System.Drawing.Size(430, 24);
            this.defaultdriver.TabIndex = 9;
            this.defaultdriver.SelectedIndexChanged += new System.EventHandler(this.defaultdriver_SelectedIndexChanged);
            // 
            // savelog
            // 
            this.savelog.Enabled = false;
            this.savelog.Location = new System.Drawing.Point(540, 3);
            this.savelog.Name = "savelog";
            this.savelog.Size = new System.Drawing.Size(94, 28);
            this.savelog.TabIndex = 10;
            this.savelog.Text = "Save Log";
            this.savelog.UseVisualStyleBackColor = true;
            this.savelog.Click += new System.EventHandler(this.savelog_Click);
            // 
            // removeConnectionButton
            // 
            this.removeConnectionButton.Enabled = false;
            this.removeConnectionButton.Location = new System.Drawing.Point(640, 3);
            this.removeConnectionButton.Name = "removeConnectionButton";
            this.removeConnectionButton.Size = new System.Drawing.Size(100, 28);
            this.removeConnectionButton.TabIndex = 11;
            this.removeConnectionButton.Text = "Remove";
            this.removeConnectionButton.UseVisualStyleBackColor = true;
            this.removeConnectionButton.Click += new System.EventHandler(this.removeConnectionButton_Click);
            // 
            // clearAllButton
            // 
            this.clearAllButton.Location = new System.Drawing.Point(746, 3);
            this.clearAllButton.Name = "clearAllButton";
            this.clearAllButton.Size = new System.Drawing.Size(94, 28);
            this.clearAllButton.TabIndex = 12;
            this.clearAllButton.Text = "Clear All";
            this.clearAllButton.UseVisualStyleBackColor = true;
            this.clearAllButton.Click += new System.EventHandler(this.clearAllButton_Click);
            // 
            // settingsGroup
            // 
            this.settingsGroup.Controls.Add(this.settingsLayout);
            this.settingsGroup.Dock = System.Windows.Forms.DockStyle.Fill;
            this.settingsGroup.Location = new System.Drawing.Point(3, 341);
            this.settingsGroup.Name = "settingsGroup";
            this.settingsGroup.Size = new System.Drawing.Size(864, 156);
            this.settingsGroup.TabIndex = 11;
            this.settingsGroup.TabStop = false;
            this.settingsGroup.Text = "Auto-Connect Settings (stored in Registry)";
            // 
            // settingsLayout
            // 
            this.settingsLayout.ColumnCount = 4;
            this.settingsLayout.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle());
            this.settingsLayout.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle(System.Windows.Forms.SizeType.Percent, 50F));
            this.settingsLayout.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle());
            this.settingsLayout.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle(System.Windows.Forms.SizeType.Percent, 50F));
            this.settingsLayout.Controls.Add(this.labelProtocol, 0, 0);
            this.settingsLayout.Controls.Add(this.protocolCombo, 1, 0);
            this.settingsLayout.Controls.Add(this.labelBaudRate, 2, 0);
            this.settingsLayout.Controls.Add(this.baudRateCombo, 3, 0);
            this.settingsLayout.Controls.Add(this.labelFlags, 0, 1);
            this.settingsLayout.Controls.Add(this.flagsTextBox, 1, 1);
            this.settingsLayout.Controls.Add(this.labelDeviceName, 2, 1);
            this.settingsLayout.Controls.Add(this.deviceNameTextBox, 3, 1);
            this.settingsLayout.Controls.Add(this.autoInjectCheckBox, 0, 2);
            this.settingsLayout.Controls.Add(this.labelMockVbatt, 2, 2);
            this.settingsLayout.Controls.Add(this.mockVbattTextBox, 3, 2);
            this.settingsLayout.Dock = System.Windows.Forms.DockStyle.Fill;
            this.settingsLayout.Location = new System.Drawing.Point(3, 18);
            this.settingsLayout.Name = "settingsLayout";
            this.settingsLayout.RowCount = 3;
            this.settingsLayout.RowStyles.Add(new System.Windows.Forms.RowStyle());
            this.settingsLayout.RowStyles.Add(new System.Windows.Forms.RowStyle());
            this.settingsLayout.RowStyles.Add(new System.Windows.Forms.RowStyle());
            this.settingsLayout.Size = new System.Drawing.Size(858, 135);
            this.settingsLayout.TabIndex = 0;
            // 
            // labelProtocol
            // 
            this.labelProtocol.AutoSize = true;
            this.labelProtocol.Dock = System.Windows.Forms.DockStyle.Fill;
            this.labelProtocol.Location = new System.Drawing.Point(3, 7);
            this.labelProtocol.Margin = new System.Windows.Forms.Padding(3, 7, 3, 0);
            this.labelProtocol.Name = "labelProtocol";
            this.labelProtocol.Size = new System.Drawing.Size(80, 23);
            this.labelProtocol.TabIndex = 0;
            this.labelProtocol.Text = "Protocol:";
            // 
            // protocolCombo
            // 
            this.protocolCombo.Dock = System.Windows.Forms.DockStyle.Fill;
            this.protocolCombo.DropDownStyle = System.Windows.Forms.ComboBoxStyle.DropDownList;
            this.protocolCombo.FormattingEnabled = true;
            this.protocolCombo.Location = new System.Drawing.Point(89, 3);
            this.protocolCombo.Name = "protocolCombo";
            this.protocolCombo.Size = new System.Drawing.Size(330, 24);
            this.protocolCombo.TabIndex = 1;
            this.protocolCombo.SelectedIndexChanged += new System.EventHandler(this.protocolCombo_SelectedIndexChanged);
            // 
            // labelBaudRate
            // 
            this.labelBaudRate.AutoSize = true;
            this.labelBaudRate.Dock = System.Windows.Forms.DockStyle.Fill;
            this.labelBaudRate.Location = new System.Drawing.Point(425, 7);
            this.labelBaudRate.Margin = new System.Windows.Forms.Padding(3, 7, 3, 0);
            this.labelBaudRate.Name = "labelBaudRate";
            this.labelBaudRate.Size = new System.Drawing.Size(80, 23);
            this.labelBaudRate.TabIndex = 2;
            this.labelBaudRate.Text = "Baud Rate:";
            // 
            // baudRateCombo
            // 
            this.baudRateCombo.Dock = System.Windows.Forms.DockStyle.Fill;
            this.baudRateCombo.FormattingEnabled = true;
            this.baudRateCombo.Location = new System.Drawing.Point(511, 3);
            this.baudRateCombo.Name = "baudRateCombo";
            this.baudRateCombo.Size = new System.Drawing.Size(344, 24);
            this.baudRateCombo.TabIndex = 3;
            this.baudRateCombo.TextChanged += new System.EventHandler(this.baudRateCombo_TextChanged);
            // 
            // labelFlags
            // 
            this.labelFlags.AutoSize = true;
            this.labelFlags.Dock = System.Windows.Forms.DockStyle.Fill;
            this.labelFlags.Location = new System.Drawing.Point(3, 37);
            this.labelFlags.Margin = new System.Windows.Forms.Padding(3, 7, 3, 0);
            this.labelFlags.Name = "labelFlags";
            this.labelFlags.Size = new System.Drawing.Size(80, 23);
            this.labelFlags.TabIndex = 4;
            this.labelFlags.Text = "Flags:";
            // 
            // flagsTextBox
            // 
            this.flagsTextBox.Dock = System.Windows.Forms.DockStyle.Fill;
            this.flagsTextBox.Location = new System.Drawing.Point(89, 33);
            this.flagsTextBox.Name = "flagsTextBox";
            this.flagsTextBox.Size = new System.Drawing.Size(330, 22);
            this.flagsTextBox.TabIndex = 5;
            this.flagsTextBox.Text = "0";
            this.flagsTextBox.TextChanged += new System.EventHandler(this.flagsTextBox_TextChanged);
            // 
            // labelDeviceName
            // 
            this.labelDeviceName.AutoSize = true;
            this.labelDeviceName.Dock = System.Windows.Forms.DockStyle.Fill;
            this.labelDeviceName.Location = new System.Drawing.Point(425, 37);
            this.labelDeviceName.Margin = new System.Windows.Forms.Padding(3, 7, 3, 0);
            this.labelDeviceName.Name = "labelDeviceName";
            this.labelDeviceName.Size = new System.Drawing.Size(80, 23);
            this.labelDeviceName.TabIndex = 6;
            this.labelDeviceName.Text = "Device Name:";
            // 
            // deviceNameTextBox
            // 
            this.deviceNameTextBox.Dock = System.Windows.Forms.DockStyle.Fill;
            this.deviceNameTextBox.Location = new System.Drawing.Point(511, 33);
            this.deviceNameTextBox.Name = "deviceNameTextBox";
            this.deviceNameTextBox.Size = new System.Drawing.Size(344, 22);
            this.deviceNameTextBox.TabIndex = 7;
            this.deviceNameTextBox.TextChanged += new System.EventHandler(this.deviceNameTextBox_TextChanged);
            // 
            // autoInjectCheckBox
            // 
            this.autoInjectCheckBox.AutoSize = true;
            this.settingsLayout.SetColumnSpan(this.autoInjectCheckBox, 2);
            this.autoInjectCheckBox.Checked = true;
            this.autoInjectCheckBox.CheckState = System.Windows.Forms.CheckState.Checked;
            this.autoInjectCheckBox.Location = new System.Drawing.Point(3, 63);
            this.autoInjectCheckBox.Name = "autoInjectCheckBox";
            this.autoInjectCheckBox.Size = new System.Drawing.Size(300, 21);
            this.autoInjectCheckBox.TabIndex = 8;
            this.autoInjectCheckBox.Text = "Auto-inject PassThruConnect on channel-scoped calls";
            this.autoInjectCheckBox.UseVisualStyleBackColor = true;
            this.autoInjectCheckBox.CheckedChanged += new System.EventHandler(this.autoInjectCheckBox_CheckedChanged);
            // 
            // labelMockVbatt
            // 
            this.labelMockVbatt.AutoSize = true;
            this.labelMockVbatt.Dock = System.Windows.Forms.DockStyle.Fill;
            this.labelMockVbatt.Location = new System.Drawing.Point(425, 67);
            this.labelMockVbatt.Margin = new System.Windows.Forms.Padding(3, 7, 3, 0);
            this.labelMockVbatt.Name = "labelMockVbatt";
            this.labelMockVbatt.Size = new System.Drawing.Size(80, 23);
            this.labelMockVbatt.TabIndex = 9;
            this.labelMockVbatt.Text = "Mock VBATT (V):";
            // 
            // mockVbattTextBox
            // 
            this.mockVbattTextBox.Dock = System.Windows.Forms.DockStyle.Fill;
            this.mockVbattTextBox.Location = new System.Drawing.Point(511, 63);
            this.mockVbattTextBox.Name = "mockVbattTextBox";
            this.mockVbattTextBox.Size = new System.Drawing.Size(344, 22);
            this.mockVbattTextBox.TabIndex = 10;
            this.mockVbattTextBox.Text = "0.0";
            this.mockVbattTextBox.TextChanged += new System.EventHandler(this.mockVbattTextBox_TextChanged);
            // 
            // iDDataGridViewTextBoxColumn
            // 
            this.iDDataGridViewTextBoxColumn.DataPropertyName = "ID";
            this.iDDataGridViewTextBoxColumn.HeaderText = "ID";
            this.iDDataGridViewTextBoxColumn.Name = "iDDataGridViewTextBoxColumn";
            this.iDDataGridViewTextBoxColumn.ReadOnly = true;
            this.iDDataGridViewTextBoxColumn.Width = 40;
            // 
            // statusDataGridViewTextBoxColumn
            // 
            this.statusDataGridViewTextBoxColumn.DataPropertyName = "Status";
            this.statusDataGridViewTextBoxColumn.HeaderText = "Status";
            this.statusDataGridViewTextBoxColumn.Name = "statusDataGridViewTextBoxColumn";
            this.statusDataGridViewTextBoxColumn.ReadOnly = true;
            this.statusDataGridViewTextBoxColumn.Width = 80;
            // 
            // eventCountDataGridViewTextBoxColumn
            // 
            this.eventCountDataGridViewTextBoxColumn.DataPropertyName = "EventCount";
            this.eventCountDataGridViewTextBoxColumn.HeaderText = "EventCount";
            this.eventCountDataGridViewTextBoxColumn.Name = "eventCountDataGridViewTextBoxColumn";
            this.eventCountDataGridViewTextBoxColumn.ReadOnly = true;
            this.eventCountDataGridViewTextBoxColumn.Width = 114;
            // 
            // driverDataGridViewTextBoxColumn
            // 
            this.driverDataGridViewTextBoxColumn.DataPropertyName = "Driver";
            this.driverDataGridViewTextBoxColumn.HeaderText = "Driver";
            this.driverDataGridViewTextBoxColumn.Name = "driverDataGridViewTextBoxColumn";
            this.driverDataGridViewTextBoxColumn.ReadOnly = true;
            this.driverDataGridViewTextBoxColumn.Width = 150;
            // 
            // clientDataGridViewTextBoxColumn
            // 
            this.clientDataGridViewTextBoxColumn.DataPropertyName = "Client";
            this.clientDataGridViewTextBoxColumn.HeaderText = "Client";
            this.clientDataGridViewTextBoxColumn.Name = "clientDataGridViewTextBoxColumn";
            this.clientDataGridViewTextBoxColumn.ReadOnly = true;
            this.clientDataGridViewTextBoxColumn.Width = 150;
            // 
            // connectionBindingSource
            // 
            this.connectionBindingSource.DataSource = typeof(PassThruLoggerControl.ConnectionInfo);
            // 
            // J2534LogController
            // 
            this.AutoScaleDimensions = new System.Drawing.SizeF(8F, 16F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.ClientSize = new System.Drawing.Size(870, 500);
            this.Controls.Add(this.tableLayoutPanel1);
            this.Icon = ((System.Drawing.Icon)(resources.GetObject("$this.Icon")));
            this.Name = "J2534LogController";
            this.Text = "PassThruLoggerControl";
            this.FormClosing += new System.Windows.Forms.FormClosingEventHandler(this.Form1_FormClosing);
            this.tableLayoutPanel1.ResumeLayout(false);
            this.splitContainer1.Panel1.ResumeLayout(false);
            this.splitContainer1.Panel2.ResumeLayout(false);
            ((System.ComponentModel.ISupportInitialize)(this.splitContainer1)).EndInit();
            this.splitContainer1.ResumeLayout(false);
            ((System.ComponentModel.ISupportInitialize)(this.loggerconnections)).EndInit();
            ((System.ComponentModel.ISupportInitialize)(this.connectionBindingSource)).EndInit();
            this.settingsGroup.ResumeLayout(false);
            this.settingsLayout.ResumeLayout(false);
            this.settingsLayout.PerformLayout();
            this.bottomPanel.ResumeLayout(false);
            this.bottomPanel.PerformLayout();
            this.ResumeLayout(false);

        }

        #endregion
        private System.Windows.Forms.SaveFileDialog logsavewindow;
        private BindingSource connectionBindingSource;
        private TableLayoutPanel tableLayoutPanel1;
        private SplitContainer splitContainer1;
        private DataGridView loggerconnections;
        private DataGridViewTextBoxColumn iDDataGridViewTextBoxColumn;
        private DataGridViewTextBoxColumn statusDataGridViewTextBoxColumn;
        private DataGridViewTextBoxColumn eventCountDataGridViewTextBoxColumn;
        private DataGridViewTextBoxColumn driverDataGridViewTextBoxColumn;
        private DataGridViewTextBoxColumn clientDataGridViewTextBoxColumn;
        private ListBox logpreview;
        private TableLayoutPanel bottomPanel;
        private Button savelog;
        private Button clearAllButton;
        private Button removeConnectionButton;
        private ComboBox defaultdriver;
        private Label label1;
        private GroupBox settingsGroup;
        private TableLayoutPanel settingsLayout;
        private Label labelProtocol;
        private ComboBox protocolCombo;
        private Label labelBaudRate;
        private ComboBox baudRateCombo;
        private Label labelFlags;
        private TextBox flagsTextBox;
        private Label labelDeviceName;
        private TextBox deviceNameTextBox;
        private CheckBox autoInjectCheckBox;
        private Label labelMockVbatt;
        private TextBox mockVbattTextBox;
    }
}

