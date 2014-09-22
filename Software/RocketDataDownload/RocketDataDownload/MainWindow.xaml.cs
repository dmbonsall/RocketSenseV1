using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Navigation;
using System.Windows.Shapes;
using System.IO.Ports;
using System.IO;
using System.Threading;

namespace RocketDataDownload
{
    /// <summary>
    /// Interaction logic for MainWindow.xaml
    /// </summary>
    public partial class MainWindow : Window
    {
        SerialPort serialPort;

        int[] baudRates = { 1200, 2400, 9600, 19200, 115200 };

        const int deviceGetInfoCommand = 6;
        const int deviceDownloadCommand = 5;
        const int deviceReformatCommand = 4;

        private delegate void DownloadProgressBarDelegate(int numIncrement);
        private delegate void DownloadStatusLabelDelegate(string text);
        private DownloadProgressBarDelegate downloadProgBarDel;
        private DownloadStatusLabelDelegate downloadLabelDel;

        public MainWindow()
        {
            InitializeComponent();
            serialPort = new SerialPort();  //initialize the serial port object
            downloadProgBarDel += incrementDownloadProgressBar;
            downloadLabelDel += setDownloadLabelText;
        }

        private int loadPorts()
        {
            string[] ports = SerialPort.GetPortNames(); //get the port names
            portComboBox.Items.Clear();
            for (int i = 0; i < ports.Count(); i++) //add all of the port names to the combo box
            {
                portComboBox.Items.Add(ports[i]);
            }

            return ports.Count();
        }
        private void openPort()
        {
            serialPort.PortName = (string)portComboBox.SelectedItem;
            serialPort.BaudRate = (int)baudComboBox.SelectedItem;

            try
            {
                serialPort.Open();
            }
            catch (Exception ex)
            {
                MessageBox.Show(ex.ToString());
            }
            portComboBox.IsEnabled = false;
            baudComboBox.IsEnabled = false;
            openClosePortButton.Content = "Close Port";
            portStatusLabel.Text = serialPort.PortName + " open";
        }
        private void closePort()
        {
            try
            {
                serialPort.Close();
            }
            catch (Exception ex)
            {
                MessageBox.Show(ex.ToString());
            }

            portComboBox.IsEnabled = true;
            baudComboBox.IsEnabled = true;
            openClosePortButton.Content = "Open Port";
            portStatusLabel.Text = "No Device";
        }
        private void reformatDeviceMemory()
        {

        }
        private void downloadData(object filePath)
        {
            if (serialPort.IsOpen)  //if the port is open
            {
                using(BinaryWriter writer = new BinaryWriter(new FileStream((string)filePath, FileMode.Create)))
                {
                    try
                    {
                        this.Dispatcher.BeginInvoke(downloadLabelDel, "Starting Download");

                        serialPort.DiscardInBuffer();   //clear the input buffer
                        byte[] sendBuffer = { deviceDownloadCommand };
                        serialPort.Write(sendBuffer, 0, sendBuffer.Count());    //send the downloadCommand

                        byte[] preamblePacket = { (byte)serialPort.ReadByte(), (byte)serialPort.ReadByte(), (byte)serialPort.ReadByte() };  //get the preamble to make sure command was recieved

                        if (!(preamblePacket[0] == 0x01 && preamblePacket[1] == 0x02 && preamblePacket[2] == 0x03)) //if the preamble packet is not as expected
                        {
                            this.Dispatcher.BeginInvoke(downloadLabelDel, "Download Failed");
                            return;
                        }
                        //if packet is as expected, continue

                        this.Dispatcher.BeginInvoke(downloadLabelDel, "Downloading Data...");
                        this.Dispatcher.BeginInvoke(new Action(() => resetDownloadProgressBar()), null);
                        while (true)    //intentional
                        {
                            byte data = (byte)serialPort.ReadByte();    //get the first byte

                            //look for end of transmission signal
                            if (data == 0x03)   //data byte one
                            {
                                byte dataTwo = (byte)serialPort.ReadByte();
                                if (dataTwo == 0x02)    //data byte two
                                {
                                    byte dataThree = (byte)serialPort.ReadByte();
                                    if (dataThree == 0x01)  //data byte three
                                    {
                                        break;
                                    }
                                    else
                                    {
                                        writer.Write(data);
                                        writer.Write(dataTwo);
                                        writer.Write(dataThree);
                                        this.Dispatcher.BeginInvoke(downloadProgBarDel, 3);
                                        continue;
                                    }
                                }
                                else
                                {
                                    writer.Write(data);
                                    writer.Write(dataTwo);
                                    this.Dispatcher.BeginInvoke(downloadProgBarDel, 2);
                                    continue;
                                }
                            }

                            writer.Write(data); //if normal data
                            this.Dispatcher.BeginInvoke(downloadProgBarDel, 1);
                        }

                        this.Dispatcher.BeginInvoke(downloadLabelDel, "Download Complete");
                    }
                    catch (Exception ex)
                    {
                        MessageBox.Show(ex.ToString());
                        this.Dispatcher.BeginInvoke(downloadLabelDel, "Download Failed");
                    }
                }

                if (reformatCheckBox.IsChecked == true)
                {
                    this.Dispatcher.Invoke(new Action(() => reformatDeviceMemory()));
                }
            }

        }

        private void incrementDownloadProgressBar(int numIncrement)
        {
            downloadProgressBar.Value += numIncrement;
        }

        private void setDownloadLabelText(string text)
        {
            downloadStatusLabel.Text = text;
        }

        private void resetDownloadProgressBar()
        {
            this.downloadProgressBar.Value = 0;
        }

        private void Window_Loaded(object sender, RoutedEventArgs e)
        {
            int numPorts = loadPorts(); //load the ports
            portComboBox.SelectedIndex = numPorts - 1; //set to last index (if there is any other port other that COM1, set it to that)

            for (int i = 0; i < baudRates.Count(); i++) //add all of the baud rates to the combo box
            {
                baudComboBox.Items.Add(baudRates[i]);
            }

            baudComboBox.SelectedIndex = baudRates.Count() - 1; //set to last index which will be 115200

        }

        private void openClosePortButton_Click(object sender, RoutedEventArgs e)
        {
            if (serialPort.IsOpen)  //open, need to close
            {
                closePort();
            }
            else   //closed, need to open
            {
                openPort();
            }
        }

        private void reformatButton_Click(object sender, RoutedEventArgs e)
        {
            if (serialPort.IsOpen)  //only excecute if the port is open
            {
                reformatDeviceMemory();
            }
        }

        private void portComboBox_DropDownOpened(object sender, EventArgs e)
        {
            string prevItem = (string)portComboBox.SelectedItem;    //get the currently selected item
            int numPorts = loadPorts(); //reload the ports

            for (int i = 0; i < numPorts; i++)  //if the previous item exists, make it the selected item
            {
                if (portComboBox.Items[i].Equals(prevItem))
                {
                    portComboBox.SelectedIndex = i;
                    return; //we're done so return
                }
            }

            portComboBox.SelectedIndex = 0; //if previous item not found, set to the first index

        }

        private void openButton_Click(object sender, RoutedEventArgs e)
        {
            // Configure save file dialog box
            Microsoft.Win32.SaveFileDialog dlg = new Microsoft.Win32.SaveFileDialog();
            dlg.FileName = "Data"; // Default file name
            dlg.DefaultExt = ".txt"; // Default file extension
            dlg.Filter = "Text documents (.txt)|*.txt"; // Filter files by extension 

            // Show open file dialog box
            Nullable<bool> result = dlg.ShowDialog();

            // Process open file dialog box results 
            if (result == true)
            {
                //Open document 
                filePathBox.Text = dlg.FileName;
            }
        }

        private void downloadButton_Click(object sender, RoutedEventArgs e)
        {
            Thread thread = new Thread(new ParameterizedThreadStart(downloadData));
            string test = String.Copy(filePathBox.Text);
            thread.Start(test);
        }


    }
}
