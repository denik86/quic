/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2019 SIGNET Lab, Department of Information Engineering, University of Padova
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors: Alvise De Biasio <alvise.debiasio@gmail.com>
 *          Federico Chiariotti <whatever@blbl.it>
 *          Michele Polese <michele.polese@gmail.com>
 *          Davide Marcato <davidemarcato@outlook.com>
 *  

receivers      sources

   sources  --> sinks

   n2 ---+      +--- n5  // cloud gaming - commands
         |      |
   n3 ---n0 -- n1--- n6  // video streaming
         |      |
   n4 ---+      +--- n7  // online game


 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/quic-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/applications-module.h"
#include "ns3/config-store-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include <iostream>
#include <ctime>

#include <bits/stdc++.h> 
#include <sys/stat.h> 
#include <sys/types.h>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("quic-tester");

static double unit = 0.1; // unita tempo (s) utilizzata per definire l' intervallo di ogni misura

// Chiamato ogni unita' di tempo
// stampa sullo stream la dimensione dati ricevuti cumulata: tx * 8 / unit (in Mbps), per ogni sink/flusso
static void
RxThroughput (Ptr<OutputStreamWrapper> stream, uint32_t *cumRx)
{ 
  double rx = (double) *cumRx;
  *stream->GetStream ()<< Simulator::Now ().GetSeconds () <<"\t" << rx * 8.0 / unit / 1024 / 1024 << std::endl;
  *cumRx = 0;
  Simulator::Schedule (Seconds (unit), &RxThroughput, stream, cumRx);
}

// Chiamato ogni unita' di tempo
// stampa sullo stream la dimensione dati trasmessi cumulata: tx * 8 / unit (in Mbps).
static void
TxThroughput (Ptr<OutputStreamWrapper> stream, uint32_t *cumTx)
{ 
  double tx = (double) *cumTx;
  *stream->GetStream ()<< Simulator::Now ().GetSeconds () <<"\t" << tx * 8.0 / unit / 1024 / 1024 << std::endl;
  *cumTx = 0;
  Simulator::Schedule (Seconds (unit), &TxThroughput, stream, cumTx);
}

static void
CwndChange (Ptr<OutputStreamWrapper> stream, uint32_t oldCwnd, uint32_t newCwnd)
{ 
  //std::cout << Simulator::Now ().GetSeconds () << " change CWND\n";
  //double oldCwndM = (double)oldCwnd / 1024 / 1024;
  double newCwndM = (double)newCwnd / 1024 / 1024;
  //double oldCwndBit = oldCwndM * 8;
  double newCwndBit = newCwndM * 8;
  *stream->GetStream () << Simulator::Now ().GetSeconds () << "\t" << newCwndM<< "\t" << newCwndBit << std::endl;
}

static void
RttChange (Ptr<OutputStreamWrapper> stream, Time oldRtt, Time newRtt)
{
  *stream->GetStream () << Simulator::Now ().GetSeconds () << "\t" << oldRtt.GetSeconds () << "\t" << newRtt.GetSeconds () << std::endl;
}

// Chiamato ogni volta che un pacchetto arriva ad un sink

static void
Rx (Ptr<OutputStreamWrapper> stream, uint32_t *cumRx, Ptr<const Packet> p, Ptr<Ipv4> ipv4, unsigned int val)
//Rx (Ptr<OutputStreamWrapper> stream, uint32_t *cumRx, Ptr<const Packet> p, const QuicHeader& q, Ptr<const QuicSocketBase> qsb)
{
  *cumRx += p->GetSize();
  *stream->GetStream () << Simulator::Now ().GetSeconds () << "\t" << p->GetSize() << std::endl;
}

static void
Tx (Ptr<OutputStreamWrapper> stream, uint32_t *cumTx, Ptr<const Packet> p, Ptr<Ipv4> ipv4, unsigned int val)
{
  *cumTx += p->GetSize();
  *stream->GetStream () << Simulator::Now ().GetSeconds () << "\t" << p->GetSize() << std::endl;
}

// array dei valori precedenti
static int prevDrops[100];
static int prevTx[100];
static int prevRx[100];
static Ptr<OutputStreamWrapper> flowStreams[100]; // puntatori agli oggetti stream di ogni flow

// stampa le metriche per ogni intervallo di tempo definito da unit
static void StatsFlowMonitor(std::string traceDir, Ptr<FlowMonitor> monitor)
{

  //Prende le statistiche dall'oggetto FlowMonitor
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();
  for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator iter = stats.begin (); iter != stats.end (); ++iter)
  {
    // Per ogni flusso...
    std::string fId = std::to_string(iter->first);
    int flowId = std::atoi(fId.c_str());
    if(flowStreams[flowId]==NULL) 
    {
      AsciiTraceHelper asciiTraceHelper;
      std::ostringstream fileStats;
      fileStats << "./"+traceDir+"flowTrace_"+fId+".txt";
      flowStreams[flowId] = asciiTraceHelper.CreateFileStream (fileStats.str ().c_str ());
    }

    int lostPackets = iter->second.lostPackets - prevDrops[flowId];
    int txBytes = iter->second.txBytes - prevTx[flowId];
    int rxBytes = iter->second.rxBytes - prevRx[flowId];

    double txThroughput = txBytes * 8.0 / unit / 1024 / 1024;
    double rxThroughput = rxBytes * 8.0 / unit / 1024 / 1024;
    
    *flowStreams[flowId]->GetStream () << Simulator::Now ().GetSeconds () << "\t" << 
    lostPackets << "\t" << txThroughput << "\t" << rxThroughput << std::endl;
    
    prevDrops[flowId] = iter->second.lostPackets;
    prevTx[flowId] = iter->second.txBytes;
    prevRx[flowId] = iter->second.rxBytes; 
      
  }
  // Rischedula il metodo stesso ad ogni unita' di tempo
  Simulator::Schedule (Seconds (unit), &StatsFlowMonitor, traceDir, monitor);
}

static void
Traces(uint32_t nodeId, std::string pathVersion, std::string finalPart)
{
  AsciiTraceHelper asciiTraceHelper;

  std::ostringstream pathCW;
  pathCW << "/NodeList/" << nodeId << "/$ns3::QuicL4Protocol/SocketList/0/QuicSocketBase/CongestionWindow";
  std::ostringstream fileCW;
  fileCW << pathVersion << "QUIC-cwnd-change"  << nodeId << "" << finalPart;

  std::ostringstream pathRTT;
  pathRTT << "/NodeList/" << nodeId << "/$ns3::QuicL4Protocol/SocketList/0/QuicSocketBase/RTT";
  std::ostringstream fileRTT;
  fileRTT << pathVersion << "QUIC-rtt"  << nodeId << "" << finalPart;

  std::ostringstream pathRCWnd;
  pathRCWnd<< "/NodeList/" << nodeId << "/$ns3::QuicL4Protocol/SocketList/0/QuicSocketBase/RWND";
  std::ostringstream fileRCWnd;
  fileRCWnd<<pathVersion << "QUIC-rwnd-change"  << nodeId << "" << finalPart;

  std::ostringstream pathRx;
  //pathRx << "/NodeList/" << nodeId << "/$ns3::QuicL4Protocol/SocketList/*/QuicSocketBase/Rx";
  pathRx << "/NodeList/" << nodeId << "/$ns3::Ipv4L3Protocol/Rx";
  std::ostringstream fileRx;
  fileRx << pathVersion << "rx-data" << nodeId << "" << finalPart;

  std::ostringstream pathTx;
  //pathTx << "/NodeList/" << nodeId << "/$ns3::QuicL4Protocol/SocketList/*/QuicSocketBase/Tx";
   pathTx << "/NodeList/" << nodeId << "/$ns3::Ipv4L3Protocol/Tx";
  std::ostringstream fileTx;
  fileTx << pathVersion << "tx-data" << nodeId << "" << finalPart;

  std::ostringstream fileThrRx;
  fileThrRx << pathVersion << "rx-Throughput" << nodeId << "" << finalPart;
  std::ostringstream fileThrTx;
  fileThrTx << pathVersion << "tx-Throughput" << nodeId << "" << finalPart;
  uint32_t * cumRx = new uint32_t(0);
  uint32_t * cumTx = new uint32_t(0);
  
  Ptr<OutputStreamWrapper> streamRx = asciiTraceHelper.CreateFileStream (fileRx.str ().c_str ());
  Config::ConnectWithoutContext (pathRx.str ().c_str (), MakeBoundCallback (&Rx, streamRx, cumRx));

  Ptr<OutputStreamWrapper> streamTx = asciiTraceHelper.CreateFileStream (fileTx.str ().c_str ());
  Config::ConnectWithoutContext (pathTx.str ().c_str (), MakeBoundCallback (&Tx, streamTx, cumTx));

  Ptr<OutputStreamWrapper> stream1 = asciiTraceHelper.CreateFileStream (fileCW.str ().c_str ());
  Config::ConnectWithoutContext (pathCW.str ().c_str (), MakeBoundCallback(&CwndChange, stream1));

  Ptr<OutputStreamWrapper> stream2 = asciiTraceHelper.CreateFileStream (fileRTT.str ().c_str ());
  Config::ConnectWithoutContext (pathRTT.str ().c_str (), MakeBoundCallback(&RttChange, stream2));

  Ptr<OutputStreamWrapper> stream4 = asciiTraceHelper.CreateFileStream (fileRCWnd.str ().c_str ());
  Config::ConnectWithoutContext (pathRCWnd.str ().c_str (), MakeBoundCallback(&CwndChange, stream4));
 
  Ptr<OutputStreamWrapper> streamThrRx = asciiTraceHelper.CreateFileStream (fileThrRx.str ().c_str ());
  Simulator::Schedule (Seconds (0), &RxThroughput, streamThrRx, cumRx);

  Ptr<OutputStreamWrapper> streamThrTx = asciiTraceHelper.CreateFileStream (fileThrTx.str ().c_str ());
  Simulator::Schedule (Seconds (0), &TxThroughput, streamThrTx, cumTx);
}

int
main (int argc, char *argv[])
{
  std::string s_file(argv[0]); // scenario file
  std::string d = "scratch/"; // delimiter
  std::string init_dir = s_file.substr(s_file.find(d)+d.length(), s_file.length()-1);
  std::string traceDir = "";

  time_t rawtime;
  struct tm * timeinfo;
  char t_buffer[80];
  time(&rawtime);
  timeinfo = localtime(&rawtime);
  strftime(t_buffer, sizeof(t_buffer), "%d-%m-%Y", timeinfo);
  std::string s_time(t_buffer);

  bool dirOK = false;
  int fi = 1;
  while(!dirOK)
  {
    traceDir = init_dir + "_" + s_time + "_" + std::to_string(fi) + "/";
    const char *traceDirChar = traceDir.c_str();
    // 0777 sono i permessi
    if (mkdir(traceDirChar, 0777) == -1) 
      fi++;
      //std::cerr << "Error :  " << strerror(errno) << std::endl; 
    else {
      std::cout << "Trace Directory '" + traceDir + "' created";
      dirOK = true;
    }
  }

  bool flow_monitor = true;
  bool pcap = false;
  int nFlows = 1;
  double duration = 5;

  std::string btnBandwidth= "10Mbps";
  std::string btnDelay= "30ms";

  std::string csmaBandwidth= "100Mbps";
  std::string csmaDelay= "3000ns";

  std::string congestion = "TcpNewReno";


  // Flows
  std::string transport_prot[nFlows];
  int nodeId[nFlows];
  int maxPackets[nFlows];
  int interval[nFlows];
  int packetSize[nFlows];
  double receiverStartTime[nFlows];
  double receiverStopTime[nFlows];
  double sourceStartTime[nFlows];
  double sourceStopTime[nFlows];
  int port[nFlows];

  // Flow 1 - CLOUD GAME STREAMING
  int idx = 0;
  nodeId[idx] = 1;
  port[idx] = 900;
  transport_prot[idx] = "quic"; // udp, tcp
  receiverStartTime[idx] = 0.9;
  receiverStopTime[idx] = duration-1.0;
  sourceStartTime[idx] = 1.0;
  sourceStopTime[idx] = duration-1.0;
  maxPackets[idx] = 2000000;
  interval[idx] = 200; // 10.5 Mbps
  packetSize[idx] = 1211;
/*
  // Flow 2 - VIDEO STREAMING 4k
  idx = 1;
  nodeId[idx] = 2;
  port[idx] = 911;
  transport_prot[idx] = "quic"; // udp, tcp
  receiverStartTime[idx] = 50;
  receiverStopTime[idx] = duration-1.0;
  sourceStartTime[idx] = 50;
  sourceStopTime[idx] = duration-1.0;
  maxPackets[idx] = 2000000;
  interval[idx] = 759;  //15 Mbps
  packetSize[idx] = 1450; // non maggiore di 1500 perche esplode.

  // Flow 3 - ONLINE GAME
  idx = 2;
  nodeId[idx] = 3;
  port[idx] = 922;
  transport_prot[idx] = "quic"; // udp, tcp
  receiverStartTime[idx] = 0.9;
  receiverStopTime[idx] = duration-1.0;
  sourceStartTime[idx] = 10.0;
  sourceStopTime[idx] = duration-1.0;
  maxPackets[idx] = 2000000;
  interval[idx] = 17060;
  packetSize[idx] = 359;*
  */

  congestion = std::string ("ns3::") + congestion;

std::cout << "\n#################### SIMULATION SET-UP ####################\n";

 LogLevel log_precision = LOG_LEVEL_INFO;
  Time::SetResolution (Time::NS);
 // LogComponentEnableAll (LOG_PREFIX_TIME);
 // LogComponentEnableAll (LOG_PREFIX_FUNC);
 // LogComponentEnableAll (LOG_PREFIX_NODE);
  //LogComponentEnable ("QuicEchoClientApplication", log_precision);
 // LogComponentEnable ("QuicEchoServerApplication", log_precision);
//  LogComponentEnable ("QuicHeader", log_precision);
 //LogComponentEnable ("QuicSocketBase", log_precision);
 // LogComponentEnable ("QuicStreamBase", LOG_LEVEL_LOGIC);
 LogComponentEnable ("Socket", log_precision);
 // LogComponentEnable ("Application", log_precision);
 LogComponentEnable ("Node", log_precision);
 LogComponentEnable ("InternetStackHelper", log_precision);
//  LogComponentEnable ("QuicSocketFactory", log_precision);
//  LogComponentEnable ("ObjectFactory", log_precision);
//  //LogComponentEnable ("TypeId", log_precision);
//  LogComponentEnable ("QuicL4Protocol", log_precision);
//  LogComponentEnable ("QuicL5Protocol", log_precision);
//  LogComponentEnable ("ObjectBase", log_precision);
//  LogComponentEnable ("QuicEchoHelper", log_precision);
 // LogComponentEnable ("QuicSocketTxBuffer", log_precision);
//LogComponentEnable ("QuicSocketRxBuffer", log_precision);
//  LogComponentEnable ("QuicHeader", log_precision);
//  LogComponentEnable ("QuicSubheader", log_precision);
//  LogComponentEnable ("Header", log_precision);
//  LogComponentEnable ("PacketMetadata", log_precision);

  // 4 MB of buffer
  Config::SetDefault ("ns3::QuicSocketBase::SocketRcvBufSize", UintegerValue (1 << 20));
  Config::SetDefault ("ns3::QuicSocketBase::SocketSndBufSize", UintegerValue (1 << 20));
  Config::SetDefault ("ns3::QuicStreamBase::StreamSndBufSize", UintegerValue (1 << 20));
  Config::SetDefault ("ns3::QuicStreamBase::StreamRcvBufSize", UintegerValue (1 << 20));
 
  // Select congestion control variant
  if (congestion.compare ("ns3::TcpWestwoodPlus") == 0)
    { 
      // TcpWestwoodPlus is not an actual TypeId name; we need TcpWestwood here
      Config::SetDefault ("ns3::QuicL4Protocol::SocketType", TypeIdValue (TcpWestwood::GetTypeId ()));
      // the default protocol type in ns3::TcpWestwood is WESTWOOD
      Config::SetDefault ("ns3::TcpWestwood::ProtocolType", EnumValue (TcpWestwood::WESTWOODPLUS));
    }
  else
    {
      TypeId tcpTid;
      NS_ABORT_MSG_UNLESS (TypeId::LookupByNameFailSafe (congestion, &tcpTid), "TypeId " << congestion << " not found");
      Config::SetDefault ("ns3::QuicL4Protocol::SocketType", TypeIdValue (TypeId::LookupByName (congestion)));
    }

//** POINT TO POINT - Bottleneck **//
  std::cout << "\n#### BOTTLENECK ####\n";

  NodeContainer gateways;
  gateways.Create (2);

  PointToPointHelper bottleneck;
  bottleneck.SetDeviceAttribute ("DataRate", StringValue (btnBandwidth));
  bottleneck.SetChannelAttribute ("Delay", StringValue (btnDelay));

  NetDeviceContainer devices;
  devices = bottleneck.Install (gateways);
//** END **//

  std::cout << "\n#### RECEIVERS ####\n";
  NodeContainer receivers;
  receivers.Create (nFlows);
  
  PointToPointHelper b;
  // capire se p2p helper accetta piu nodi o deve avere solo 2 nodi.

  NodeContainer wifiApNode = p2pNodes.Get (0);


  WifiHelper wifi;
  wifi.SetRemoteStationManager ("ns3::AarfWifiManager");

  WifiMacHelper mac;

  Ssid ssid = Ssid ("ns-3-ssid");
  mac.SetType ("ns3::StaWifiMac",
    "Ssid", SsidValue (ssid),
    "ActiveProbing", BooleanValue (false));

  NetDeviceContainer staDevices;
  staDevices = wifi.Install (phy, mac, receivers);

  mac.SetType ("ns3::ApWifiMac",
               "Ssid", SsidValue (ssid));

  NetDeviceContainer apDevices;
  apDevices = wifi.Install (phy, mac, wifiApNode);

  MobilityHelper mobility;
  mobility.SetPositionAllocator ("ns3::GridPositionAllocator",
    "MinX", DoubleValue (0.0),
    "MinY", DoubleValue (0.0),
    "DeltaX", DoubleValue (5.0),
    "DeltaY", DoubleValue (10.0),
    "GridWidth", UintegerValue (3),
    "LayoutType", StringValue ("RowFirst"));
  mobility.SetMobilityModel ("ns3::RandomWalk2dMobilityModel",
    "Bounds", RectangleValue (Rectangle (-50, 50, -50, 50)));
  mobility.Install (receivers);
  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobility.Install (wifiApNode);


std::cout << "\n#### CREAZIONE NODI CSMA ####\n\n";
//** CSMA - SOURCES **//
  NodeContainer sources;
  sources.Add (p2pNodes.Get(1));
  sources.Create (nFlows);

  CsmaHelper csmaS;
  csmaS.SetChannelAttribute ("DataRate", StringValue (csmaBandwidth));
  csmaS.SetChannelAttribute ("Delay", StringValue (csmaDelay));

  NetDeviceContainer csmaDevicesS;
  csmaDevicesS = csmaS.Install (sources);
//** END **//


  std::cout << "\n#### INSTALLAZIONE STACK & ASSEGNAZIONE IP ####\n\n";
  
  // QUIC

  QuicHelper stack; 

  stack.InstallQuic (wifiApNode);
  stack.InstallQuic (receivers);
  stack.InstallQuic (sources);

  

  
  // INTERNET
/*
  InternetStackHelper stack;
  stack.Install (receivers);
  stack.Install (sources);
  stack.Install (wifiApNode);
*/

  Ipv4AddressHelper address;

  address.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer interfaces = address.Assign (devices);

  address.SetBase ("10.1.2.0", "255.255.255.0");
   Ipv4InterfaceContainer intR = address.Assign (staDevices);
  address.Assign (apDevices);


  address.SetBase ("10.1.3.0", "255.255.255.0");
  Ipv4InterfaceContainer intCsmaS = address.Assign (csmaDevicesS);

  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();
  

  


std::cout << "\n\n#### CREAZIONE FLUSSI ####\n\n";
  for(uint16_t i = 0; i < nFlows; i++)
  {
    ApplicationContainer sourceApps;
    ApplicationContainer sinkApps;
   
/*
    if(i == 1) {
      BulkSendHelper source ("ns3::TcpSocketFactory",InetSocketAddress (intR.GetAddress (nodeId[i]-1), port[i]));
      // Set the amount of data to send in bytes.  Zero is unlimited.
      source.SetAttribute ("MaxBytes", UintegerValue (0));
      sourceApps = source.Install (sources.Get(nodeId[i]));

      // Create a PacketSinkApplication and install it on node 1
      PacketSinkHelper sink ("ns3::TcpSocketFactory",
                             InetSocketAddress (Ipv4Address::GetAny (), port[i]));
      sinkApps = sink.Install (receivers.Get(nodeId[i]-1));


    } else {
*/
      //Sources (QuicClient invia un flusso dati)
      
      //----- QUIC
      //Client (il parametro e' il nodo a cui invio il flusso)
      QuicClientHelper source (intR.GetAddress (nodeId[i]-1), port[i]);
      //------UDP
      //UdpClientHelper source (intR.GetAddress (nodeId[i]-1), port[i]);

      source.SetAttribute ("MaxPackets", UintegerValue (maxPackets[i]));
      source.SetAttribute ("Interval", TimeValue (MicroSeconds (interval[i])));
      source.SetAttribute ("PacketSize", UintegerValue (packetSize[i]));
      sourceApps = source.Install (sources.Get(nodeId[i]));


      // Receivers (QuicServer riceve i dati)
     // UdpServerHelper sink (port[i]);
      QuicServerHelper sink (port[i]);
      sinkApps = sink.Install (receivers.Get(nodeId[i]-1));
      
     
    //}

    sourceApps.Start (Seconds (sourceStartTime[i]));
    sourceApps.Stop (Seconds (sourceStopTime[i]));

    sinkApps.Start (Seconds (receiverStartTime[i]));
    sinkApps.Stop (Seconds (receiverStopTime[i]));

    // Trace
    auto n1 = receivers.Get (nodeId[i] - 1);
    auto n2 = sources.Get (nodeId[i]);
    Time t1 = Seconds(receiverStartTime[i]+0.1);
    Time t2 = Seconds(sourceStartTime[i]+0.1);
    Simulator::Schedule (t2, &Traces, n2->GetId(), "./"+traceDir+"source_", ".txt");
    Simulator::Schedule (t1, &Traces, n1->GetId(), "./"+traceDir+"receiver_", ".txt");
  }

/*

  // flussi di ritorno (comandi)

  // Flusso 1 (Cloud Gaming)
  int idn = 0;
  QuicServerHelper server1 (933);
  // ------UDP
  //UdpServerHelper server (port[i]);
    
  ApplicationContainer serverApps1 = server1.Install (sources.Get(nodeId[idn]));
  serverApps1.Start (Seconds (receiverStartTime[idn]));
  serverApps1.Stop (Seconds (receiverStopTime[idn]));

  // Sources (QuicClient invia un flusso dati)
  //----- QUIC
  // Client (il parametro e' il nodo a cui invio il flusso)
  QuicClientHelper client1 (intCsmaS.GetAddress (nodeId[idn]), 933);
  //------UDP
  //UdpClientHelper client (intCsma1.GetAddress (nodeId[i]), port[i]);

  client1.SetAttribute ("MaxPackets", UintegerValue(100000));
  client1.SetAttribute ("Interval", TimeValue (MicroSeconds (6250)));
  client1.SetAttribute ("PacketSize", UintegerValue (128));
  ApplicationContainer clientApps1 = client1.Install (receivers.Get(nodeId[idn]-1));
  clientApps1.Start (Seconds (sourceStartTime[idn]+0.05));
  clientApps1.Stop (Seconds (sourceStopTime[idn]));


  // Flusso 2 (Online Game)
  idn = 2;
  QuicServerHelper server2 (944);
  // ------UDP
  //UdpServerHelper server (port[i]);
    
  ApplicationContainer serverApps2 = server2.Install (sources.Get(nodeId[idn]));
  serverApps2.Start (Seconds (receiverStartTime[idn]));
  serverApps2.Stop (Seconds (receiverStopTime[idn]));

  // Sources (QuicClient invia un flusso dati)

  //----- QUIC
  // Client (il parametro e' il nodo a cui invio il flusso)
  QuicClientHelper client2 (intCsmaS.GetAddress (nodeId[idn]), 944);
  //------UDP
  //UdpClientHelper client (intCsma1.GetAddress (nodeId[i]), port[i]);

  client2.SetAttribute ("MaxPackets", UintegerValue(100000));
  client2.SetAttribute ("Interval", TimeValue (MicroSeconds (19194)));
  client2.SetAttribute ("PacketSize", UintegerValue (127));
  ApplicationContainer clientApps2 = client2.Install (receivers.Get(nodeId[idn]-1));
  clientApps2.Start (Seconds (sourceStartTime[idn]+0.05));
  clientApps2.Stop (Seconds (sourceStopTime[idn]));
*/

	if (pcap)
	{
	  pointToPoint.EnablePcapAll (traceDir + "pcap_trace", true);
	}

  //Packet::EnablePrinting ();
  //Packet::EnableChecking ();

	FlowMonitorHelper flowHelper;
  double binWidth = 0.0001;
	Config::SetDefault ("ns3::FlowMonitor::DelayBinWidth", DoubleValue (binWidth));
    if (flow_monitor)
    {
    	flowHelper.InstallAll ();
    }
 
  Ptr<FlowMonitor> monitor = flowHelper.GetMonitor();
  Simulator::Schedule (Seconds(0.1), &StatsFlowMonitor, traceDir, monitor);


  Simulator::Stop (Seconds (duration));

  std::cout << "\n\n#################### STARTING RUN ####################\n\n";
  Simulator::Run ();
  std::cout
      << "\n\n#################### RUN FINISHED ####################\n\n";
  Simulator::Destroy ();



  // Stampa dati presi da flowMonitor
  if (flow_monitor)
  {
    flowHelper.SerializeToXmlFile (traceDir+"flows.flowmonitor", true, true);

    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();
    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator iter = stats.begin (); iter != stats.end (); ++iter)
    {
      std::string ind = std::to_string(iter->first);

      std::string delayFile = traceDir+"flow"+ind+"delay.txt";
      std::ofstream osd (delayFile.c_str (), std::ios::out|std::ios::binary);
      Histogram delays = iter->second.delayHistogram;
      uint32_t totalRecvPackets = iter->second.rxPackets;
      int cum = 0;
      for (uint32_t index = 0; index < delays.GetNBins(); index++) {
        if (delays.GetBinCount(index)) {
          cum += delays.GetBinCount(index);
          double perc = (double)delays.GetBinCount(index)/totalRecvPackets;
          double percCum = (double)cum/totalRecvPackets;
          osd << (index*binWidth)*1000 << "\t" << delays.GetBinCount(index) << "\t" << perc
                                  << "\t" << cum << "\t" << percCum << "\n";
        }
      }
/*
      std::string jitterFile = traceDir"flow"+ind+"jitter.txt";
      std::ofstream osj (jitterFile.c_str (), std::ios::out|std::ios::binary);

      std::string packetSizeFile = traceDir+"flow"+ind+"packetSize.txt";
      std::ofstream oss (packetSizeFile.c_str (), std::ios::out|std::ios::binary);

      Histogram jitters = iter->second.jitterHistogram;
      Histogram packetSizes = iter->second.packetSizeHistogram; 
      */
    }
  }


  std::cout
      << "\n\n#################### SIMULATION END ####################\n\n";
  return 0;
}
